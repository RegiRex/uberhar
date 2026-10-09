#!/usr/bin/env python3
"""CodexAstraLocal: Check CPU stream reservation and final draw command order.

Extract unchanged ring/scheduler/render-pass bodies and actual CPU reservation,
prebind/pass/sample and draw call sites. Driver, queue completion, selection and
query owners are explicit recording models. This is not Vulkan execution, pixel
parity, accelerated/presentation coverage or a complete cached-resource proof.
The default gate requires late-Map and early-Commit defects to fail.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def section(source, start, end):
    require(source.count(start) == 1, "Ambiguous start boundary: " + start)
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def replace_once(source, old, new):
    require(source.count(old) == 1, "Mutation boundary changed: " + old)
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=Path('build/uberhar-probe/vulkan-stream-order'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'sources_before': {},
             'function_seams': [], 'variants': [], 'passed': False}
    inputs = []

    # CodexAstraLocal: Reuse the existing balanced extractor, snapshot every
    # input, and fail on changed boundaries instead of compiling a copied fix.
    def read(rel):
        path = root / rel
        inputs.append(path)
        proof['sources_before'][rel] = digest(path)
        target = output / 'source' / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        return path.read_text()

    try:
        helper_rel = 'tools/uberhar/test_ready_cpu_fragments.py'
        read(helper_rel)
        spec = importlib.util.spec_from_file_location('stream_order_extraction', root / helper_rel)
        extractor = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(extractor)
        parts = []
        for rel, signatures in [
            ('src/video_core/renderer_vulkan/vk_stream_buffer.cpp', [
                'std::tuple<u8*, u32, bool> StreamBuffer::Map(',
                'void StreamBuffer::Commit(', 'void StreamBuffer::ReserveWatches(',
                'void StreamBuffer::WaitPendingOperations(']),
            ('src/video_core/renderer_vulkan/vk_scheduler.cpp', [
                'void Scheduler::Flush(', 'void Scheduler::Wait(u64 tick)',
                'void Scheduler::DispatchWork(', 'void Scheduler::SubmitExecution(',
                'void Scheduler::AllocateWorkerCommandBuffers()']),
            ('src/video_core/renderer_vulkan/vk_render_manager.cpp', [
                'void RenderManager::BeginRendering(const Framebuffer*',
                'void RenderManager::BeginRendering(const RenderPass&',
                'void RenderManager::EndRendering()'])]:
            source = read(rel)
            for signature in signatures:
                body = extractor.function(source, signature)
                parts.append(body)
                proof['function_seams'].append({'source': rel, 'signature': signature,
                    'sha256': hashlib.sha256(body.encode()).hexdigest()})
        (output / 'production.inc').write_text('\n\n'.join(parts) + '\n')

        header = read('src/video_core/renderer_vulkan/vk_render_manager.h')
        renderpass = section(header, 'struct RenderPass {', '\n};') + '\n};'
        (output / 'renderpass.inc').write_text(renderpass)
        pipeline = read('src/video_core/renderer_vulkan/vk_pipeline_cache.cpp')
        bind = section(pipeline, '        if (is_dirty || bound_pipeline != selected) {',
                       '        // AstraEH: Invalidate even')
        (output / 'bind-commands.inc').write_text(bind)
        rasterizer = read('src/video_core/renderer_vulkan/vk_rasterizer.cpp')
        draw = extractor.function(rasterizer, 'bool RasterizerVulkan::Draw(')
        prepare = section(draw, '    // CodexAstraLocal: Map may submit the current tick',
                          '    const auto draw_rect = fb_helper.DrawRect();')
        setup = section(draw, '    PipelineCache::CpuFragmentToken cpu_fragment_use{};',
                        '    // AstraPro: Sample synchronized state')
        tail = section(draw, '        // AstraEH: A ready bridge bypasses creation', '\n    }')
        end_sample = section(draw, '    // AstraEH: This sample covers the native draw',
                             '    // AstraPro: No draw occurred')
        require(prepare.count('stream_buffer.Map(') == 1 and
                'stream_buffer.Map(' not in setup + tail and
                'stream_buffer.Commit(' not in prepare and
                tail.count('stream_buffer.Commit(') == 1,
                'CPU Map/Commit source population changed')
        require(draw.index(prepare) < draw.index('compute_rect->ReserveSample(false,') < draw.index(setup),
                'CPU reservation must precede query reservation and final setup')
        require(tail.index('pipeline_cache.BindPipeline(') < tail.index('stream_buffer.Commit(') <
                tail.index('scheduler.Record(') < tail.index('pipeline_cache.CompleteReadyCpuDraw('),
                'CPU final binding/storage/draw/token order changed')
        (output / 'setup.inc').write_text(setup)
        (output / 'end-sample.inc').write_text(end_sample)

        # CodexAstraLocal: Mutate only the actual reservation/commit seams.
        # The first defect restores the old ordering; the second loses the
        # final allocation tick when pass setup submits after reservation.
        map_block = extractor.function(prepare, '    if (!accelerate)')
        copy_commit = ('        std::memcpy(vertex_data, vertex_batch.data(), vertex_size);\n'
                       '        stream_buffer.Commit(vertex_size);\n')
        require(tail.count(copy_commit) == 1, 'CPU copy/commit boundary changed')
        generated = {
            'candidate-prepare.inc': prepare,
            'candidate-tail.inc': tail,
            'late-prepare.inc': replace_once(prepare, map_block, ''),
            'late-tail.inc': replace_once(tail, '        // CodexAstraLocal: A pass change',
                                          map_block + '\n        // CodexAstraLocal: A pass change'),
            'early-prepare.inc': prepare + '\n' + copy_commit,
            'early-tail.inc': replace_once(tail, copy_commit, ''),
        }
        for name, body in generated.items():
            (output / name).write_text(body)
        # CodexAstraLocal: Header/owner snapshots bind the modeled interface
        # contract without claiming those modeled constructors were executed.
        for rel in ['src/video_core/renderer_vulkan/vk_stream_buffer.h',
                    'src/video_core/renderer_vulkan/vk_scheduler.h',
                    'src/video_core/renderer_vulkan/vk_master_semaphore.h',
                    'src/video_core/renderer_vulkan/vk_master_semaphore.cpp',
                    'src/video_core/renderer_vulkan/vk_compute_rect.cpp']:
            read(rel)
        fixture_rel = 'tools/uberhar/test_vulkan_stream_order.cpp'
        fixture = read(fixture_rel)
        read('tools/uberhar/test_vulkan_stream_order.py')
        (output / 'fixture.cpp').write_text(fixture)
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        require(bool(compiler), 'Empty CXX command')
        binary = output / 'test'
        command = compiler + ['-std=c++20', '-O2', '-g0', '-DNDEBUG', '-pthread',
                              str(output / 'fixture.cpp'), '-o', str(binary)]
        proof['compile_argv'] = command
        compiled = subprocess.run(command, cwd=root, capture_output=True, timeout=60)
        (output / 'compile.stdout').write_bytes(compiled.stdout)
        (output / 'compile.stderr').write_bytes(compiled.stderr)
        proof['compile_exit'] = compiled.returncode
        require(compiled.returncode == 0, 'Recording fixture compile failed: ' + str(output))
        proof['binary_sha256'] = digest(binary)
        for name, number, expected in [
                ('baseline', 0, None), ('late-map', 1, b'FAIL final owner state ready\n'),
                ('early-commit', 2, b'FAIL geometry watch stamped at final draw tick\n')]:
            result = subprocess.run([str(binary), str(number)], cwd=root,
                                    capture_output=True, timeout=20)
            (output / (name + '.stdout')).write_bytes(result.stdout)
            (output / (name + '.stderr')).write_bytes(result.stderr)
            record = {'name': name, 'exit': result.returncode}
            proof['variants'].append(record)
            if expected is None:
                require(result.returncode == 0 and result.stderr == b'' and
                        result.stdout.endswith(b'PASS cases=24 checks=768\n') and
                        len(re.findall(rb'^CASE ', result.stdout, re.M)) == 24,
                        'Recording control population or result changed')
                record.update({'cases': 24, 'checks': 768})
            else:
                require(result.returncode == 1 and result.stderr == expected,
                        'Defect did not fail the intended obligation: ' + name)
            record['passed'] = True
        proof['passed'] = True
    finally:
        proof['sources_after'] = {str(path.relative_to(root)): digest(path) for path in inputs}
        proof['sources_unchanged'] = proof['sources_before'] == proof['sources_after']
        proof['artifacts'] = {str(path.relative_to(output)): digest(path)
                              for path in output.rglob('*') if path.is_file()}
        (output / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print('Proof: ' + str(output), flush=True)
    require(proof['sources_unchanged'], 'Recording inputs changed during the gate')
    print('PASS: 24 CPU recording cases, 768 checks and two intended ordering/tick defects', flush=True)


if __name__ == '__main__':
    main()

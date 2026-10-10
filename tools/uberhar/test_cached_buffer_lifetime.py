#!/usr/bin/env python3
"""CodexAstraLocal: Preserve cached UBO/LUT contents through their final consuming draw."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(value, reason):
    if not value:
        raise RuntimeError(reason)


def definition(source, signature):
    # CodexAstraLocal: Extract the actual balanced production body, failing
    # closed if a changed overload or extraction seam makes it ambiguous.
    require(source.count(signature) == 1, 'ambiguous definition: ' + signature)
    start = source.index(signature)
    begin = source.index('{', start)
    depth = 1
    at = begin + 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at]


def replace_once(source, old, new):
    require(source.count(old) == 1, 'changed defect seam: ' + old[:100])
    return source.replace(old, new, 1)


def run(argv, cwd, stem, timeout):
    result = subprocess.run(argv, cwd=cwd, capture_output=True, timeout=timeout)
    stem.with_suffix('.stdout').write_bytes(result.stdout)
    stem.with_suffix('.stderr').write_bytes(result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/uberhar-probe/cached-buffer-lifetime')
    args = parser.parse_args()
    repo = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    proof = {'author': 'CodexAstraLocal', 'status': 'INCOMPLETE', 'inputs': {}, 'variants': []}

    def read(relative):
        path = repo / relative
        proof['inputs'][str(path)] = digest(path)
        copied = output / 'sources' / relative
        copied.parent.mkdir(parents=True, exist_ok=True)
        copied.write_bytes(path.read_bytes())
        return path.read_text()

    try:
        read('tools/uberhar/test_cached_buffer_lifetime.py')
        fixture = read('tools/uberhar/test_cached_buffer_lifetime.cpp')
        stream = read('src/video_core/renderer_vulkan/vk_stream_buffer.cpp')
        read('src/video_core/renderer_vulkan/vk_stream_buffer.h')
        raster = read('src/video_core/renderer_vulkan/vk_rasterizer.cpp')
        read('src/video_core/renderer_vulkan/vk_rasterizer.h')
        # CodexAstraLocal: Link the real ShaderSetup lifecycle and uniform
        # conversion; dead-section elimination excludes unrelated runtime paths.
        read('src/video_core/pica/shader_setup.cpp')
        read('src/video_core/shader/generator/shader_uniforms.cpp')
        ring = '\n\n'.join(definition(stream, sig) for sig in (
            'std::tuple<u8*, u32, bool> StreamBuffer::Map(',
            'void StreamBuffer::Commit(', 'void StreamBuffer::MarkDrawUse(',
            'void StreamBuffer::ReserveWatches(', 'void StreamBuffer::WaitPendingOperations('))
        uploads = '\n\n'.join(definition(raster, sig) for sig in (
            'void RasterizerVulkan::SyncAndUploadLUTs()',
            'void RasterizerVulkan::SyncAndUploadLUTsLF()',
            'void RasterizerVulkan::UploadUniforms(',
            'void RasterizerVulkan::MarkCachedShaderBuffersUsed()'))
        # CodexAstraLocal: Successful-return alone is not a draw: ordinary
        # accelerated pending pipelines may return true. Bind stamps to every
        # actual enqueue, including capture and CPU/deferred, and no earlier gate.
        accelerate = definition(raster, 'bool RasterizerVulkan::AccelerateDrawBatchInternal(')
        draw = definition(raster, 'bool RasterizerVulkan::Draw(')
        require(accelerate.count('MarkCachedShaderBuffersUsed();') == 2, 'accelerated stamp population')
        require(draw.count('MarkCachedShaderBuffersUsed();') == 1, 'CPU/deferred stamp population')
        capture = accelerate.index('scheduler.Record([draw, evidence]')
        ordinary = accelerate.index('scheduler.Record(draw);')
        require(capture < accelerate.index('MarkCachedShaderBuffersUsed();') < ordinary,
                'capture draw stamp after enqueue')
        require(ordinary < accelerate.rindex('MarkCachedShaderBuffersUsed();'), 'ordinary accelerated stamp after enqueue')
        require(accelerate.index('return !ready_vertex_attempt;') < capture, 'pending pipeline refuses before stamps')
        require(draw.rindex('cmdbuf.draw(') < draw.index('MarkCachedShaderBuffersUsed();'),
                'CPU/deferred stamp after both real enqueues')
        require(draw.index('if (!framebuffer->Handle())') < draw.index('MarkCachedShaderBuffersUsed();'),
                'no-target return before stamp')
        proof['scope'] = ('Actual ring/upload definitions with real PICA data; controlled scheduler reads exact '
                          'cached bytes. Source-bound draw call sites; no GPU execution or title claim.')
        proof['seams'] = {'ring': hashlib.sha256(ring.encode()).hexdigest(),
                          'uploads': hashlib.sha256(uploads.encode()).hexdigest(),
                          'accelerate': hashlib.sha256(accelerate.encode()).hexdigest(),
                          'draw': hashlib.sha256(draw.encode()).hexdigest()}
        stub = output / 'stub/video_core/renderer_vulkan/vk_common.h'
        stub.parent.mkdir(parents=True)
        stub.write_text('''// CodexAstraLocal: Controlled Vulkan visibility endpoints, no device access.
#pragma once
#include "common/common_types.h"
#define VK_NULL_HANDLE 0
namespace vk {
using Buffer=u64; using DeviceMemory=u64; using BufferUsageFlags=u32;
struct MappedMemoryRange { DeviceMemory memory; u64 offset, size; };
struct Device {
 void invalidateMappedMemoryRanges(MappedMemoryRange) {}
 void flushMappedMemoryRanges(MappedMemoryRange) {}
};
}
''')
        variants = [('candidate', ring, uploads, None)]
        # CodexAstraLocal: Every negative compiles and must expose the intended
        # byte mismatch, not a generic crash/timeout or a direct counter assertion.
        variants.append(('omit-last-use', ring, replace_once(uploads,
            '    uniform_buffer.MarkDrawUse();\n    texture_buffer.MarkDrawUse();\n    texture_lf_buffer.MarkDrawUse();', ''),
            b'FAIL queued cached draw bytes overwritten\n'))
        variants.append(('stamp-before-final-flush', replace_once(ring,
            'last_draw_use_tick = scheduler.CurrentTick();',
            'last_draw_use_tick = scheduler.CurrentTick() - 1;'), uploads,
            b'FAIL queued cached draw bytes overwritten\n'))
        variants.append(('omit-wrap-wait', replace_once(ring,
            'scheduler.Wait(last_draw_use_tick);', '(void)last_draw_use_tick;'), uploads,
            b'FAIL queued cached draw bytes overwritten\n'))
        uniform = definition(uploads, 'void RasterizerVulkan::UploadUniforms(')
        variants.append(('omit-uniform-refresh', ring, replace_once(uploads, uniform,
            uniform.replace(' || invalidate', '')), b'FAIL all cached blocks refreshed at wrap\n'))
        for name, sig in [('proctex', 'void RasterizerVulkan::SyncAndUploadLUTs()'),
                          ('lighting-fog', 'void RasterizerVulkan::SyncAndUploadLUTsLF()')]:
            body = definition(uploads, sig)
            variants.append(('omit-' + name + '-refresh', ring, replace_once(uploads, body,
                replace_once(body, 'if (invalidate)', 'if (false)')),
                b'FAIL all cached blocks refreshed at wrap\n'))
        for label, ring_code, upload_code, expected_error in variants:
            case = output / label
            case.mkdir()
            (case / 'ring.inc').write_text(ring_code + '\n')
            (case / 'uploads.inc').write_text(upload_code + '\n')
            (case / 'fixture.cpp').write_text(fixture)
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-pthread',
                       '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                       '-DFMT_HEADER_ONLY', '-I' + str(output / 'stub'),
                       '-I' + str(output / 'sources/src'), '-I' + str(repo / 'src'),
                       '-I' + str(repo / 'externals/fmt/include'), '-I' + str(repo / 'externals/boost'),
                       '-I' + str(repo / 'externals/nihstro/include'),
                       '-I' + str(repo / 'externals/xxHash'),
                       '-MMD', '-MF', str(case / 'dependencies.d'), str(case / 'fixture.cpp'),
                       str(repo / 'src/video_core/pica/shader_setup.cpp'),
                       str(repo / 'src/video_core/shader/generator/shader_uniforms.cpp'),
                       '-o', str(case / 'fixture')]
            built = run(command, repo, case / 'compile', 120)
            require(built.returncode == 0, 'compile failed: ' + str(case) + '\n' + built.stderr.decode()[-6000:])
            result = run([str(case / 'fixture')], repo, case / 'run', 20)
            if expected_error is None:
                require(result.returncode == 0 and result.stdout.startswith(b'PASS cached lifetime cases=12 checks='),
                        'candidate failed: ' + str(case) + '\n' + result.stderr.decode())
            else:
                require(result.returncode == 23 and result.stderr == expected_error,
                        'wrong defect failure: ' + label + '\n' + result.stderr.decode())
            proof['variants'].append({'name': label, 'compile_argv': command,
                                      'returncode': result.returncode, 'expected_error':
                                      expected_error.decode() if expected_error else None})
            print(label + ': PASS', flush=True)
        proof['status'] = 'PASS'
    finally:
        proof['inputs_after'] = {path: digest(Path(path)) for path in proof['inputs']}
        proof['unchanged'] = proof['inputs_after'] == proof['inputs']
        proof['artifacts'] = {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
        (output / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print('Proof: ' + str(output), flush=True)
    require(proof['unchanged'], 'source changed during gate')


if __name__ == '__main__':
    main()

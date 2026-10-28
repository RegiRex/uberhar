#!/usr/bin/env python3
"""CodexAstraLocal: Finite production-extraction pass metadata/order fixture.

Recording endpoints model queued binding/draw, timestamp availability and GPU tick
retirement. No Vulkan driver, device performance or whole-frame output is tested.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from test_ready_cpu_fragments import function, replace_once

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/gpu-pass-attribution')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='attribution-', dir=args.output.resolve()))
    proof = {'scope': __doc__, 'inputs': {}, 'variants': [], 'passed': False}

    def read(path):
        path = path.resolve()
        content = path.read_bytes()
        proof['inputs'][str(path)] = hashlib.sha256(content).hexdigest()
        return content.decode()

    try:
        read(Path(__file__))
        read(Path(__file__).with_name('test_ready_cpu_fragments.py'))
        source = args.source_root / 'src/video_core/renderer_vulkan'
        cpp = read(source / 'vk_render_manager.cpp')
        header = read(source / 'vk_render_manager.h')
        pipeline = read(source / 'vk_pipeline_cache.h')
        fixture = read(Path(__file__).with_suffix('.cpp'))
        production = '\n\n'.join(function(cpp, sig) for sig in [
            'void RenderManager::BeginDiagnosticPass(', 'void RenderManager::EndDiagnosticPass(',
            'void RenderManager::RecordDiagnosticDraw(', 'void RenderManager::PollDiagnostics('])
        (output / 'fixture.cpp').write_text(fixture)
        (output / 'diagnostic-fields.inc').write_text(
            header[header.index('    struct PassSample {'):header.index('\n};', header.index('    struct PassSample {'))])
        (output / 'reason.inc').write_text(function(header, 'enum class PassEndReason') + ';\n')
        (output / 'pipeline-accessor.inc').write_text(function(pipeline, '    const GraphicsPipeline* GetBoundDiagnosticPipeline()'))
        variants = [
            ('candidate', production, None),
            ('requested_identity', replace_once(production,
                'pipelines.GetBoundDiagnosticPipeline()', 'pipelines.requested_pipeline'),
                'FAIL fulfilled pipeline wins requested identity\n'),
            ('endpoint_only_mixed', replace_once(production,
                'draw.shader_changes += draw.last_shaders != shaders;',
                'draw.shader_changes = draw.first_shaders != shaders;'),
                'FAIL ABA retains mixed changes\n'),
            ('missing_publication', replace_once(production,
                ' ||\n                    !diagnostic_published[i].load(std::memory_order_acquire)', ''),
                'FAIL unpublished worker metadata not consumed\n'),
        ]
        for name, body, expected_failure in variants:
            (output / 'production.inc').write_text(body)
            (output / (name + '.inc')).write_text(body)
            command = ['c++', '-std=c++20', '-O2', '-pthread', str(output / 'fixture.cpp'),
                       '-o', str(output / name)]
            built = subprocess.run(command, capture_output=True, timeout=60)
            (output / (name + '.compile.stdout')).write_bytes(built.stdout)
            (output / (name + '.compile.stderr')).write_bytes(built.stderr)
            row = {'name': name, 'compile_exit': built.returncode, 'command': command}
            proof['variants'].append(row)
            # CodexAstraLocal: A failed compile is always a failed gate, including
            # negative controls; only the expected runtime assertion is success.
            if built.returncode:
                raise RuntimeError(f'{name}: compile failed; see {output}')
            run = subprocess.run([str(output / name)], capture_output=True, timeout=10)
            (output / (name + '.stdout')).write_bytes(run.stdout)
            (output / (name + '.stderr')).write_bytes(run.stderr)
            row['runtime_exit'] = run.returncode
            if expected_failure is None:
                ok = run.returncode == 0 and run.stdout.startswith(b'PASS GPU pass attribution checks=')
            else:
                ok = run.returncode == 1 and run.stderr.decode() == expected_failure
            if not ok:
                raise RuntimeError(f'{name}: unexpected runtime outcome; see {output}')
            print(f'{name}: PASS', flush=True)
        proof['passed'] = True
    finally:
        proof['inputs_after'] = {path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                                 for path in proof['inputs']}
        proof['unchanged'] = proof['inputs'] == proof['inputs_after']
        (output / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print('Proof: ' + str(output), flush=True)
    if not proof['unchanged']:
        raise RuntimeError('Source changed during finite check')


if __name__ == '__main__':
    main()

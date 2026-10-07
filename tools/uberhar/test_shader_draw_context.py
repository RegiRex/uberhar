#!/usr/bin/env python3
"""CodexAstraLocal: Verify real draw binding/FIFO semantics with profiler off/on.

This synthetic host regression uses real JIT/interpreter/transport/assembly and
extracted PicaCore binding/diagnostic call sites. File-provider/renderer behavior,
profiler storage and target-device execution are outside its scope.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/shader-draw-context'))
    parser.add_argument('--source-root', type=Path)
    args = parser.parse_args()
    repo = next(parent for parent in Path(__file__).resolve().parents if (parent / '.git').exists())
    source_root = (args.source_root or repo).resolve()
    target = args.output.resolve()
    target.mkdir(parents=True, exist_ok=True)
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('This host gate executes the production x64 JIT; ARM is covered by Android compilation separately')

    def source(name):
        proposed = source_root / name
        return proposed if proposed.exists() else repo / name

    # CodexAstraLocal: Suppress only compile diagnostics settings; no shader or
    # factory code is modeled. The test explicitly selects real engine types.
    stubs = target / 'stubs/common'
    stubs.mkdir(parents=True, exist_ok=True)
    (stubs / 'settings.h').write_text('''// CodexAstraLocal: Standalone logging policy only.
#pragma once
namespace Settings {
enum class UberharTestMode { Custom };
struct Setting {UberharTestMode GetValue()const{return UberharTestMode::Custom;}};
inline struct {Setting uberhar_test_mode;} values;
}
''')
    pica = source('src/video_core/pica/pica_core.cpp').read_text()
    begin = '            const auto context = pipeline.num_vertices\n'
    end = '            const auto run_inherited = '
    assert pica.count(begin) == 1 and pica.count(end) == 1
    binding = pica[pica.index(begin):pica.index(end, pica.index(begin))]
    begin = '            using DiagnosticCall = void (*)'
    end = '            const auto diagnostic_shade = '
    assert pica.count(begin) == 1 and pica.count(end) == 1
    diagnostic = pica[pica.index(begin):pica.index(end, pica.index(begin))]
    (target / 'draw_context_binding.inc').write_text(binding)
    (target / 'draw_diagnostic_dispatch.inc').write_text(diagnostic)
    sources = [Path(__file__).with_suffix('.cpp').resolve()] + [source(name) for name in (
        'src/video_core/pica/output_vertex.cpp', 'src/video_core/pica/primitive_assembly.cpp',
        'src/video_core/pica/shader_unit.cpp', 'src/video_core/pica/shader_setup.cpp',
        'src/video_core/shader/shader.cpp', 'src/video_core/shader/shader_jit.cpp',
        'src/video_core/shader/shader_interpreter.cpp',
        'src/video_core/shader/shader_jit_x64_compiler.cpp')]
    provenance = {'author': 'CodexAstraLocal', 'scope': __doc__, 'source_sha256': {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
        'pica_source_sha256': hashlib.sha256(source('src/video_core/pica/pica_core.cpp').read_bytes()).hexdigest(),
        'driver_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'variants': []}
    for profiler in (0, 1):
        binary = target / f'test-draw-context-{profiler}'
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-DNDEBUG',
                   '-msse4.1', '-msse4.2', f'-DMICROPROFILE_ENABLED={profiler}',
                   '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL', f'-I{target / "stubs"}',
                   f'-I{source_root / "src"}', f'-I{target}', '-Isrc',
                   '-Iexternals/fmt/include', '-Iexternals/boost', '-Iexternals/xxHash',
                   '-isystem', 'externals/nihstro/include', '-Iexternals/microprofile',
                   '-Iexternals/xbyak'] + [str(path) for path in sources] + ['-o', str(binary)]
        started = time.monotonic()
        compiled = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (target / f'compile-{profiler}.log').write_bytes(compiled.stdout + compiled.stderr)
        record = {'profiler': profiler, 'compile_argv': command, 'compile_exit': compiled.returncode,
                  'compile_seconds': time.monotonic() - started}
        provenance['variants'].append(record)
        (target / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        compiled.check_returncode()
        started = time.monotonic()
        executed = subprocess.run([str(binary)], capture_output=True, timeout=60)
        (target / f'run-{profiler}.log').write_bytes(executed.stdout + executed.stderr)
        output = executed.stdout.decode()
        record.update(run_exit=executed.returncode, run_seconds=time.monotonic() - started,
                      binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        (target / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        executed.check_returncode()
        result = re.fullmatch(r'PASS: (\d+) full-draw comparisons; (\d+) assertions; profiler=(\d) enters=(\d+) leaves=(\d+)\n', output)
        assert result and int(result[1]) == 1920 and int(result[2]) > 10000 and int(result[3]) == profiler
        assert result[4] == result[5] and (int(result[4]) > 0) == bool(profiler)
        print(output.strip(), flush=True)
    print('PASS: actual factory/call-site regression with profiler off and on')


if __name__ == '__main__':
    main()

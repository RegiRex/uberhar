#!/usr/bin/env python3
"""CodexAstraLocal: Bind exact compiled JIT source identity under real hash collisions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/shader-jit-identity'))
    parser.add_argument('--mutants', action='store_true')
    args = parser.parse_args()
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('This gate executes the production x64 JIT')
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    target = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    source = root / 'src/video_core/shader/shader_jit.cpp'
    fixture = Path(__file__).with_suffix('.cpp')
    production = [root / x for x in (
        'src/video_core/pica/shader_unit.cpp', 'src/video_core/pica/shader_setup.cpp',
        'src/video_core/shader/shader_interpreter.cpp',
        'src/video_core/shader/shader_jit_x64_compiler.cpp')]
    inputs = [Path(__file__).resolve(), fixture, source, *production, root / 'src/video_core/shader/shader_jit.h',
              root / 'src/video_core/pica/shader_setup.h', root / 'src/video_core/pica/uberhar_parallel_vertex.h']
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    before = {str(p.relative_to(root)): sha(p) for p in inputs}
    proof = {'author': 'CodexAstraLocal', 'sources_before': before, 'variants': []}
    # CodexAstraLocal: Adapt only the optional diagnostic setting. All artifact
    # selection, source hashing, compilation and execution remain real bodies.
    stubs = target / 'stubs/common'; stubs.mkdir(parents=True)
    (stubs / 'settings.h').write_text('''// CodexAstraLocal: Standalone logging policy only.
#pragma once
namespace Settings {
enum class UberharTestMode { Custom };
struct Setting { UberharTestMode GetValue() const { return UberharTestMode::Custom; } };
inline struct { Setting uberhar_test_mode; } values;
}
''')
    flags = shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++20', '-O2', '-DNDEBUG', '-DMICROPROFILE_ENABLED=0', '-DFMT_HEADER_ONLY',
        '-DXXH_INLINE_ALL', '-msse4.1', '-msse4.2', '-pthread', f'-I{target / "stubs"}',
        '-Isrc', '-Iexternals/fmt/include', '-Iexternals/boost', '-Iexternals/xxHash',
        '-isystem', 'externals/nihstro/include', '-Iexternals/microprofile', '-Iexternals/xbyak']
    originals = {'jit': source.read_text(), 'setup': production[1].read_text()}
    mutants = [
        ('hash_only', 'jit', 'entry.code == setup.GetProgramCode() && entry.swizzles == setup.GetSwizzleData()', 'true'),
        ('ignore_descriptors', 'jit', 'entry.code == setup.GetProgramCode() && entry.swizzles == setup.GetSwizzleData()',
         'entry.code == setup.GetProgramCode()'),
        ('ignore_revision', 'jit', 'binding.revision == revision', 'true'),
        ('reused_address', 'jit', 'setup.cached_shader == binding.entry->shader.get()', 'true'),
        # CodexAstraLocal: Imported host metadata recreates the copy/assignment
        # hole even though the bucket's full-source comparison remains correct.
        ('copy_binding', 'setup', 'cached_shader = nullptr;', 'cached_shader = other.cached_shader;'),
        ('copy_revision', 'setup', '++code_revision;', 'code_revision = other.code_revision;'),
    ] if args.mutants else []
    try:
        objects = []
        for cpp in [fixture, *production]:
            obj = target / (cpp.stem + '.o')
            command = flags + ['-c', str(cpp), '-o', str(obj)]
            result = subprocess.run(command, cwd=root, capture_output=True, timeout=120)
            obj.with_suffix('.compile.log').write_bytes(result.stdout + result.stderr)
            result.check_returncode(); objects.append(obj)
        for name, kind, old, new in [('baseline', 'jit', None, None), *mutants]:
            folder = target / name; folder.mkdir()
            original = originals[kind]
            modified = original
            if old is not None:
                if original.count(old) != 1:
                    raise RuntimeError('Mutation anchor changed: ' + name)
                modified = original.replace(old, new, 1)
            cpp = folder / (source.name if kind == 'jit' else production[1].name)
            cpp.write_text(modified)
            binary = folder / 'test'
            sources = [str(cpp)] if kind == 'jit' else [str(source), str(cpp)]
            linked = objects if kind == 'jit' else [x for x in objects if x.name != 'shader_setup.o']
            command = flags + sources + [*map(str, linked), '-o', str(binary)]
            result = subprocess.run(command, cwd=root, capture_output=True, timeout=120)
            (folder / 'compile.log').write_bytes(result.stdout + result.stderr)
            result.check_returncode()
            result = subprocess.run([str(binary)], capture_output=True, timeout=60)
            output = (result.stdout + result.stderr).decode()
            (folder / 'run.log').write_text(output)
            match = re.fullmatch(r'PASS shader_identity_checks=(\d+)\n', output)
            # CodexAstraLocal: Address reuse is caught before execution because
            # the stale fast hit leaves the newly constructed setup unbound.
            expected_failure = {
                'reused_address': 'bound actual compiled owner',
                'copy_binding': 'copy retained foreign compiled owner',
                'copy_revision': 'copy constructor imported source revision',
            }.get(name, 'compiled artifact differs from current certified source')
            passed = result.returncode == 0 and bool(match) and int(match[1]) > 500 if old is None else \
                result.returncode != 0 and expected_failure in output
            proof['variants'].append({'name': name, 'argv': command, 'exit': result.returncode,
                'source_sha256': sha(cpp), 'binary_sha256': sha(binary), 'output': output, 'passed': passed})
            print(name + ': ' + output.strip(), flush=True)
            if not passed:
                raise RuntimeError('Unexpected outcome: ' + name)
    finally:
        proof['sources_after'] = {str(p.relative_to(root)): sha(p) for p in inputs}
        proof['sources_unchanged'] = before == proof['sources_after']
        (target / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print('Proof: ' + str(target), flush=True)
    if not proof['sources_unchanged']:
        raise RuntimeError('Source changed during proof')


if __name__ == '__main__':
    main()

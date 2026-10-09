#!/usr/bin/env python3
"""CodexAstraLocal: Check actual shader independence and ordered CPU batch composition.

Synthetic shaders run through the production interpreter and x64 JIT. Source
mutants must corrupt actual serial/grain output or floating flags; rejected unsafe
flow is never executed. This is not an A64 execution or guest-memory pinning test.
"""
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
import time


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/parallel-vertex'))
    parser.add_argument('--mutants', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('This gate executes the actual x64 JIT and requires an x64 host')
    args.output.mkdir(parents=True, exist_ok=True)
    target = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    header = root / 'src/video_core/pica/uberhar_parallel_vertex.h'
    fixture = Path(__file__).with_suffix('.cpp')
    production = [root / name for name in (
        'src/video_core/pica/shader_unit.cpp', 'src/video_core/pica/shader_setup.cpp',
        'src/video_core/pica/primitive_assembly.cpp',
        'src/video_core/shader/shader_interpreter.cpp',
        'src/video_core/shader/shader_jit_x64_compiler.cpp')]
    inputs = [Path(__file__).resolve(), header, fixture, *production] + [root / name for name in (
        'src/video_core/pica/shader_unit.h', 'src/video_core/pica/shader_setup.h',
        'src/video_core/pica/primitive_assembly.h', 'src/video_core/pica/uberhar_vertex_input.h',
        'src/video_core/pica/uberhar_vertex_output.h', 'src/video_core/pica/uberhar_vertex_cache.h',
        'src/video_core/pica/uberhar_vertex_parallel_batch.h', 'src/common/uberhar_parallel_work.h',
        'src/video_core/shader/shader_jit_x64_compiler.h',
        'src/video_core/shader/uberhar_interpreter_stack.h',
        'externals/nihstro/include/nihstro/shader_bytecode.h')]
    original = header.read_text()
    fixture_bytes = fixture.read_bytes()
    before = {str(path.relative_to(root)): digest(path) for path in inputs}
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'sources_before': before,
             'objects': [], 'variants': [], 'dependencies': {}}

    # CodexAstraLocal: Compile real production bodies once, retaining dependency
    # files and binaries. Every variant includes its own exact certificate copy;
    # no shader engine, FIFO, worker or assembler is replaced by a model.
    flags = shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++20', '-O2', '-DNDEBUG', '-DMICROPROFILE_ENABLED=0', '-DFMT_HEADER_ONLY',
        '-DXXH_INLINE_ALL', '-msse4.1', '-msse4.2', '-pthread']
    includes = ['-Isrc', '-Iexternals/fmt/include', '-Iexternals/boost', '-Iexternals/xxHash',
                '-isystem', 'externals/nihstro/include', '-Iexternals/microprofile', '-Iexternals/xbyak']

    def save():
        (target / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')

    def compile_one(source, output, local_include=None):
        dependency = output.with_suffix('.d')
        command = flags + ([f'-I{local_include}'] if local_include else []) + includes + [
            '-MMD', '-MF', str(dependency), '-c', str(source), '-o', str(output)]
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=150)
        output.with_suffix('.compile.log').write_bytes(result.stdout + result.stderr)
        record = {'argv': command, 'exit': result.returncode, 'seconds': time.monotonic() - start}
        proof['objects'].append(record)
        save()
        result.check_returncode()
        record['sha256'] = digest(output)
        for name in shlex.split(dependency.read_text().replace('\\\n', ' ').split(':', 1)[1]):
            path = Path(name)
            if not path.is_absolute():
                path = root / path
            proof['dependencies'].setdefault(str(path.resolve()), digest(path))
        save()

    # CodexAstraLocal: Each defect must reach a behavioral mismatch, rather than
    # merely failing compilation or an expected-admission assertion. Masked-lane
    # removal specifically exposes SIMD exception flags with unchanged outputs.
    mutants = [
        ('merge_union', 'to.temporary[reg] &= from.temporary[reg];',
         'to.temporary[reg] |= from.temporary[reg];'),
        ('temporary_read', 'node.access.reads.temporary[reg] & possible.temporary[reg] & ~state.temporary[reg]', '0'),
        ('address_read', 'node.access.reads.addresses & possible.addresses & ~state.addresses', '0'),
        ('condition_read', 'node.access.reads.conditions & possible.conditions & ~state.conditions', '0'),
        ('output_read', 'possible.output[reg] & ~state.output[reg]', '0'),
        ('uniform_snapshot', '(uniform_bools & (1U << instruction.flow_control.bool_uniform_id)) != 0',
         '(uniform_bools & (1U << instruction.flow_control.bool_uniform_id)) == 0'),
        ('false_alternative', 'const u32 alternatives = condition ? 2 : 1;', 'const u32 alternatives = 1;'),
        ('possible_write', 'Union(possible, nodes[at].access.writes);', ''),
        ('discarded_lanes', 'const u8 consumed = 15;', 'const u8 consumed = enabled;'),
    ] if args.mutants else []
    try:
        objects = []
        for source in production:
            output = target / (source.stem + '.o')
            compile_one(source, output)
            objects.append(output)
        for name, old, new in [('baseline', None, None), *mutants]:
            variant = target / name
            variant.mkdir()
            include = variant / 'include/video_core/pica'
            include.mkdir(parents=True)
            modified = original
            if old is not None:
                if original.count(old) != 1:
                    raise RuntimeError(f'Nonunique mutation anchor: {name}')
                modified = original.replace(old, new, 1)
            (include / header.name).write_text(modified)
            copied = variant / fixture.name
            copied.write_bytes(fixture_bytes)
            obj, binary = variant / 'fixture.o', variant / 'test'
            compile_one(copied, obj, variant / 'include')
            command = flags + [str(obj), *map(str, objects), '-o', str(binary)]
            linked = subprocess.run(command, cwd=root, capture_output=True, timeout=60)
            (variant / 'link.log').write_bytes(linked.stdout + linked.stderr)
            linked.check_returncode()
            start = time.monotonic()
            result = subprocess.run([str(binary)], capture_output=True, timeout=90)
            output = (result.stdout + result.stderr).decode()
            (variant / 'run.log').write_text(output)
            if old is None:
                match = re.fullmatch(r'PASS cases=(\d+) accepted=(\d+) rejected=(\d+) comparisons=(\d+) checks=(\d+) composed_draws=(\d+) certificate_bytes=(\d+)\n', output)
                passed = result.returncode == 0 and bool(match) and int(match[1]) >= 580 and \
                    int(match[4]) >= 290000 and int(match[6]) == 120
            else:
                passed = result.returncode != 0 and ('accepted certificate changes serial carry output' in output or
                                                   'accepted certificate changes serial carry FP flags' in output)
            proof['variants'].append({'name': name, 'link_argv': command,
                'execute_exit': result.returncode, 'seconds': time.monotonic() - start,
                'binary_sha256': digest(binary), 'header_sha256': digest(include / header.name),
                'expected_failure': old is not None, 'passed': passed, 'output': output})
            save()
            print(name + ': ' + output.strip(), flush=True)
            if not passed:
                raise RuntimeError(f'Unexpected test outcome: {name}; {target}')
    finally:
        proof['sources_after'] = {str(path.relative_to(root)): digest(path) for path in inputs}
        proof['source_unchanged'] = proof['sources_before'] == proof['sources_after']
        proof['dependencies_unchanged'] = all(digest(path) == value for path, value in proof['dependencies'].items())
        save()
        print(f'Proof: {target}', flush=True)
    if not proof['source_unchanged'] or not proof['dependencies_unchanged']:
        raise RuntimeError('Source changed during proof; retain this run and validate the stable generation')


if __name__ == '__main__':
    main()

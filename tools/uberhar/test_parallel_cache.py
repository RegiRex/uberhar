#!/usr/bin/env python3
"""CodexAstraLocal: Execute production proof-cache invalidation and negative controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutants', action='store_true')
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/parallel-cache'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    paths = [Path(__file__).resolve(), Path(__file__).with_suffix('.cpp'),
             root / 'src/video_core/pica/pica_core.cpp',
             root / 'src/video_core/pica/shader_setup.h',
             root / 'src/video_core/pica/shader_setup.cpp',
             root / 'src/video_core/pica/uberhar_parallel_vertex.h']
    def hashes():
        return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    proof = {'author': 'CodexAstraLocal', 'sources_before': hashes(), 'variants': []}
    pica, setup = paths[2].read_text(), paths[3].read_text()
    start, end = 'struct PicaCore::ParallelVertexState {', '\nPicaCore::PicaCore('
    if pica.count(start) != 1 or pica.count(end) != 1:
        raise RuntimeError('Production cache extraction boundary changed')
    cache = pica[pica.index(start):pica.index(end)].replace(start, 'struct ParallelVertexState {', 1)
    # CodexAstraLocal: Defects must compile and reach the fresh-certificate oracle
    # or actual restore/safety assertions. Source checks alone are not evidence.
    mutations = [
        ('revision', 'cache', 'if (!current || revision != setup.GetCodeRevision())', 'if (!current)'),
        ('collision', 'cache', 'candidate.code == setup.GetProgramCode() &&\n                    candidate.swizzles == setup.GetSwizzleData()', 'true'),
        ('booleans', 'cache', 'ParallelVertexBooleanUniforms(setup.uniforms)', '0'),
        ('entry', 'cache', 'const Key key{setup.entry_point,', 'const Key key{0,'),
        ('outputs', 'cache', 'setup.entry_point, output_mask,', 'setup.entry_point, 1,'),
        # CodexAstraLocal: A repeated identical source revision still needs two
        # independent domain keys; omitting this field must corrupt admission.
        ('contract', 'cache', 'ParallelVertexBooleanUniforms(setup.uniforms), contract}',
         'ParallelVertexBooleanUniforms(setup.uniforms), ParallelVertexContract::FullArithmeticReads}'),
        ('restore', 'setup', '++code_revision;\n            uniforms_dirty = true;', 'uniforms_dirty = true;'),
    ] if args.mutants else []
    flags = shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++20', '-O2', '-pthread', '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL']
    try:
        for name, source, old, new in [('baseline', '', '', ''), *mutations]:
            variant = output / name
            include = variant / 'include/video_core/pica'
            include.mkdir(parents=True)
            texts = {'cache': cache, 'setup': setup}
            if source:
                if texts[source].count(old) != 1:
                    raise RuntimeError(f'Nonunique mutation {name}')
                texts[source] = texts[source].replace(old, new, 1)
            (variant / 'parallel_cache.inc').write_text(texts['cache'])
            (include / 'shader_setup.h').write_text(texts['setup'])
            binary = variant / 'test'
            command = flags + [f'-I{variant / "include"}', f'-I{variant}', '-Isrc',
                '-Iexternals/fmt/include', '-Iexternals/boost', '-Iexternals/xxHash',
                '-Iexternals/nihstro/include', str(paths[1]), str(paths[4]), '-o', str(binary)]
            compiled = subprocess.run(command, cwd=root, capture_output=True, timeout=120)
            (variant / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
            compiled.check_returncode()
            run = subprocess.run([str(binary)], capture_output=True, timeout=30)
            text = (run.stdout + run.stderr).decode()
            (variant / 'run.log').write_text(text)
            passed = (run.returncode == 0 and 'parallel cache PASS:' in text) if not source else (
                run.returncode != 0 and any(message in text for message in (
                    'cached certificate differs', 'restore retained host revision')))
            proof['variants'].append({'name': name, 'argv': command, 'exit': run.returncode,
                                      'output': text, 'passed': passed})
            print(f'{name}: {"PASS" if passed else "FAIL"}', flush=True)
            if not passed:
                raise RuntimeError(f'Unexpected test outcome: {name}; {variant}')
    finally:
        proof['sources_after'] = hashes()
        proof['sources_unchanged'] = proof['sources_before'] == proof['sources_after']
        (output / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(f'Proof: {output}', flush=True)
    if not proof['sources_unchanged']:
        raise RuntimeError('Production source changed during test')


if __name__ == '__main__':
    main()

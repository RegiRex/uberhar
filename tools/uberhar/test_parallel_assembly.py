#!/usr/bin/env python3
"""CodexAstraLocal: Check ordered assembly state against actual scalar submission.

The default gate includes both behavior-sensitive production-header defects.
No private captures, timing workload or device dependencies are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def replace_once(source, old, new):
    require(source.count(old) == 1, 'Production mutation boundary changed')
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/parallel-assembly'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    header = repo / 'src/video_core/pica/primitive_assembly.h'
    body = header.with_suffix('.cpp')
    fixture = Path(__file__).with_suffix('.cpp')
    inputs = [header, body, fixture, Path(__file__).resolve()]
    hashes = {str(p.relative_to(repo)): digest(p) for p in inputs}
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'sources_before': hashes,
             'variants': [], 'dependencies': {}}
    for path in inputs:
        target = output / 'source' / path.relative_to(repo)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())

    # CodexAstraLocal: The baseline compiles the actual header/body. Each defect
    # changes one owned semantic obligation in a child include shadow and must
    # fail retained state equality, not merely compile or crash unsuccessfully.
    original = header.read_text()
    variants = {
        'baseline': original,
        'missing_final_pair': replace_once(original,
            '                buffer[0] = *last_first;\n                buffer[1] = *last_second;',
            '                (void)last_second;'),
        'premature_winding_clear': replace_once(original,
            '                    handler(second, first, third);\n                    winding = false;',
            '                    winding = false;\n                    handler(second, first, third);'),
    }
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    require(bool(compiler), 'Empty CXX command')
    try:
        for name, source in variants.items():
            directory = output / name
            shadow = directory / 'video_core/pica/primitive_assembly.h'
            shadow.parent.mkdir(parents=True)
            shadow.write_text(source)
            binary = directory / 'test'
            dependencies = directory / 'fixture.d'
            command = compiler + ['-std=c++20', '-O2', '-DNDEBUG', '-DFMT_HEADER_ONLY',
                '-I' + str(directory), '-I' + str(repo / 'src'),
                '-I' + str(repo / 'externals/fmt/include'),
                '-I' + str(repo / 'externals/boost'),
                '-I' + str(repo / 'externals/nihstro/include'),
                '-MMD', '-MF', str(dependencies), str(fixture), str(body), '-o', str(binary)]
            record = {'name': name, 'argv': command, 'header_sha256': digest(shadow)}
            proof['variants'].append(record)
            compiled = subprocess.run(command, cwd=repo, capture_output=True, timeout=60)
            (directory / 'compile.stdout').write_bytes(compiled.stdout)
            (directory / 'compile.stderr').write_bytes(compiled.stderr)
            record['compile_exit'] = compiled.returncode
            require(compiled.returncode == 0, 'Compile failed: ' + str(directory))
            run = subprocess.run([str(binary)], cwd=repo, capture_output=True, timeout=20)
            (directory / 'run.stdout').write_bytes(run.stdout)
            (directory / 'run.stderr').write_bytes(run.stderr)
            record.update({'exit': run.returncode, 'binary_sha256': digest(binary)})
            if name == 'baseline':
                require(run.returncode == 0 and run.stderr == b'', 'Assembly baseline failed')
                result = json.loads(run.stdout)
                require(result == {'cases': 504, 'partitions': 36, 'checks': 3636},
                        'Assembly fixture population changed')
                record['result'] = result
            else:
                require(run.returncode == 1 and run.stdout == b'' and
                        run.stderr == b'serialized/raw retained state\n',
                        'Defect did not expose intended state fault: ' + name)
            record['passed'] = True
            # CodexAstraLocal: Bind actual transitive headers used by the real
            # assembler body; overlapping dependencies must remain unchanged.
            text = dependencies.read_text().replace('\\\n', ' ').split(':', 1)[1]
            for entry in shlex.split(text):
                path = Path(entry)
                path = path if path.is_absolute() else repo / path
                path = path.resolve()
                value = digest(path)
                require(proof['dependencies'].get(str(path), value) == value,
                        'Dependency changed across variants: ' + str(path))
                proof['dependencies'][str(path)] = value
            print(name + ': PASS', flush=True)
        proof['passed'] = True
    finally:
        proof['sources_after'] = {str(p.relative_to(repo)): digest(p) for p in inputs}
        proof['sources_unchanged'] = proof['sources_before'] == proof['sources_after']
        proof['artifacts'] = {str(p.relative_to(output)): digest(p)
                              for p in output.rglob('*') if p.is_file()}
        (output / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print('Proof: ' + str(output), flush=True)
    require(proof['sources_unchanged'], 'Assembly source changed during test')
    print('PASS: 504 topology/tail/winding/throw cases, 36 persistent partitions, '
          '3636 checks and two intended state defects', flush=True)


if __name__ == '__main__':
    main()

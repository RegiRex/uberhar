#!/usr/bin/env python3
"""CodexAstraLocal: Run one actual codec comparison with retained source provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    inputs = [Path(__file__).resolve(), Path(__file__).with_suffix('.cpp')] + [root / p for p in [
        'src/video_core/rasterizer_cache/texture_decode_work.h',
        'src/video_core/rasterizer_cache/texture_codec.h',
        'src/video_core/rasterizer_cache/utils.cpp',
        'src/video_core/texture/etc1.cpp', 'src/common/uberhar_parallel_work.h']]
    proof = {'author': 'CodexAstraLocal', 'inputs': {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    command = ['c++', '-std=c++20', '-O2', '-DNDEBUG', '-DFMT_HEADER_ONLY', '-pthread',
               '-Isrc', '-Iexternals/fmt/include', '-Iexternals/boost',
               str(Path(__file__).with_suffix('.cpp')),
               'src/video_core/rasterizer_cache/utils.cpp', 'src/video_core/texture/etc1.cpp',
               '-o', str(out / 'parallel-texture')]
    try:
        for name, cmd in [('compile', command), ('run', [str(out / 'parallel-texture')])]:
            result = subprocess.run(cmd, cwd=root, capture_output=True, timeout=90)
            (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
            proof[name] = {'command': cmd, 'exit': result.returncode}
            print(result.stdout.decode(errors='replace'), end='')
            if result.returncode:
                print(result.stderr.decode(errors='replace')[:8192])
            result.check_returncode()
    finally:
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')


if __name__ == '__main__':
    main()

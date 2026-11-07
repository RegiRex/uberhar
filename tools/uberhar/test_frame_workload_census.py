#!/usr/bin/env python3
"""CodexAstraLocal: Finite host test of real census and Diagnostic text adapter."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=Path('.'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source_dir.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    inputs = ['src/video_core/uberhar_frame_workload_census.h',
              'src/video_core/uberhar_frame_workload_census_log.h',
              'tools/uberhar/test_frame_workload_census.cpp']
    command = [os.environ.get('CXX', 'g++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DFMT_HEADER_ONLY', '-I' + str(source / 'src'),
               '-I' + str(source / 'externals/fmt/include'),
               str(source / inputs[-1]), '-o', str(output / 'test-frame-census')]
    result = {'source_sha256': {p: hashlib.sha256((source / p).read_bytes()).hexdigest() for p in inputs},
              'command': command, 'scope': 'host helper/formatting; no Vulkan/device or full application build'}
    for phase, cmd in [('compile', command), ('run', [str(output / 'test-frame-census')])]:
        completed = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (output / (phase + '.log')).write_text(completed.stdout)
        result[phase + '_exit'] = completed.returncode
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        if completed.returncode:
            print(completed.stdout)
            raise SystemExit(completed.returncode)
    print((output / 'run.log').read_text(), end='')


if __name__ == '__main__':
    main()

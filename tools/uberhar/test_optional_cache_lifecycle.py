#!/usr/bin/env python3
"""CodexAstraLocal: Compile and run the production optional cache contract fixture."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    parent = root / 'build/uberhar-probe/optional-cache-lifecycle'
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    source = Path(__file__).with_suffix('.cpp')
    binary = output / 'fixture'
    command = ['c++', '-std=c++20', '-O2', '-DXXH_INLINE_ALL',
               '-I' + str(root / 'src'), '-I' + str(root / 'externals/xxHash'),
               str(source), '-o', str(binary)]
    with (output / 'compile.log').open('w') as log:
        built = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if built.returncode:
        raise RuntimeError(f'compile failed: {output}')
    with (output / 'run.log').open('w') as log:
        result = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=10)
    inputs = [source] + [root / 'src/video_core/renderer_vulkan' / name for name in (
        'uberhar_compile_budget.h', 'uberhar_optional_failure_cache.h',
        'uberhar_optional_disk_cache.h', 'uberhar_spirv_cache.h')]
    (output / 'result.json').write_text(json.dumps({
        'command': command, 'exit_code': result.returncode,
        'sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    }, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError(f'contract failure: {output}')
    print(f'Optional cache lifecycle passed: {output}')


if __name__ == '__main__':
    main()

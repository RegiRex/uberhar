#!/usr/bin/env python3
"""CodexAstraLocal: Bound one real-file optional-cache persistence fixture, no GPU."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
from test_optional_fragment_worker import extract


def main():
    root = Path(__file__).resolve().parents[2]
    parent = root / 'build/uberhar-probe/optional-cache-io'
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    production = root / 'src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp'
    source = Path(__file__).with_suffix('.cpp')
    text = production.read_text()
    markers = [
        'std::vector<u32> OptionalProfileWords(',
        'u64 ShaderDiskCache::OptionalCompilerIdentity(',
        'bool ShaderDiskCache::OptionalWriteAllowed(',
        'std::vector<u32> ShaderDiskCache::LoadOrCompileOptional(',
        'bool ShaderDiskCache::WarmDemand(',
        'void ShaderDiskCache::LoadUsefulRecipes(',
        'void ShaderDiskCache::SaveUsefulRecipesAfterDrain(',
    ]
    (output / 'optional_cache_io_production.inc').write_text('\n\n'.join(extract(text, m) for m in markers))
    binary = output / 'fixture'
    command = ['c++', '-std=c++20', '-O2', '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL',
               '-I' + str(output), '-I' + str(root / 'src'),
               '-I' + str(root / 'externals/xxHash'), '-I' + str(root / 'externals/fmt/include'),
               str(source), '-o', str(binary)]
    with (output / 'compile.log').open('w') as log:
        built = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    if built.returncode:
        raise RuntimeError(f'compile failed: {output}')
    # CodexAstraLocal: Temporary artifacts are real disk files and always cleaned,
    # while source hashes, compiler output and executed result remain reviewable.
    with tempfile.TemporaryDirectory(prefix='uberhar-cache-io-') as disk:
        with (output / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), disk], stdout=log, stderr=subprocess.STDOUT, timeout=15)
    inputs = [source, Path(__file__), production,
              root / 'src/video_core/renderer_vulkan/uberhar_optional_disk_cache.h',
              root / 'src/video_core/renderer_vulkan/uberhar_spirv_cache.h']
    (output / 'result.json').write_text(json.dumps({
        'command': command, 'exit_code': result.returncode,
        'sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        'boundary': 'Real stdio/filesystem FileUtil adapter; generated SCM constants and compiler output modeled; no Android SAF or Vulkan.'
    }, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError(f'optional persistence failure: {output}')
    print(f'Optional cache actual-file lifecycle passed: {output}')


if __name__ == '__main__':
    main()

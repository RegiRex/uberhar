#!/usr/bin/env python3
"""CodexAstraLocal: Execute real bounded dedup, inclusive uploader and Vulkan upload hunk.

Only Vulkan recording/storage boundaries are replaced. Reconstruct every indexed
corner byte, exercise collision/refusal/generation paths and deferred command
lifetime, and check OFF/excluded paths use the original bytes and draw command.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from test_ready_cpu_fragments import function


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--xxhash-dir', type=Path)
    ap.add_argument('--sanitizers', action='store_true', help='Requires host ASan/UBSan runtimes')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    header = root / 'src/video_core/rasterizer_accelerated.h'
    source = header.with_suffix('.cpp')
    vk = root / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp'
    helper = root / 'src/video_core/uberhar_indexed_cpu_output.h'
    fixture = Path(__file__).with_suffix('.cpp')
    xxdir = args.xxhash_dir or root / 'externals/xxHash'
    expected = subprocess.check_output(['git', 'ls-tree', 'HEAD', 'externals/xxHash'], cwd=root,
                                       text=True).split()[2]
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=xxdir, text=True).strip()
    if actual != expected:
        raise RuntimeError('XXH3 dependency is not the production-pinned submodule')
    declarations = header.read_text()
    uploader = function(source.read_text(), 'RasterizerAccelerated::CpuOutputUpload RasterizerAccelerated::UploadCpuOutput(')
    body = vk.read_text()
    # CodexAstraLocal: Keep the complete real synchronous fallback branch,
    # including indexed-output selection, while the independent worker lease
    # is exercised by its ownership/stream fixture.
    (out / 'actual_upload.inc').write_text(function(body, '            if (!queued_worker_upload) {'))
    (out / 'actual_types.inc').write_text(function(declarations, 'struct CpuOutputUpload {') + ';\n' +
        function(declarations, 'struct IndexedOutputStats {') + ';\n')
    (out / 'actual_method.inc').write_text(uploader)
    paths = [header, source, vk, helper, fixture, Path(__file__).resolve(), xxdir / 'xxhash.h']
    proof = {'author': 'CodexAstraLocal', 'source_sha256': {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
        'xxhash_commit': expected, 'status': 'running'}
    started = time.monotonic()
    try:
        binary = out / 'fixture'
        cmd = ['c++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I' + str(out), '-I' + str(root / 'src'), '-I' + str(xxdir),
               str(fixture), '-o', str(binary)]
        if args.sanitizers:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        proof['sanitizers'] = args.sanitizers
        result = subprocess.run(cmd, cwd=root, capture_output=True, timeout=90)
        (out / 'compile.log').write_bytes(result.stdout + result.stderr)
        proof['compile_exit'] = result.returncode
        result.check_returncode()
        result = subprocess.run([str(binary)], cwd=root, capture_output=True, timeout=30)
        (out / 'run.log').write_bytes(result.stdout + result.stderr)
        proof['run_exit'] = result.returncode
        result.check_returncode()
        proof['status'] = 'passed'
    except Exception as error:
        proof['status'] = 'failed'
        proof['error'] = str(error)
        raise
    finally:
        proof['seconds'] = round(time.monotonic() - started, 3)
        (out / 'RESULT.json').write_text(json.dumps(proof, indent=2) + '\n')
    print('PASS indexed CPU output:', out)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""CodexAstraLocal: Run one finite image-work correctness check with a pinned ICD."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT / 'build/uberhar-probe/image-work')
parser.add_argument('--icd', type=Path)
args = parser.parse_args()
icd = args.icd or next((p for p in [Path('/usr/share/vulkan/icd.d/lvp_icd.json'),
    Path('/usr/share/vulkan/icd.d/lvp_icd.x86_64.json')] if p.is_file()), None)
if icd is None:
    raise SystemExit('Required software Vulkan ICD is unavailable')
args.output.mkdir(parents=True, exist_ok=True)
out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
env = dict(os.environ, VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd))
proof = {'author': 'CodexAstraLocal', 'status': 'INCOMPLETE', 'commands': [],
         'scope': 'Region eligibility and exact RGBA8 transfer pixels; no target performance claim',
         'inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [
             Path(__file__), ROOT / 'tools/uberhar/test_image_work.cpp',
             ROOT / 'src/video_core/renderer_vulkan/uberhar_image_work.h', icd]}}
# CodexAstraLocal: Preserve each command result and source identity even on failure;
# completion is bounded to this test process, never to owner gameplay capture.
try:
    commands = [(['c++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'),
                 '-I', str(ROOT / 'externals/vulkan-headers/include'),
                 str(ROOT / 'tools/uberhar/test_image_work.cpp'), '-Wl,-l:libvulkan.so.1',
                 '-o', str(out / 'probe')], 'compile', 60),
                ([str(out / 'probe')], 'execute', 30)]
    for argv, name, timeout in commands:
        result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, timeout=timeout)
        (out / (name + '.stdout')).write_bytes(result.stdout)
        (out / (name + '.stderr')).write_bytes(result.stderr)
        proof['commands'].append({'argv': argv, 'returncode': result.returncode})
        result.check_returncode()
    proof['status'] = 'PASS'
finally:
    (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
print(json.dumps({'status': proof['status'], 'output': str(out)}))

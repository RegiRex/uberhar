#!/usr/bin/env python3
"""AstraEH: Validate real fallback modules with both frontend optimizer settings.

The compact loop must reach SPIR-V with DontUnroll intact. This checks the
compiler input sent to Vulkan, not whether a particular device obeys the hint.
"""

from pathlib import Path
import subprocess
import sys


def checked(command):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}\n{result.stderr}")
    return result.stdout


cases = sorted(Path(sys.argv[1]).glob("dynamic-*.frag"))
if len(cases) != 64:
    raise AssertionError(f"Expected 64 complete fragment families, got {len(cases)}")

for label, option in (("unoptimized", "-Od"), ("optimized", "-Os")):
    sizes = []
    for source in cases:
        binary = source.with_suffix(f".{label}.spv")
        checked(["glslangValidator", "-V", "--target-env", "vulkan1.1", option,
                 str(source), "-o", str(binary)])
        checked(["spirv-val", "--target-env", "vulkan1.1", str(binary)])
        assembly = checked(["spirv-dis", str(binary)])
        if not any("OpLoopMerge" in line and "DontUnroll" in line
                   for line in assembly.splitlines()):
            raise AssertionError(f"TEV loop hint missing: {binary}")
        # AstraEH: Catch Vulkan push-constant ABI drift, including the two new controls.
        for member, offset in enumerate((0, 96, 100, 104)):
            expected = f"OpMemberDecorate %UberTev {member} Offset {offset}"
            if expected not in assembly:
                raise AssertionError(f"Fallback ABI mismatch: {binary}: {expected}")
        sizes.append(binary.stat().st_size)
    print(f"PASS: {len(cases)} {label} Vulkan modules; DontUnroll and 108-byte ABI verified; "
          f"SPIR-V size {min(sizes)}..{max(sizes)} bytes", flush=True)

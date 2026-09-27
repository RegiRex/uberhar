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

# AstraEH: Also validate every distinct complete lighting program from the pixel
# corpus. Deduplicate exact source, not keys, and check both frontend optimizer modes.
if len(sys.argv) > 2:
    fragment_cases = sorted(Path(sys.argv[2]).glob("*.frag"))
    if len(fragment_cases) != 2112:
        raise AssertionError(f"Expected 1056 specialized/generic pairs, got {len(fragment_cases)} files")
    unique = {source.read_text(): source for source in cases + fragment_cases}
    cases = list(unique.values())

for label, option in (("unoptimized", "-Od"), ("optimized", "-Os")):
    sizes = []
    for source in cases:
        binary = source.with_suffix(f".{label}.spv")
        checked(["glslangValidator", "-V", "--target-env", "vulkan1.1", option,
                 str(source), "-o", str(binary)])
        checked(["spirv-val", "--target-env", "vulkan1.1", str(binary)])
        assembly = checked(["spirv-dis", str(binary)])
        if "specialized" not in source.name:
            # AstraEH: Lit generic shaders need both lighting and TEV loops kept compact.
            required_loops = 2 if "uber_light_count" in source.read_text() else 1
            loop_hints = sum("OpLoopMerge" in line and "DontUnroll" in line
                             for line in assembly.splitlines())
            if loop_hints < required_loops:
                raise AssertionError(f"Compact loop hint missing: {binary}: {loop_hints}")
            # AstraEH: Catch Vulkan ABI drift, including runtime lighting at bytes 108–119.
            for member, offset in enumerate((0, 96, 100, 104, 108, 112, 116, 120, 124)):
                expected = f"OpMemberDecorate %UberTev {member} Offset {offset}"
                if expected not in assembly:
                    raise AssertionError(f"Fallback ABI mismatch: {binary}: {expected}")
        sizes.append(binary.stat().st_size)
    print(f"PASS: {len(cases)} {label} Vulkan modules; DontUnroll and 128-byte ABI verified; "
          f"SPIR-V size {min(sizes)}..{max(sizes)} bytes", flush=True)

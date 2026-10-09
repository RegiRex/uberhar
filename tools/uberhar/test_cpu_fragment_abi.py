#!/usr/bin/env python3
"""CodexAstraLocal: Build synthetic actual CPU-output/trivial-VS fragment fixtures.

Each run has a separate output directory. No game content, device or renderer
selection policy is involved. Optional rendering is a host OpenGL check; Vulkan
modules can be validated separately by the companion renderer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def extract_function(source, signature):
    # CodexAstraLocal: Compile the actual production body, with only its owner shell replaced.
    if source.count(signature) != 1:
        raise AssertionError(f"Expected one production function: {signature}")
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(command, log, timeout=120):
    # CodexAstraLocal: Retain failed compiler/probe diagnostics alongside successful provenance.
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    log.write_text(result.stdout + result.stderr)
    result.check_returncode()
    if result.stdout:
        print(result.stdout, end="", flush=True)


def prepare(directory):
    # CodexAstraLocal: Keep hardware lanes, quaternion correction and layout bitfields source-derived.
    cpu_path = ROOT / "src/video_core/rasterizer_accelerated.cpp"
    header_path = cpu_path.with_suffix(".h")
    layout_path = ROOT / "src/video_core/renderer_vulkan/vk_graphics_pipeline.h"
    raster_path = ROOT / "src/video_core/renderer_vulkan/vk_rasterizer.cpp"
    cpu = cpu_path.read_text()
    header = header_path.read_text()
    start = header.index("    struct HardwareVertex {")
    fields = header[start:header.index("\n    };", start) + len("\n    };")]
    (directory / "hardware-fields.inc").write_text(fields)
    signatures = (
        "RasterizerAccelerated::HardwareVertex::HardwareVertex(",
        "static bool AreQuaternionsOpposite(",
        "void RasterizerAccelerated::AddTriangle(",
    )
    methods = "\n".join(extract_function(cpu, signature) for signature in signatures)
    (directory / "hardware-bodies.inc").write_text(methods)
    layout = layout_path.read_text()
    start = layout.index("union VertexBinding {")
    end = layout.index("struct AttachmentInfo {", start)
    (directory / "layout-types.inc").write_text(layout[start:end])
    method = extract_function(raster_path.read_text(),
                              "void RasterizerVulkan::MakeSoftwareVertexLayout()")
    (directory / "layout-body.inc").write_text(
        method.replace("RasterizerVulkan::", "RasterizerAccelerated::"))
    source = ROOT / "tools/uberhar/test_cpu_fragment_abi.cpp"
    (directory / "probe.cpp").write_bytes(source.read_bytes())

    # CodexAstraLocal: Accurate-multiply currently affects guest VS only; freeze both FS-profile controls.
    original_probe = ROOT / "tools/uberhar/fragment_state_probe.cpp"
    fragment = original_probe.read_text()
    for needle in ("if (argc != 2)", "profile.is_vulkan = true;"):
        if fragment.count(needle) != 1:
            raise AssertionError(f"Fragment producer changed: {needle}")
    fragment = fragment.replace("if (argc != 2)", "if (argc != 3)")
    fragment = fragment.replace(
        "profile.is_vulkan = true;",
        "profile.is_vulkan = true;\n"
        "    // CodexAstraLocal: Preserve the complete corpus under either arithmetic profile.\n"
        "    profile.enable_accurate_mul = std::stoi(argv[2]) != 0;")
    (directory / "fragment-state-probe.cpp").write_text(fragment)
    paths = [cpu_path, header_path, layout_path, raster_path, source, original_probe]
    paths += [ROOT / name for name in (
        "src/video_core/pica/primitive_assembly.cpp",
        "src/video_core/pica/primitive_assembly.h",
        "src/video_core/shader/generator/glsl_shader_gen.cpp",
        "src/video_core/shader/generator/glsl_fs_shader_gen.cpp",
        "src/video_core/shader/generator/glsl_fs_shader_gen.h",
        "tools/uberhar/test_cpu_fragment_abi.py",
        "tools/uberhar/compare_cpu_fragment_abi.py",
        "src/video_core/shader/generator/pica_fs_config.cpp",
        "src/video_core/shader/generator/shader_uniforms.h",
    )]
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/cpu-fragment-abi")
    # CodexAstraLocal: The optional mode adds a third route without removing any legacy ABI checks.
    parser.add_argument("--static-tev", action="store_true")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--require-spirv", action="store_true")
    args = parser.parse_args()
    if args.require_spirv and not args.render:
        parser.error("--require-spirv needs --render")
    parent = args.output.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="fixture-", dir=parent))
    hashes = prepare(directory)
    flags = [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-DFMT_HEADER_ONLY",
             "-DXXH_INLINE_ALL", "-ffunction-sections", "-fdata-sections", "-Isrc",
             "-Iexternals/fmt/include", "-Iexternals/boost", "-Iexternals/xxHash",
             "-Iexternals/nihstro/include", "-Iexternals/json", "-Wl,--gc-sections"]
    # CodexAstraLocal: Only the corpus producer consumes this test-only emission flag.
    fragment_flags = flags + (["-DUBERHAR_STATIC_TEV_TEST"] if args.static_tev else [])
    commands = [
        flags + [str(directory / "probe.cpp"), "src/video_core/pica/primitive_assembly.cpp",
                 "src/video_core/shader/generator/glsl_shader_gen.cpp",
                 "-o", str(directory / "probe")],
        fragment_flags + [str(directory / "fragment-state-probe.cpp"),
                 "src/video_core/shader/generator/glsl_fs_shader_gen.cpp",
                 "src/video_core/shader/generator/pica_fs_config.cpp",
                 "-o", str(directory / "fragment-probe")],
    ]
    for index, command in enumerate(commands):
        checked(command, directory / f"compile-{index}.log")
    checked([str(directory / "probe"), str(directory / "fixtures")], directory / "probe.log")
    for flag in (False, True):
        checked([str(directory / "fragment-probe"), str(directory / f"corpus-{str(flag).lower()}"),
                 str(int(flag))], directory / f"fragment-{flag}.log")
    left = directory / "corpus-false"
    right = directory / "corpus-true"
    files = sorted(left.iterdir())
    expected_files = 1056 * (5 if args.static_tev else 4)
    if len(files) != expected_files or sorted(path.name for path in files) != sorted(path.name for path in right.iterdir()):
        raise AssertionError("Expected exactly 1056 complete shader/state/uniform groups per profile")
    if any(path.read_bytes() != (right / path.name).read_bytes() for path in files):
        raise AssertionError("Accurate-multiply now affects fragment source; extend the two-profile oracle")
    # CodexAstraLocal: Source/payload fingerprints and an atomic latest pointer keep repeated runs reviewable.
    manifest = {
        "author": "CodexAstraLocal", "directory": str(directory), "source_sha256": hashes,
        "static_tev": args.static_tev,
        "compile_commands": commands, "accurate_mul_profiles_identical_files": len(files),
        "fixture_sha256": {str(path.relative_to(directory)): digest(path)
                           for path in (directory / "fixtures").iterdir()},
        "corpus_sha256": {path.name: digest(path) for path in files},
    }
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    temporary = directory / "latest.json"
    temporary.write_text(json.dumps({"directory": str(directory)}, indent=2) + "\n")
    os.replace(temporary, parent / "latest.json")
    print(f"PASS actual CPU ABI, two identical 1056-state profiles: {directory}", flush=True)
    if args.render:
        command = [sys.executable, str(ROOT / "tools/uberhar/compare_cpu_fragment_abi.py"),
                   str(directory)]
        if args.require_spirv:
            command.append("--require-spirv")
        checked(command, directory / "render.log", timeout=600)


if __name__ == "__main__":
    main()

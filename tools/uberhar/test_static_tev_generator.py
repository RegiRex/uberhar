#!/usr/bin/env python3
"""CodexAstraLocal: Preserve prepared-TEV identity, emitted math and finite pixel controls.

The host phase emits synthetic cases from the actual generator. Optional rendering
compares three extracted TEV bodies on host OpenGL; complete CPU-vertex/fragment
pixels are separately checked by test_cpu_fragment_abi.py --static-tev. Vulkan
family modules are compiled/validated here, not executed. No title or device data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def checked(command, log, timeout=180):
    # CodexAstraLocal: Preserve exact commands, wall duration and failed diagnostics.
    start = time.monotonic()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    log.write_text(result.stdout + result.stderr)
    row = {"argv": command, "returncode": result.returncode,
           "elapsed_seconds": time.monotonic() - start, "log": log.name}
    if result.returncode:
        raise RuntimeError(f"Required command failed ({result.returncode}); see {log}")
    if result.stdout:
        print(result.stdout, end="", flush=True)
    return row


def prepare(parent):
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    flags = [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-DFMT_HEADER_ONLY",
             "-DXXH_INLINE_ALL", "-Isrc", "-Iexternals/fmt/include", "-Iexternals/boost",
             "-Iexternals/xxHash", "-Iexternals/nihstro/include"]
    sources = {
        "generator": "src/video_core/shader/generator/glsl_fs_shader_gen.cpp",
        "config": "src/video_core/shader/generator/pica_fs_config.cpp",
        "key-family": "tools/uberhar/test_static_tev_generator.cpp",
        "tev": "tools/uberhar/shader_probe.cpp",
    }
    commands = []
    source_paths = {ROOT / name for name in sources.values()}
    source_paths.update(ROOT / name for name in (
        "tools/uberhar/test_static_tev_generator.py", "tools/uberhar/compare_static_tev.py",
        "tools/uberhar/compare_tev.py"))
    before = {path: digest(path) for path in source_paths}
    # CodexAstraLocal: Compile shared production objects once and retain dependencies
    # per translation unit so reuse cannot accept a changed included header.
    for name, source in sources.items():
        command = flags + (["-DUBERHAR_STATIC_TEV_TEST"] if name == "tev" else [])
        command += ["-MMD", "-MF", str(directory / f"{name}.d"), "-c", source,
                    "-o", str(directory / f"{name}.o")]
        commands.append(checked(command, directory / f"compile-{name}.log"))
        dependency = (directory / f"{name}.d").read_text().replace("\\\n", " ")
        for item in shlex.split(dependency.split(":", 1)[1]):
            path = Path(item)
            source_paths.add(path if path.is_absolute() else ROOT / path)
    for name in ("key-family", "tev"):
        command = [flags[0], str(directory / f"{name}.o"), str(directory / "generator.o"),
                   str(directory / "config.o"), "-o", str(directory / name)]
        commands.append(checked(command, directory / f"link-{name}.log"))
        commands.append(checked([str(directory / name), str(directory / name.replace("key-family", "families"))
                                 + ("-cases" if name == "tev" else "")],
                                directory / f"emit-{name}.log"))
    expected = {f"{i}{suffix}" for i in range(816)
                for suffix in (".frag", ".bin", ".raw", "-partial.frag")}
    expected.update(f"dynamic-{i}.frag" for i in range(64))
    assert {p.name for p in (directory / "tev-cases").iterdir()} == expected
    assert {p.name for p in (directory / "families").iterdir()} == {f"{i}.frag" for i in range(64)}
    assert "PASS 805 key/prefix/runtime/compiler-ownership controls" in (directory / "emit-key-family.log").read_text()
    assert all(digest(path) == value for path, value in before.items()), "Source changed while compiling"
    hashes = {str(path.relative_to(ROOT)): digest(path) for path in sorted(source_paths)}
    # CodexAstraLocal: Retain exact source bytes and corpus identities before any
    # optional graphics execution; repeated runs get separate directories.
    for name in hashes:
        target = directory / "sources" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / name).read_bytes())
    manifest = {
        "author": "CodexAstraLocal", "source_sha256": hashes, "commands": commands,
        "key_controls": 805, "tev_cases": 816, "nonempty_family_modules": 64,
        "family_accurate_mul_equalities": 64,
        "artifacts": {str(path.relative_to(directory)): digest(path)
                      for path in sorted(directory.rglob("*")) if path.is_file()},
    }
    write_json(directory / "manifest.json", manifest)
    temporary = directory / "latest.json"
    write_json(temporary, {"directory": str(directory), "manifest_sha256": digest(directory / "manifest.json")})
    os.replace(temporary, parent / "latest.json")
    return directory, manifest


def reuse(parent):
    # CodexAstraLocal: Reuse is exact-source/artifact validation, never a cached PASS.
    pointer = json.loads((parent / "latest.json").read_text())
    directory = Path(pointer["directory"])
    assert directory.parent.resolve() == parent.resolve()
    assert digest(directory / "manifest.json") == pointer["manifest_sha256"]
    manifest = json.loads((directory / "manifest.json").read_text())
    assert manifest["tev_cases"] == 816 and manifest["key_controls"] == 805
    assert manifest["nonempty_family_modules"] == 64
    for name, expected in manifest["source_sha256"].items():
        assert digest(ROOT / name) == expected, f"Changed reused source: {name}"
    for name, expected in manifest["artifacts"].items():
        assert digest(directory / name) == expected, f"Changed reused artifact: {name}"
    return directory, manifest


def shader_tool(name, fallback):
    found = shutil.which(name)
    path = Path(found) if found else ROOT / fallback
    if not path.is_file():
        raise RuntimeError(f"Missing required shader tool: {name}")
    return str(path)


def render(directory, require_spirv):
    run = Path(tempfile.mkdtemp(prefix="render-", dir=directory))
    renderer = ROOT / "tools/uberhar/compare_static_tev.py"
    base = [sys.executable, str(renderer), str(directory / "tev-cases")]
    commands = [checked(base + ["--output", str(run / "positive")], run / "positive.log", 900)]
    report = json.loads((run / "positive/report.json").read_text())
    assert report["cases"] == 816 and report["three_way_rgba_vectors"] == 208896
    assert report["fetch_vectors_per_candidate"] == 208896
    controls = []
    # CodexAstraLocal: Defects must reach an actual output/fetch mismatch, not
    # succeed because compilation failed or because the mutant was never exercised.
    for defect in ("wrong_plan", "missing_buffer_advance", "duplicate_fetch"):
        output = run / defect
        command = base + ["--output", str(output), "--defect", defect]
        start = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
        log = run / f"{defect}.log"
        log.write_text(result.stdout + result.stderr)
        assert result.returncode != 0 and "TEV output/fetch mismatch:" in result.stderr, defect
        failure = json.loads((output / "failure.json").read_text())
        assert 0 <= failure["case"] < 816 and 0 <= failure["sample"] < 256
        assert (output / "failed.comp").is_file() and (output / "failed-output.bin").stat().st_size == 256*5*16
        controls.append({"defect": defect, "argv": command, "returncode": result.returncode,
                         "elapsed_seconds": time.monotonic()-start, "failure": failure})
    modules = []
    if require_spirv:
        compiler = shader_tool("glslangValidator", "build/uberhar-validators/glslang/StandAlone/glslang")
        validator = shader_tool("spirv-val", "build/uberhar-validators/spirv-tools/tools/spirv-val")
        target = run / "modules"
        target.mkdir()
        # CodexAstraLocal: Both frontend policies compile the same nonempty
        # cube/procedural/lighting families; this is validation, not pixel coverage.
        for index in range(64):
            source = directory / "families" / f"{index}.frag"
            for optimized in (False, True):
                stem = f"{index}-{int(optimized)}"
                output = target / f"{stem}.spv"
                command = [compiler, "-V", "--target-env", "vulkan1.1"]
                command += ([] if optimized else ["-Od"]) + ["-Os", "-S", "frag", str(source), "-o", str(output)]
                compile_row = checked(command, target / f"{stem}.compile.log", 60)
                validate_row = checked([validator, "--target-env", "vulkan1.1", str(output)],
                                       target / f"{stem}.validate.log", 30)
                modules.append({"case": index, "optimized": optimized, "source_sha256": digest(source),
                                "binary_sha256": digest(output), "compile": compile_row, "validate": validate_row})
        assert len(modules) == 128
    report = {"author": "CodexAstraLocal", "manifest_sha256": digest(directory / "manifest.json"),
              "commands": commands, "tev_report_sha256": digest(run / "positive/report.json"),
              "controls": controls, "vulkan_validation_required": require_spirv, "modules": modules,
              "scope": "Finite host GL extracted TEV parity/fetch controls; Vulkan family compilation only. Real CPU88B/full fragment ABI is a separate gate."}
    write_json(run / "report.json", report)
    write_json(run / "provenance.json", {
        "author": "CodexAstraLocal", "driver_sha256": digest(Path(__file__)),
        "renderer_sha256": digest(renderer), "manifest_sha256": digest(directory / "manifest.json"),
        "artifacts": {str(path.relative_to(run)): digest(path) for path in sorted(run.rglob("*"))
                      if path.is_file() and "mesa-cache" not in path.parts},
    })
    return run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/static-tev-generator")
    parser.add_argument("--reuse-build", action="store_true")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--require-spirv", action="store_true")
    args = parser.parse_args()
    if args.require_spirv and not args.render:
        parser.error("--require-spirv needs --render")
    parent = args.output.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    directory, _ = reuse(parent) if args.reuse_build else prepare(parent)
    result = render(directory, args.require_spirv) if args.render else directory
    print(f"PASS static TEV generator evidence: {result}", flush=True)


if __name__ == "__main__":
    main()

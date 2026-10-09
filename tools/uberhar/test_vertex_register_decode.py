#!/usr/bin/env python3
"""CodexAstraLocal: Compare packed-register decoding through the real vertex loader.

Synthetic aligned RAM is modeled; constructor descriptors and live input values
must equal the inherited array-based decoder. This is not a game or speed test.
The default host gate and optional Android/ARM64 QEMU gate run all three defects.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

from test_vertex_input_recipes import MEMORY, block, replace_once


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(value, reason):
    if not value:
        raise RuntimeError(reason)


# CodexAstraLocal: Preserve the actual prior decoder bodies as an independent
# reference. Only these methods change in a shadow of the current register header;
# the real constructor and live-load implementation remain shared and unchanged.
LEGACY = {
    "VertexAttributeFormat GetFormat(std::size_t n) const": """
        VertexAttributeFormat GetFormat(std::size_t n) const {
            VertexAttributeFormat formats[] = {format0, format1, format2, format3,
                                               format4, format5, format6, format7,
                                               format8, format9, format10, format11};
            return formats[n];
        }""",
    "u32 GetNumElements(std::size_t n) const": """
        u32 GetNumElements(std::size_t n) const {
            u32 sizes[] = {size0, size1, size2, size3, size4, size5,
                           size6, size7, size8, size9, size10, size11};
            return sizes[n] + 1;
        }""",
    "u32 GetComponent(std::size_t n) const": """
        u32 GetComponent(std::size_t n) const {
            u32 components[] = {comp0, comp1, comp2, comp3, comp4, comp5,
                                comp6, comp7, comp8, comp9, comp10, comp11};
            return components[n];
        }""",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/vertex-register-decode"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    compiler = [str(args.compiler)] if args.compiler else shlex.split(os.environ.get("CXX", "c++"))
    require(bool(compiler), "empty compiler command")
    executable = shutil.which(compiler[0])
    require(executable is not None, "compiler unavailable")
    launcher = [str(args.qemu.resolve())] if args.qemu else []
    flags = ["--target=aarch64-linux-android33", "-static", "-static-libstdc++"] if launcher else []
    header = repo / "src/video_core/pica/regs_pipeline.h"
    body = repo / "src/video_core/pica/vertex_loader.cpp"
    fixture = Path(__file__).with_suffix(".cpp")
    helpers = Path(__file__).with_name("test_vertex_input_recipes.py")
    inputs = [header, body, fixture, helpers, Path(__file__).resolve()]
    proof = {"author": "CodexAstraLocal", "scope": __doc__, "commands": [],
             "inputs": {str(p.relative_to(repo)): digest(p) for p in inputs},
             "tools": {str(Path(executable).resolve()): digest(Path(executable))},
             "variants": {}, "dependencies": {}}
    if launcher:
        proof["tools"][launcher[0]] = digest(Path(launcher[0]))
    for path in inputs:
        target = out / "source" / path.relative_to(repo)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
    fixture = out / "source" / fixture.relative_to(repo)
    memory = out / "include/core/memory.h"
    memory.parent.mkdir(parents=True)
    memory.write_text(MEMORY)

    def save():
        (out / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")

    def command(label, argv, expected=0):
        result = subprocess.run(list(map(str, argv)), cwd=repo, capture_output=True, timeout=120)
        # CodexAstraLocal: Raw initialized-value streams are retained as binary
        # artifacts; CI console output stays textual and bounded on failures.
        suffix = ".output.bin" if label.endswith("-run") else ".stdout"
        (out / (label + suffix)).write_bytes(result.stdout)
        (out / (label + ".stderr")).write_bytes(result.stderr)
        proof["commands"].append({"label": label, "argv": list(map(str, argv)),
                                  "returncode": result.returncode})
        save()
        if result.returncode != expected:
            print(result.stderr.decode(errors="replace")[:16384])
            raise RuntimeError(label + ": unexpected exit; full evidence in " + str(out))
        return result

    current = header.read_text()
    legacy = current
    for anchor, replacement in LEGACY.items():
        legacy = replace_once(legacy, block(legacy, anchor), replacement.strip())
    variants = {"current": current, "legacy": legacy}
    # CodexAstraLocal: Each semantic fault must reach explicit mismatch exit7;
    # unrelated compilation failures, crashes and timeouts never count as passes.
    for name, old, new in [
        ("bad_format", "((n % 8) * 4)) & 3U", "((n % 8) * 4 + 1)) & 3U"),
        ("bad_size", "& 3U) + 1;", "& 3U) + 2;"),
        ("bad_component", "& 15U;", "& 7U;"),
    ]:
        variants[name] = replace_once(current, old, new)

    try:
        outputs = {}
        for name, source in variants.items():
            directory = out / name
            shadow = directory / "video_core/pica/regs_pipeline.h"
            shadow.parent.mkdir(parents=True)
            shadow.write_text(source)
            common = compiler + flags + [
                "-std=c++20", "-O3", "-DNDEBUG", "-DMICROPROFILE_ENABLED=0",
                "-DFMT_HEADER_ONLY", "-ffunction-sections", "-fdata-sections",
                "-I" + str(out / "include"), "-I" + str(directory), "-Isrc",
                "-Iexternals/fmt/include", "-Iexternals/boost", "-Iexternals/microprofile",
                "-isystem", "externals/nihstro/include",
            ]
            objects = []
            for part, path in [("fixture", fixture), ("loader", body)]:
                obj = directory / (part + ".o")
                dep = directory / (part + ".d")
                objects.append(obj)
                command(name + "-" + part + "-compile",
                        common + ["-MD", "-MF", dep, "-c", path, "-o", obj])
                for word in dep.read_text().replace("\\\n", " ").split(":", 1)[1].split():
                    dependency = Path(word)
                    if not dependency.is_absolute():
                        dependency = repo / dependency
                    if dependency.is_file():
                        proof["dependencies"][str(dependency.resolve())] = digest(dependency)
            binary = directory / "test"
            command(name + "-link", compiler + flags + objects + ["-Wl,--gc-sections", "-o", binary])
            result = command(name + "-run", launcher + [binary], 7 if name.startswith("bad_") else 0)
            proof["variants"][name] = {
                "binary_sha256": digest(binary), "header_sha256": digest(shadow),
                "output_sha256": hashlib.sha256(result.stdout).hexdigest(),
                "output_bytes": len(result.stdout), "summary": result.stderr.decode(errors="replace"),
            }
            if name in ("current", "legacy"):
                outputs[name] = result.stdout
                require(len(result.stdout) == 8945668, "incomplete initialized-value population")
                require(result.stderr == b"scalar_checks=590400 constructor_cases=8192 live_loads=24576 errors=37405\n",
                        "fixture population or overflow behavior changed")
        require(outputs["current"] == outputs["legacy"], "production loader differs from inherited decoder")
        require(all(digest(repo / path) == value for path, value in proof["inputs"].items()),
                "source changed during validation")
        proof["result"] = "PASS"
        print("Packed vertex registers PASS: 590400 scalar checks, 8192 layouts, "
              "24576 live loads and three rejected defects; " + str(out))
    finally:
        proof["artifacts"] = {str(p.relative_to(out)): digest(p) for p in out.rglob("*")
                              if p.is_file() and p.name != "provenance.json"}
        save()


if __name__ == "__main__":
    main()

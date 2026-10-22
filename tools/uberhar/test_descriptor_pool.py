#!/usr/bin/env python3
"""CodexAstraLocal: Execute exact pool methods against strict Vulkan endpoints.

The extracted owner bindings/hints cover supported graphics, rectangle and utility
consumers. Endpoint capacity/failure/completion is modeled; this gate neither
executes a GPU draw nor qualifies Android exception delivery or title coverage.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def declaration(text, name, start=0):
    # CodexAstraLocal: Keep each production declaration intact; only the single
    # Native texture-template parameter is instantiated explicitly below.
    match = re.search(r"(?:const|constexpr)(?: static)? std::array(?:<[^;]+?>)?\s+" + name + r"\s*(?:=\s*)?\{",
                      text[start:])
    if not match:
        raise ValueError(f"missing exact binding declaration {name}")
    begin = start + match.start()
    brace = start + match.end() - 1
    depth = 0
    for pos in range(brace, len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if depth == 0:
            assert text[pos + 1] == ";"
            return text[begin:pos + 2]
    raise ValueError(f"unclosed binding declaration {name}")


def owner_definition(text, signature):
    # CodexAstraLocal: Diagnostic parameters can change without changing the
    # heap contract. Bind the unique actual method body, so its declaration and
    # allocation must still belong to the same owner rather than a later method.
    if text.count(signature) != 1:
        raise ValueError(f"ambiguous or missing owner definition {signature}")
    begin = text.index(signature)
    brace = text.index("{", begin)
    depth = 0
    for pos in range(brace, len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if depth == 0:
            return text[begin:pos + 1]
    raise ValueError(f"unclosed owner definition {signature}")


def owner_contracts(root):
    base = root / "src/video_core/renderer_vulkan"
    inputs = {}
    contracts = []
    for filename in ("vk_pipeline_cache.cpp",
                     "vk_blit_helper.cpp", "renderer_vulkan.cpp", "vk_compute_rect.cpp"):
        path = base / filename
        inputs[str(path.relative_to(root))] = digest(path)
    def add(filename, name, hint, expected, call, template=False, owner=None):
        source = owner if owner is not None else (base / filename).read_text()
        if not re.search(call, source):
            raise ValueError(f"actual heap caller changed: {filename}/{name}")
        body = declaration(source, name)
        if template:
            assert body.count("NumTex0") == 1
            body = body.replace("NumTex0", "1")
        contracts.append(f"{{ constexpr auto stage = vk::ShaderStageFlagBits::eCompute;\n"
                         f"{body}\nCheckContract({name}, {hint}, {expected}); }}")
    # CodexAstraLocal: Retired independent renderer heaps no longer own descriptor pools.
    for name, hint in (("BUFFER_BINDINGS", 32), ("TEXTURE_BINDINGS", 1024), ("UTILITY_BINDINGS", 32)):
        call = (r"DescriptorHeap\{instance, scheduler.GetMasterSemaphore\(\), TEXTURE_BINDINGS<1>\}"
                if name == "TEXTURE_BINDINGS" else
                r"DescriptorHeap\{instance, scheduler.GetMasterSemaphore\(\), " + name + r", 32\}")
        add("vk_pipeline_cache.cpp", name, hint, hint * 4, call, template=name == "TEXTURE_BINDINGS")
    for name, hint in (("COMPUTE_BINDINGS", 1024), ("COMPUTE_BUFFER_BINDINGS", 1024),
                       ("TWO_TEXTURES_BINDINGS", 16), ("SINGLE_TEXTURE_BINDINGS", 16),
                       ("THREE_TEXTURES_BINDINGS", 16)):
        suffix = r"\}" if hint == 1024 else r",\s*16\}"
        add("vk_blit_helper.cpp", name, hint, hint * 4,
            r"\{instance, scheduler.GetMasterSemaphore\(\), " + name + suffix)
    add("renderer_vulkan.cpp", "PRESENT_BINDINGS", 32, 128,
        r"present_heap\{instance, scheduler.GetMasterSemaphore\(\), PRESENT_BINDINGS, 32\}")
    add("vk_compute_rect.cpp", "bindings", 32, 128,
        r"make_unique<DescriptorHeap>\(instance, scheduler.GetMasterSemaphore\(\), bindings,\s*32\)")
    return "\n".join(contracts), inputs, len(contracts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("src/video_core/renderer_vulkan/vk_resource_pool.cpp"))
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/descriptor-pool"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    fixture = Path(__file__).with_suffix(".cpp")
    text = args.source.read_text()
    resource_start = text.index("ResourcePool::ResourcePool(")
    resource_end = text.index("constexpr std::size_t COMMAND_BUFFER_POOL_SIZE", resource_start)
    descriptor_start = text.index("constexpr u32 DESCRIPTOR_SET_BATCH")
    descriptor_end = text.rindex("} // namespace Vulkan")
    methods = text[resource_start:resource_end] + text[descriptor_start:descriptor_end]
    contracts, inputs, contract_count = owner_contracts(root)
    template = fixture.read_text().replace("// UBERHAR_RESOURCE_METHODS", methods).replace(
        "// UBERHAR_DESCRIPTOR_METHODS", "").replace("// UBERHAR_OWNER_CONTRACTS", contracts)
    (run / "production-source.cpp").write_text(text)
    variants = {"baseline": template}
    # CodexAstraLocal: Each defect changes an executed production seam. Required
    # failures prove sensitivity to the actual capacity/retry/lifetime contract.
    mutations = {
        "undersized-pool": ("std::max<u64>(DESCRIPTOR_SET_BATCH,\n                                     u64{descriptor_heap_count_} * DESCRIPTOR_MULTIPLIER)",
                            "u64{descriptor_heap_count_} * DESCRIPTOR_MULTIPLIER"),
        "array-undercount": ("count += binding.descriptorCount;", "++count;"),
        "retry-device-error": ("result != vk::Result::eErrorFragmentedPool)",
                               "result != vk::Result::eErrorFragmentedPool && result != vk::Result::eErrorDeviceLost)"),
        "unbounded-fresh-retry": ("attempt <= retained_pools", "attempt <= pools.size()"),
        "lost-tick-rollback": ("ticks.resize(old_capacity);", "/* defect: leave unpublished ticks */"),
        "lost-fresh-owner-rollback": ("if (pools.size() > retained_pools)", "if (false)"),
    }
    for name, (old, new) in mutations.items():
        assert old in methods, (name, "production mutation seam missing")
        mutant = methods.replace(old, new)
        if name == "unbounded-fresh-retry":
            mutant = mutant.replace("const bool fresh_pool = attempt == retained_pools;",
                                    "const bool fresh_pool = attempt == pools.size();")
            mutant = mutant.replace("if (fresh_pool || (result !=", "if (false || (result !=")
        variants[name] = template.replace(methods, mutant)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    report = {"schema": 1, "scope": "actual pool methods and Vulkan-Hpp ownership; strict modeled endpoints",
              "owner_contracts": contract_count, "inputs": inputs, "results": []}
    for path in (args.source.resolve(), fixture.resolve(), Path(__file__).resolve(),
                 root / "src/video_core/renderer_vulkan/vk_resource_pool.h",
                 root / "src/video_core/shader_recovery_error.h"):
        report["inputs"][str(path.relative_to(root))] = digest(path)
    started = time.monotonic()
    try:
        for name, source in variants.items():
            cpp = run / f"{name}.cpp"
            exe = run / name
            cpp.write_text(source)
            command = [os.environ.get("CXX", "c++"), "-std=c++20", *flags, "-Isrc",
                       "-Iexternals/vulkan-headers/include", str(cpp), "-o", str(exe)]
            with (run / f"{name}.compile.stdout").open("w") as out, (run / f"{name}.compile.stderr").open("w") as err:
                compile_result = subprocess.run(command, cwd=root, stdout=out, stderr=err, timeout=90)
            row = {"name": name, "command": command, "compile_returncode": compile_result.returncode}
            report["results"].append(row)
            if compile_result.returncode:
                raise RuntimeError(f"{name}: compile failed; {run}")
            with (run / f"{name}.stdout").open("w") as out, (run / f"{name}.stderr").open("w") as err:
                result = subprocess.run([str(exe)], cwd=root, stdout=out, stderr=err, timeout=10)
            row["returncode"] = result.returncode
            if name == "baseline":
                if result.returncode != 0: raise RuntimeError(f"baseline failed; {run}")
            elif result.returncode == 0 or "FAIL" not in (run / f"{name}.stderr").read_text():
                raise RuntimeError(f"{name}: intended failure not observed; {run}")
        report["passed"] = True
    finally:
        report["elapsed_seconds"] = time.monotonic() - started
        report["artifacts"] = {str(p.relative_to(run)): digest(p) for p in sorted(run.iterdir()) if p.is_file()}
        (run / "provenance.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"Descriptor pool proof: {run}", flush=True)


if __name__ == "__main__":
    main()

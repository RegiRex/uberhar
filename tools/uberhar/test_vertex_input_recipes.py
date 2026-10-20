#!/usr/bin/env python3
"""CodexAstraLocal: Check complete input recipes with production software components.

Synthetic bytes only. Memory mapping and settings endpoints are modeled; this
does not execute a complete PicaCore, Vulkan, Android or target performance test.
The generic reference is derived from the same source with recipes disabled.
An additional actual legacy-loader oracle checks every normal candidate miss.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile


# CodexAstraLocal: Fail closed if an expected source seam changes. Extracted
# bodies remain actual production statements, with only owner/type names adapted.
def need(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def block(source, anchor):
    need(source.count(anchor) == 1, "ambiguous source anchor: " + anchor)
    start = source.index(anchor)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise RuntimeError("unclosed source: " + anchor)


def replace_once(source, before, after):
    need(source.count(before) == 1, "mutation/source seam changed: " + before)
    return source.replace(before, after, 1)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# CodexAstraLocal: These endpoints retain real VertexLoader/JIT calls but do not
# reproduce guest page tables, pinned MemoryRef ownership or frontend settings.
MEMORY = r'''
#pragma once
#include <span>
#include "common/common_types.h"
namespace Memory {
class MemorySystem {
public:
    void Set(PAddr base_, std::span<u8> bytes_) { base=base_; bytes=bytes_; }
    u8* GetPhysicalPointer(PAddr address) const {
        if(address<base || u64(address-base)>=bytes.size()) return nullptr;
        return bytes.data()+(address-base);
    }
private: PAddr base{}; std::span<u8> bytes;
};
}
'''
SETTINGS = r'''
#pragma once
namespace Settings {
enum class UberharTestMode { Custom };
struct Setting { UberharTestMode GetValue() const { return UberharTestMode::Custom; } };
inline struct { Setting uberhar_test_mode; } values;
}
'''
INSPECTOR = r'''
// CodexAstraLocal: A test-only friend observes recipe selection and injects
// semantic defects without changing the actual candidate production statements.
#pragma once
#include "reference_plan.h"
namespace Pica {
class InputPlanInspector {
public:
    static u32 Selected(const NativeVertexInputPlan& p){return p.recipe!=nullptr;}
    static u32 Selected(const NativeVertexInputPlanReference&){return 0;}
    template<class P>static void Reverse(const P& p,ShaderUnit& unit,const AttributeBuffer& defaults,u32 v){
        for(u32 i=p.count;i-->0;){const auto& op=p.ops[i];
            if(op.is_default)unit.input[op.reg]=defaults[op.attribute];
            else op.read(unit.input[op.reg],op.data+std::size_t(op.stride)*v);}
    }
    template<class P>static void LoseW(const P& p,ShaderUnit& unit){
        for(u32 i=0;i<p.count;++i)if(!p.ops[i].is_default)unit.input[p.ops[i].reg].w=f24::Zero();
    }
};
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/vertex-input-recipes"))
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--input-root", type=Path,
                        help="optional candidate root containing only the three input/PicaCore files")
    parser.add_argument("--mutants", action="store_true")
    args = parser.parse_args()
    root = args.source_root.resolve()
    inputs = (args.input_root or root).resolve()
    fixture_sources = Path(__file__).resolve().parent
    need(platform.machine().lower() in ("x86_64", "amd64"), "host x64 JIT execution required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="fixtures-", dir=output))
    sources = {}

    def read(path):
        path = path.resolve()
        sources[str(path)] = digest(path)
        return path.read_text()

    header = read(inputs / "src/video_core/pica/uberhar_vertex_input.h")
    core = read(inputs / "src/video_core/pica/pica_core.cpp")
    core_header = read(inputs / "src/video_core/pica/pica_core.h")
    raster_header = read(root / "src/video_core/rasterizer_accelerated.h")
    raster = read(root / "src/video_core/rasterizer_accelerated.cpp")
    runner = fixture_sources / "test_vertex_input_recipes_runner.cpp"
    accounting = fixture_sources / "test_vertex_input_recipes_accounting.cpp"
    for path in (Path(__file__), runner, accounting):
        read(path)
        snapshot = run / "fixture-source" / path.name
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        snapshot.write_bytes(path.read_bytes())
    runner = run / "fixture-source" / runner.name
    accounting = run / "fixture-source" / accounting.name

    # CodexAstraLocal: Bind the real per-draw preparation and once-per-finished-FIFO
    # accounting/report consumers. No new bucket operation belongs in the shade loop.
    prepare = block(core, "            const auto prepare_input =")
    need("pipeline.num_vertices != 0" in prepare, "empty-draw recipe selection gate missing")
    need("std::array<u64, 5> native_input_recipe_invocations{};" in core_header,
         "five-bucket owner storage changed")
    load = block(core, "void PicaCore::LoadVertices(")
    need(load.count("native_input_recipe_invocations[") == 1, "accounting is not once per draw")
    hot = load[load.index("            u64 escaped_input_vertices = 0;"):
               load.index("            // AstraPro: Separate recovered invocation coverage")]
    need("native_input_recipe_invocations" not in hot and "RecipeSlot" not in hot,
         "recipe accounting entered per-input shader/FIFO work")
    start = core.index("            if (input_plan.Ready()) {",
                       core.index("// AstraEH: Count actual misses using each transport"))
    end = core.index("            ++native_vertex_batches;", start)
    aggregate = core[start:end]
    at = core.index('"Uberhar vertex input {}:')
    start = core.rfind("    LOG_INFO_WITH_DELIVERY(", 0, at)
    end = core.index(");", at) + 2
    report = core[start:end]
    need(report.count("native_input_recipe_invocations[") == 5, "missing report consumer")

    # CodexAstraLocal: Derive the forced-generic reference from this exact header.
    # Existing public default-false Prepare is used by reference callsites; no
    # prior checkout, retained game payload or private historical header is needed.
    candidate = replace_once(header, "private:\n", "private:\n    friend class InputPlanInspector;\n")
    reference = candidate.replace("NativeVertexInputPlan", "NativeVertexInputPlanReference")
    attribute = block(reference, "struct NativeInputAttribute") + ";"
    reference = replace_once(reference, attribute, "")
    baseline_aggregate = aggregate.replace(
        "                native_input_recipe_invocations[input_plan.RecipeSlot()] += prepared_invocations;\n", "")
    need(baseline_aggregate != aggregate, "reference accounting seam changed")
    hardware_fields = block(raster_header, "    struct HardwareVertex") + ";\n"
    hardware = "\n\n".join(block(raster, anchor) for anchor in (
        "RasterizerAccelerated::HardwareVertex::HardwareVertex(",
        "static bool AreQuaternionsOpposite(", "void RasterizerAccelerated::AddTriangle("))
    hardware = hardware.replace("RasterizerAccelerated::", "Sink::")

    def write(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    for name, text in {
        "include/video_core/pica/uberhar_vertex_input.h": candidate,
        "include/core/memory.h": MEMORY, "include/common/settings.h": SETTINGS,
        "reference_plan.h": reference, "inspector.h": INSPECTOR,
        "hardware-fields.inc": hardware_fields, "hardware-bodies.inc": hardware,
        "candidate-account.inc": aggregate, "baseline-account.inc": baseline_aggregate,
    }.items():
        write(run / "runner" / name, text)

    compiler = os.environ.get("CXX", "c++")
    common = [compiler, "-O3", "-DNDEBUG", "-std=gnu++20", "-DMICROPROFILE_ENABLED=0",
              "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL", "-msse4.1", "-msse4.2"]
    includes = ["-Isrc", "-Iexternals/fmt/include", "-Iexternals/boost", "-Iexternals/xxHash",
                "-isystem", "externals/nihstro/include", "-Iexternals/microprofile",
                "-Iexternals/xbyak", "-Iexternals/json"]
    cpp_paths = [root / "src/video_core" / name for name in (
        "pica/output_vertex.cpp", "pica/primitive_assembly.cpp", "pica/shader_unit.cpp",
        "pica/shader_setup.cpp", "shader/shader.cpp", "shader/shader_jit.cpp",
        "shader/shader_interpreter.cpp", "shader/shader_jit_x64_compiler.cpp", "pica/vertex_loader.cpp")]
    for path in cpp_paths:
        read(path)
    for name in ("uberhar_vertex_output.h", "uberhar_vertex_plan_cache.h", "uberhar_vertex_cache.h",
                 "vertex_loader.h", "regs_pipeline.h", "shader_unit.h"):
        read(root / "src/video_core/pica" / name)
    rows = []

    # CodexAstraLocal: A mutant must compile and then fail its named runtime
    # contract. Every build/run has a finite deadline and create-only artifact dir.
    def execute(name, command, arguments, expected=None):
        folder = run / name
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "commands.json").write_text(json.dumps(
            dict(compile_argv=command, run_argv=arguments, required_failure=expected), indent=2) + "\n")
        built = subprocess.run(command, cwd=root, capture_output=True, timeout=180)
        (folder / "compile.log").write_bytes(built.stdout + built.stderr)
        built.check_returncode()
        result = subprocess.run(arguments, cwd=root, capture_output=True, timeout=60)
        text = (result.stdout + result.stderr).decode()
        (folder / "execute.log").write_text(text)
        passed = result.returncode == 0 if expected is None else result.returncode != 0 and expected in text
        rows.append(dict(name=name, compile_argv=command, run_argv=arguments, exit=result.returncode,
                         required_failure=expected, passed=passed, output=text,
                         binary_sha256=digest(arguments[0])))
        (run / "progress.json").write_text(json.dumps(
            dict(author="CodexAstraLocal", source_sha256=sources, rows=rows), indent=2) + "\n")
        print(text.strip(), flush=True)
        need(passed, "runtime gate failed: " + name)

    folder = run / "runner"
    binary = folder / "test"
    command = common + [f"-I{folder / 'include'}", f"-I{folder}"] + includes
    command += [str(runner)] + list(map(str, cpp_paths)) + ["-o", str(binary)]
    execute("runner", command, [str(binary), str(folder / "report.json")])

    variants = [("accounting", header, aggregate, report, None)]
    if args.mutants:
        variants += [
            ("stale_pointer", replace_once(header, "        recipe = nullptr;\n", ""), aggregate, report,
             "disabled prepare retained stale recipe"),
            ("ignored_default", replace_once(header, "ops[index].is_default || ", ""), aggregate, report,
             "default invalidates tag"),
            ("missing_w", replace_once(header, "component == 3 ? f24::One() : f24::Zero()", "f24::Zero()"),
             aggregate, report, "conversion/W fill"),
            ("wrong_live_offset", replace_once(header,
                "op.data + static_cast<std::size_t>(op.stride) * vertex);\n    }\n    template <class... E>",
                "op.data);\n    }\n    template <class... E>"), aggregate, report, "live vertex offset"),
            ("stale_tag", replace_once(header, "        recipe_slot = 0;\n", ""), aggregate, report,
             "default invalidates tag"),
            ("wrong_tag", replace_once(header, "recipe_slot = 3;", "recipe_slot = 4;"), aggregate, report,
             "slot tracks actual successful preparation"),
            ("missing_bucket", header, replace_once(aggregate,
                "native_input_recipe_invocations[input_plan.RecipeSlot()] += prepared_invocations;",
                "native_input_recipe_invocations[input_plan.RecipeSlot()] += 0;"), report,
             "actual prepared invocations only"),
            ("included_escapes", header, replace_once(aggregate,
                "native_input_recipe_invocations[input_plan.RecipeSlot()] += prepared_invocations;",
                "native_input_recipe_invocations[input_plan.RecipeSlot()] += counts.invocations;"), report,
             "actual prepared invocations only"),
            ("recognition_as_use", header, replace_once(aggregate,
                "native_input_recipe_invocations[input_plan.RecipeSlot()] += prepared_invocations;",
                "native_input_recipe_invocations[input_plan.RecipeSlot()] += 1;"), report,
             "actual prepared invocations only"),
            ("legacy_in_bucket", header, replace_once(aggregate,
                "native_input_legacy_vertices += counts.invocations;",
                "native_input_legacy_vertices += counts.invocations; native_input_recipe_invocations[0] += counts.invocations;"),
             report, "unready legacy is outside recipe denominator"),
            ("missing_report_field", header, aggregate,
             replace_once(report, "recipe4_vertices=", "unreported4_vertices="), "all five report consumers"),
        ]
    for name, modified_header, account_body, report_body, expected in variants:
        folder = run / name
        write(folder / "include/video_core/pica/uberhar_vertex_input.h", modified_header)
        write(folder / "account.inc", account_body)
        write(folder / "report.inc", report_body)
        binary = folder / "test"
        command = common + [f"-I{folder / 'include'}", f"-I{folder}"] + includes
        command += [str(accounting), str(root / "src/video_core/pica/shader_unit.cpp"), "-o", str(binary)]
        execute(name, command, [str(binary)], expected)

    for path, expected in sources.items():
        need(digest(path) == expected, "input changed during gate: " + path)
    proof = dict(author="CodexAstraLocal", source_sha256=sources, rows=rows,
                 compiler=subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
                 generated_sha256={str(p.relative_to(run)): digest(p) for p in run.rglob("*") if p.is_file()},
                 scope="synthetic software correctness; same-source generic and actual legacy-input references; no target timing")
    (run / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
    print("PASS input recipes; provenance: " + str(run / "provenance.json"))


if __name__ == "__main__":
    main()

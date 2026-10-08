#!/usr/bin/env python3
"""CodexAstraLocal: Execute CPU-ready fragment routing and queued binding.

Production selection, key, reset and queued command bodies are extracted without
rewriting their decisions. GPU compilation, task scheduling and command emission
use explicit recording endpoints; this test cannot establish driver/image parity.
Real worker completion and CPU-output/shader execution have complementary gates.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess


# CodexAstraLocal: Balanced extraction rejects missing/ambiguous source rather than
# silently compiling a second implementation of selection or a neighboring job.
def function(source: str, signature: str) -> str:
    if source.count(signature) != 1:
        raise ValueError(f"Expected exactly one function: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth, state, index = 0, "code", opening
    while index < len(source):
        char, pair = source[index], source[index:index + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if pair == "*/":
                state = "code"
                index += 1
        elif state in ('"', "'"):
            if char == "\\":
                index += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            index += 1
        elif pair == "/*":
            state = "block"
            index += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
        index += 1
    raise ValueError(f"Unterminated function: {signature}")


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"Mutation/extraction anchor changed: {old}")
    return source.replace(old, new)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# CodexAstraLocal: These endpoints deliberately expose a delayed queue and one
# frontend mode. No production worker or settings implementation is replaced.
WORKER = r'''#pragma once
#include <deque>
#include <functional>
#include <stdexcept>
#include <string_view>
namespace Common {
class ThreadWorker {
public:
    std::deque<std::function<void()>> tasks;
    bool throw_queue{};
    unsigned scheduled{}, executed{}, drains{};
    ThreadWorker(unsigned = 1, std::string_view = {}) {}
    template <class F> void QueueWork(F&& task) {
        if (throw_queue) { throw_queue = false; throw std::bad_alloc{}; }
        tasks.emplace_back(std::forward<F>(task));
        ++scheduled;
    }
    void WaitForRequests() {
        ++drains;
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
            ++executed;
        }
    }
};
}
'''
SETTINGS = r'''#pragma once
namespace Settings {
enum class UberharTestMode {Custom, Native, Compute, Automatic, ComboGeneric};
struct Mode {
    UberharTestMode value{UberharTestMode::Automatic};
    auto GetValue() const { return value; }
};
inline struct Values { Mode uberhar_test_mode; } values;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--source-root", type=Path,
                        help="Optional isolated candidate source tree; unchanged files use repo root")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--mutants", action="store_true",
                        help="Also require deliberate key/state/cap/selection violations to fail")
    args = parser.parse_args()
    root = args.repo_root.resolve()
    source_root = (args.source_root or root).resolve()
    output = (args.output or root / "build/uberhar-probe/ready-cpu-fragments").resolve()
    output.mkdir(parents=True, exist_ok=True)
    inputs = {}

    def read(relative: str) -> str:
        path = source_root / relative
        if not path.exists():
            path = root / relative
        inputs[str(path)] = digest(path)
        return path.read_text()

    pipeline = read("src/video_core/renderer_vulkan/vk_pipeline_cache.cpp")
    header = read("src/video_core/renderer_vulkan/vk_pipeline_cache.h")
    cache = read("src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp")
    graphics = read("src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp")
    rasterizer = read("src/video_core/renderer_vulkan/vk_rasterizer.cpp")
    parts = [function(graphics, "u64 GraphicsPipeline::Key()").replace(
        "GraphicsPipeline::Key()", "GraphicsPipeline::ActualKey()"),
        function(graphics, "bool GraphicsPipeline::MatchesExecution(")]
    for signature in (
        "bool PipelineCache::PreferReadySpecializedFragment(",
        "bool PipelineCache::ReadyVertexShaders()",
        "GraphicsPipeline* PipelineCache::PrepareReadyCpuFragment(",
        "GraphicsPipeline* PipelineCache::PrepareReadyGpuVertex(",
        "void PipelineCache::UseFragmentShader(",
        "bool PipelineCache::BindPipeline(",
        "void PipelineCache::ClearTevFallbacks()",
    ):
        parts.append(function(pipeline, signature))
    parts.append(function(cache, "std::optional<std::pair<u64, Shader* const>> "
                          "ShaderDiskCache::UseReadyFragmentShader("))
    body = "\n\n".join(parts)
    # CodexAstraLocal: Only the command endpoint type and collision-injectable Key
    # name change; all lookup/bind/worker decisions and captured values stay real.
    body = replace_once(body, "vk::CommandBuffer cmdbuf", "RecordingCommands cmdbuf")
    layout = function(rasterizer, "void RasterizerVulkan::MakeSoftwareVertexLayout()")
    (output / "layout.inc").write_text(layout[layout.index("{") + 1:-1] + "\n")
    limits = re.findall(r"static constexpr std::size_t MaxReadyCpuPipelines = ([0-9]+);", header)
    if len(limits) != 1:
        raise ValueError("CPU bank bound is missing or ambiguous")
    (output / "ready_cpu_limits.inc").write_text(
        f"static constexpr std::size_t MaxReadyCpuPipelines = {limits[0]};\n")
    stubs = output / "stubs/common"
    stubs.mkdir(parents=True, exist_ok=True)
    (stubs / "thread_worker.h").write_text(WORKER)
    (stubs / "settings.h").write_text(SETTINGS)
    fixture = Path(__file__).with_suffix(".cpp")
    inputs[str(fixture)] = digest(fixture)
    inputs[str(Path(__file__))] = digest(Path(__file__))

    includes = [output / "stubs", output, root / "src"] + [root / "externals" / item for item in (
        "fmt/include", "boost", "xxHash", "nihstro/include", "vulkan-headers/include")]
    flags = ["c++", "-std=c++20", "-O2", "-pthread", "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL",
             *[f"-I{path}" for path in includes]]
    provenance = {
        "author": "CodexAstraLocal", "source_root": str(source_root), "repo_root": str(root),
        "scope": "Actual extracted selection/key/reset/queued BindPipeline/optional-FS functions; "
                 "real state, AsyncHandle, FS config/demand and GLSL generator; modeled "
                 "queue, compiler/driver and command endpoints. No GPU or device execution.",
        "adaptations": "Command-buffer parameter becomes recorder; Key body renamed ActualKey "
                       "with explicit forced-collision wrapper. Unique fake PSO handles retained.",
        "inputs": inputs, "generator_objects": [], "cases": [],
    }

    def save() -> None:
        provenance["utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")

    # CodexAstraLocal: Build the real generators from source once. This gate does
    # not borrow a previous native object or trust its unrecorded compiler flags.
    objects = []
    for name in ("pica_fs_config", "glsl_fs_shader_gen"):
        source = root / f"src/video_core/shader/generator/{name}.cpp"
        inputs[str(source)] = digest(source)
        target = output / f"{name}.o"
        command = [*flags, "-c", str(source), "-o", str(target)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        (output / f"{name}.compile.log").write_text(result.stdout + result.stderr)
        row = {"argv": command, "returncode": result.returncode}
        provenance["generator_objects"].append(row)
        save()
        if result.returncode:
            raise RuntimeError(f"Generator compile failed: {name}; see {output}")
        row["sha256"] = digest(target)
        objects.append(target)

    # CodexAstraLocal: These single-defect mutants must compile, then fail the
    # intended assertion. A syntax error or arbitrary crash is not credited.
    cases = [("normal", body, None)]
    if args.mutants:
        mutations = [
            ("wrong-selected-pipeline", "pipeline = ready;", "pipeline = generic;",
             "actual G/S/G pipeline identity recorded"),
            ("hash-only", "ready->Key() != key || !ready->MatchesExecution(candidate, owners)",
             "ready->Key() != key", "actual map bucket with changed active layout rejected"),
            ("missing-software-layout", "!info.state.ExecutionEquals(expected, true)", "false",
             "ABI reject before optional lookup"),
            ("stale-generic-constants", "tev_push_constants.Invalidate();", "(void)constants_dirty;",
             "specialization dirty invalidates same generic constants"),
            ("cpu-omitted-from-gpu-cap",
             "ReadyVertexPolicy::CanQueue(ready_vertex_pipelines.size() + ready_cpu_pipelines.size(), pending)",
             "ReadyVertexPolicy::CanQueue(ready_vertex_pipelines.size(), pending)",
             "combined256 has no new GPU owner/task"),
            ("specialized-generic-push", "const bool selected_fallback = using_fallback || selected == alternative;",
             "const bool selected_fallback = true;", "only generic draws push constants"),
        ]
        for name, old, new, expected in mutations:
            cases.append((name, replace_once(body, old, new), expected))

    for name, code, expected in cases:
        include = output / f"{name}.inc"
        include.write_text(code + "\n")
        binary = output / name
        command = [*flags, f'-DUBERHAR_CPU_FRAGMENT_FUNCTIONS="{include.name}"', str(fixture),
                   *map(str, objects), "-o", str(binary)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        (output / f"{name}.compile.log").write_text(result.stdout + result.stderr)
        record = {"name": name, "argv": command, "compile_returncode": result.returncode,
                  "function_sha256": digest(include), "expected_failure": expected}
        provenance["cases"].append(record)
        save()
        if result.returncode:
            raise RuntimeError(f"Fixture/mutant must compile: {name}; see {output}")
        run = subprocess.run([str(binary)], cwd=root, capture_output=True, text=True, timeout=45)
        (output / f"{name}.run.log").write_text(run.stdout + run.stderr)
        record.update(binary_sha256=digest(binary), run_returncode=run.returncode,
                      stdout=run.stdout, stderr=run.stderr)
        passed = (run.returncode == 0 and run.stdout.startswith("PASS checks=")) if expected is None else (
            run.returncode == 1 and f"FAIL {expected} checks=" in run.stderr)
        record["expected_outcome_observed"] = passed
        save()
        if not passed:
            raise RuntimeError(f"Unexpected {name} result: {run.returncode}: {run.stdout}{run.stderr}")
        print(f"{name}: {run.stdout.strip() if expected is None else 'expected assertion failed'}", flush=True)

    # CodexAstraLocal: Hash concrete helper dependencies too, without pretending
    # this list is a compiler-generated transitive include dependency manifest.
    for relative in (
        "src/common/async_handle.h", "src/common/uberhar_activity.h",
        "src/video_core/renderer_vulkan/vk_graphics_pipeline.h",
        "src/video_core/renderer_vulkan/uberhar_pipeline_policy.h",
        "src/video_core/renderer_vulkan/uberhar_fragment_policy.h",
        "src/video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h",
        "src/video_core/renderer_vulkan/uberhar_push_constants.h",
        "src/video_core/renderer_vulkan/uberhar_shader_compile_policy.h",
        "src/video_core/shader/generator/profile.h",
        "src/video_core/shader/generator/pica_fs_config.h",
    ):
        inputs[str(root / relative)] = digest(root / relative)
    save()


if __name__ == "__main__":
    main()

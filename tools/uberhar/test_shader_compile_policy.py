#!/usr/bin/env python3
"""CodexAstraUlt: Execute the production queued VS/GS/optional-FS jobs.

The compiler/device are recording doubles: this checks routing, immutable compiler
inputs and success/failure publication, not driver memory or shader numerical output.
The separate generated-shader Vulkan validation/output fixtures cover real compilers.
"""
import argparse
from pathlib import Path
import subprocess


# CodexAstraLocal: Extract the real queued job and its frozen option preparation;
# an older source can therefore fail the same behavioral policy assertions.
def job(source: str, signature: str, options: bool) -> str:
    start = source.index(signature)
    # CodexAstraLocal: These methods have an unindented outer close. Bound both
    # searches to it so a removed queue cannot silently select another method.
    method_end = source.index("\n}\n", start)
    # CodexAstraLocal: Full optional fragments now share the optional lane.
    # Extract either recognized lane so the executable assertions, rather than
    # the extractor, reject a job routed back onto the mandatory worker.
    queues = []
    for call in ("parent.shader_workers.QueueWork(",
                 "parent.ready_vertex_worker->QueueWork("):
        cursor = start
        while (cursor := source.find(call, cursor, method_end)) >= 0:
            queues.append(cursor)
            cursor += len(call)
    if len(queues) != 1:
        raise ValueError("Expected exactly one recognized shader-worker queue call")
    queue = queues[0]
    # CodexAstraLocal: Optional queue admission now has an outer failure boundary.
    # Derive its indentation so both old and contained job bodies stay testable;
    # refuse an unexpected inline call rather than extracting a later method.
    indent = source[source.rfind("\n", 0, queue) + 1:queue]
    if not indent or indent.strip():
        raise ValueError("Expected a standalone indented shader-worker queue call")
    end = source.index("\n" + indent + "});", queue, method_end) + len(indent) + 5
    # CodexAstraUlt: Include the actual frozen-option preparation when present.
    # Accept the previous source too, so --source reproduces its behavioral failure.
    option = source.find("const bool disable_optimizer =", start, queue)
    return source[option if options and option >= 0 else queue:end]


# CodexAstraLocal: Recording compiler/device doubles defer execution explicitly
# and expose success/failure publication without claiming a real driver result.
HEADER = r'''
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "video_core/renderer_vulkan/uberhar_shader_compile_policy.h"
using u64 = unsigned long long;
using Vulkan::DisableShaderOptimizer;
#define LOG_ERROR(...) ((void)0)
namespace vk {
enum class ShaderStageFlagBits { eVertex, eFragment, eGeometry };
using Device = int;
}
struct Profile { bool vk_disable_spirv_optimizer{}, has_clip_planes{}; };
struct Shader {
    std::string program{"generated"}; int module{}; bool done{}, failed{};
    void MarkDone() { done = true; }
    void MarkFailed() { failed = true; done = true; }
};
struct Worker {
    std::vector<std::function<void()>> jobs;
    void QueueWork(std::function<void()> work) { jobs.push_back(std::move(work)); }
    void Drain() { for (auto& work : jobs) work(); jobs.clear(); }
};
struct ExtraFixedGSConfig { bool use_clip_planes{}, separable_shader{}; };
struct GSConfigEntry { static constexpr int EXPECTED_VERSION = 0; int version{}, gs_config{}; };
struct ReadyKey { int fs{}; Profile profile{}; };
struct Entry { ReadyKey key; Shader shader; };
struct CompilerRecord { vk::ShaderStageFlagBits stage; bool disabled; };
std::vector<CompilerRecord> records;
bool global_disable_optimizer{}, fail_compile{}, observed_clip{};
std::vector<unsigned> CompileGLSL(std::string_view, vk::ShaderStageFlagBits stage,
                               std::string_view = "", bool disabled = global_disable_optimizer) {
    records.push_back({stage, disabled});
    if (fail_compile) return {};
    return {1};
}
int CompileSPV(const std::vector<unsigned>& code, vk::Device) { return code.empty() ? 0 : 1; }
namespace GLSL {
std::string GenerateFixedGeometryShader(int, ExtraFixedGSConfig extra) {
    observed_clip = extra.use_clip_planes; return "generated";
}
std::string GenerateFragmentShader(int, int, Profile) { return "generated"; }
}
namespace VideoCore {
template<class Work, class Failure>
void CompleteShaderBuild(Shader& shader, Work work, Failure failure) {
    try { work(); shader.MarkDone(); }
    catch (const std::exception& error) { failure(error.what()); shader.MarkFailed(); }
}
}
struct Cache {
    struct Parent {
        Profile profile;
        struct { vk::Device GetDevice() const { return 1; } } instance;
        // CodexAstraLocal: Keep independently observable queues so optional
        // fragment work cannot silently fall back onto mandatory compilation.
        Worker shader_workers, optional_worker;
        Worker* ready_vertex_worker{&optional_worker};
    } parent;
    Shader output;
    Shader* warming_ready_vs{};
    Entry fragment;
    int vs_cache{}, gs_cache{}, writes{};
    std::atomic<unsigned> mandatory_shader_failures{}, ready_shader_failures{};
    std::atomic<u64> ready_fragment_compile_ns{}, ready_fragment_max_compile_ns{},
        ready_fragment_builds{}, ready_fragment_failures{};
    void AppendVSSPIRV(int, const std::vector<unsigned>&, u64) { ++writes; }
    void AppendGSSPIRV(int, const std::vector<unsigned>&, u64) { ++writes; }
    void AppendGSConfig(int, const GSConfigEntry&, u64) { ++writes; }
    void Vertex(bool ready_only) {
        auto& shader = output; const vk::Device device = 1; const u64 spirv_id = 7;
'''

# CodexAstraLocal: Exercise all stages, optional/required routes, optimizer values,
# compile outcomes and intervening setting changes through the extracted jobs.
TESTS = r'''
};
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
// CodexAstraLocal: A failed assertion returns its exact diagnostic for the
// intended-defect controls, without an abort or an unqualified timeout.
int main() try {
    unsigned checks = 0;
    for (bool change_options : {false, true}) {
    for (bool fail : {false, true}) {
        for (bool requested : {false, true}) {
            for (bool ready : {false, true}) {
                for (int stage : {0, 1, 2}) {
                    Cache cache;
                    cache.parent.profile = {requested, true};
                    cache.fragment.key.profile = cache.parent.profile;
                    global_disable_optimizer = requested;
                    fail_compile = fail;
                    records.clear();
                    observed_clip = false;
                    if (stage == 0) cache.Vertex(ready);
                    if (stage == 1) cache.Geometry(ready);
                    if (stage == 2) cache.Fragment();
                    auto& shader = stage == 2 ? cache.fragment.shader : cache.output;
                    Check(!shader.done,
                          "Optional/required work must remain unpublished until the queued job runs");
                    Check(cache.parent.shader_workers.jobs.size() == (stage == 2 ? 0 : 1) &&
                          cache.parent.optional_worker.jobs.size() == (stage == 2 ? 1 : 0),
                          "Production compiler job used the wrong worker lane");
                    // CodexAstraUlt: A later UI/global/profile change must not alter this job.
                    if (change_options) {
                        global_disable_optimizer = !requested;
                        cache.parent.profile = {!requested, false};
                    }
                    cache.parent.shader_workers.Drain();
                    cache.parent.optional_worker.Drain();
                    const bool optional = stage == 2 || ready;
                    Check(records.size() == 1 && records[0].disabled == (!optional && requested),
                          "Production compiler job did not preserve the optional/required policy snapshot");
                    const auto expected_stage = stage == 0 ? vk::ShaderStageFlagBits::eVertex :
                        stage == 1 ? vk::ShaderStageFlagBits::eGeometry : vk::ShaderStageFlagBits::eFragment;
                    Check(records[0].stage == expected_stage, "Compiler stage changed");
                    Check(shader.done && shader.failed == fail && bool(shader.module) == !fail,
                          "Compiler failure/success was not published correctly");
                    if (stage == 1)
                        Check(observed_clip, "Queued geometry job reread a mutable clip profile");
                    if (stage == 2)
                        Check(cache.ready_fragment_builds == 1, "Optional fragment completion was not counted");
                    ++checks;
                }
            }
        }
    }
    }
    std::printf("PASS: %u production queued compiler cases preserve policy, snapshots and completion\n", checks);
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
'''


# CodexAstraLocal: Defects change one actual extracted fragment job statement;
# a stale source anchor must fail rather than produce an ineffective control.
def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise ValueError(f"Expected one defect anchor: {before}")
    return source.replace(before, after, 1)


# CodexAstraLocal: Preserve compile/runtime diagnostics for the positive fixture
# and each sensitive defect. Only an executed, specifically named assertion is
# evidence that a defect was caught; compiler errors and timeouts never qualify.
def execute_fixture(cpp: str, output: Path, expected_failure: str | None) -> None:
    cpp_path = output.with_suffix(".cpp")
    cpp_path.write_text(cpp)
    compiled = subprocess.run(["c++", "-std=c++20", "-O2", "-Isrc", str(cpp_path),
                               "-o", str(output)], capture_output=True, text=True, timeout=90)
    output.with_suffix(".compile.stdout").write_text(compiled.stdout)
    output.with_suffix(".compile.stderr").write_text(compiled.stderr)
    if compiled.returncode:
        raise RuntimeError(f"{output.name} compile failed:\n{compiled.stderr[-8000:]}")
    executed = subprocess.run([str(output)], capture_output=True, text=True, timeout=20)
    output.with_suffix(".stdout").write_text(executed.stdout)
    output.with_suffix(".stderr").write_text(executed.stderr)
    if expected_failure is None:
        if executed.returncode:
            raise RuntimeError(f"{output.name} failed:\n{executed.stderr[-8000:]}")
        print(executed.stdout, end="")
    elif executed.returncode != 1 or executed.stderr.strip() != expected_failure:
        raise RuntimeError(f"{output.name} did not expose the intended defect:\n"
                           f"rc={executed.returncode}\n{executed.stderr[-8000:]}")
    else:
        print(f"PASS: {output.name}: rejected {expected_failure}")


# CodexAstraLocal: Compile those production job bodies into the host fixture and
# fail on compiler or assertion errors; generated shader validation is a separate gate.
def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/test-shader-compile-policy"))
    args = parser.parse_args()
    source = args.source.read_text()
    # CodexAstraLocal: Extract each actual queue nesting without fixing its
    # indentation to a particular source revision or exception boundary.
    vertex = job(source, "ShaderDiskCache::UseProgrammableVertexShader(", True)
    geometry = job(source, "ShaderDiskCache::UseFixedGeometryShader(", True)
    fragment = job(source, "ShaderDiskCache::UseReadyFragmentShader(", False)
    # CodexAstraLocal: Retain the original 48-case matrix, and require actual
    # wrong-lane and unoptimized-optional jobs to fail its behavioral assertions.
    variants = [("", fragment, None),
                ("-wrong-worker", replace_once(fragment,
                    "parent.ready_vertex_worker->QueueWork(", "parent.shader_workers.QueueWork("),
                 "Production compiler job used the wrong worker lane"),
                ("-wrong-frontend", replace_once(fragment,
                    "DisableShaderOptimizer(true, profile.vk_disable_spirv_optimizer != 0)",
                    "DisableShaderOptimizer(false, profile.vk_disable_spirv_optimizer != 0)"),
                 "Production compiler job did not preserve the optional/required policy snapshot")]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for suffix, fragment_job, expected_failure in variants:
        cpp = HEADER + vertex + r'''
    }
    void Geometry(bool ready_only) {
        auto& shader = output; const int gs_config = 0; const u64 gs_config_hash = 9;
''' + geometry + r'''
    }
    void Fragment() {
        auto* entry = &fragment; const vk::Device device = 1;
''' + fragment_job + "\n    }\n" + TESTS
        execute_fixture(cpp, args.output.with_name(args.output.name + suffix), expected_failure)


# CodexAstraLocal: Keep direct invocation suitable for the existing probe gate.
if __name__ == "__main__":
    main()

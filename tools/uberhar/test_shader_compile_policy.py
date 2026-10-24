#!/usr/bin/env python3
"""CodexAstraUlt: Execute the production queued VS/GS/optional-FS jobs.

The compiler/device are recording doubles: this checks routing, immutable compiler
inputs and success/failure publication, not driver memory or shader numerical output.
The separate generated-shader Vulkan validation/output fixtures cover GLSL compilers;
direct SPIR-V endpoint coverage here does not establish actual direct-module pixels.
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
    # CodexAstraLocal: Scan the actual call parentheses so outer try/catch
    # containment and nested completion lambdas cannot alter the extraction.
    opening = source.index("(", queue)
    depth, state, cursor = 0, "code", opening
    while cursor < method_end:
        char, pair = source[cursor], source[cursor:cursor + 2]
        if state == "line":
            if char == "\n": state = "code"
        elif state == "block":
            if pair == "*/": state = "code"; cursor += 1
        elif state in ('"', "'"):
            if char == "\\": cursor += 1
            elif char == state: state = "code"
        elif pair == "//": state = "line"; cursor += 1
        elif pair == "/*": state = "block"; cursor += 1
        elif char in ('"', "'"): state = char
        elif char == "(": depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0: break
        cursor += 1
    if depth or source[cursor + 1] != ";":
        raise ValueError("Unterminated queued shader job")
    option = source.find("const bool disable_optimizer =", start, queue)
    preparation = source[option:source.rfind("\n", 0, queue)] if options and option >= 0 else ""
    return preparation + "\n" + source[queue:cursor + 2]


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
#include "common/common_types.h"
#include "video_core/renderer_vulkan/uberhar_compile_budget.h"
#include "video_core/renderer_vulkan/uberhar_shader_compile_policy.h"
#include "video_core/shader/generator/profile.h"
// CodexAstraLocal: Use the production value types so queued-job interface changes
// cannot remain hidden behind a duplicate profile or integer definition.
using Pica::Shader::Profile;
using Vulkan::DisableShaderOptimizer;
using Vulkan::CompileBudget;
#define LOG_ERROR(...) ((void)0)
namespace vk {
enum class ShaderStageFlagBits { eVertex, eFragment, eGeometry };
using Device = int;
}
// CodexAstraLocal: Name the tested fields explicitly; profile layout and unrelated
// capability additions must not change the cases through aggregate positions.
Profile MakeProfile(bool disable_optimizer, bool clip_planes, bool use_spirv = false) {
    Profile profile{};
    profile.vk_disable_spirv_optimizer = disable_optimizer;
    profile.has_clip_planes = clip_planes;
    profile.vk_use_spirv_generator = use_spirv;
    return profile;
}
// CodexAstraLocal: Model only the compatibility boundary used by this queued
// policy job; actual register-to-compatibility behavior has separate source tests.
struct FSConfig {
    bool incompatible{};
    bool UsesSpirvIncompatibleConfig() const { return incompatible; }
};
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
struct ReadyKey { FSConfig fs; Profile profile; };
// CodexAstraLocal: Queue policy tests model disk/device endpoints only; real
// credit, codec and retry ownership have their focused lifecycle fixtures.
enum class OptionalCompileFailure { Source, Resource, DeviceLost, Unknown };
struct OptionalCompileError : std::runtime_error {
    OptionalCompileFailure kind;
    OptionalCompileError(OptionalCompileFailure k, const char* why) : std::runtime_error(why), kind(k) {}
};
struct Entry {
    ReadyKey key; Shader shader;
    unsigned retries{}; bool retryable{}; u64 retry_after_ms{};
    std::atomic<bool> compiler_released{};
};
std::vector<u32> OptionalIdentity(const FSConfig&, const Profile&, const void*) { return {1}; }
struct CompilerRecord { vk::ShaderStageFlagBits stage; bool disabled; };
std::vector<CompilerRecord> records;
// CodexAstraLocal: Distinct endpoints expose effective selection and frozen
// generator inputs; they cannot both silently model a GLSL frontend invocation.
struct GeneratorRecord { bool direct, incompatible, requested; };
std::vector<GeneratorRecord> generators;
bool global_disable_optimizer{}, fail_compile{}, observed_clip{};
std::vector<unsigned> CompileGLSL(std::string_view, vk::ShaderStageFlagBits stage,
                               std::string_view = "", bool disabled = global_disable_optimizer) {
    records.push_back({stage, disabled});
    if (fail_compile) return {};
    return {1};
}
int CompileSPV(const std::vector<unsigned>& code, vk::Device) {
    return code.empty() ? 0 : static_cast<int>(code.front());
}
int CompileOptionalSPV(const std::vector<unsigned>& code, vk::Device device) {
    if (code.empty()) throw OptionalCompileError(OptionalCompileFailure::Source, "empty");
    return CompileSPV(code, device);
}
namespace GLSL {
std::string GenerateFixedGeometryShader(int, ExtraFixedGSConfig extra) {
    observed_clip = extra.use_clip_planes; return "generated";
}
std::string GenerateFragmentShader(FSConfig fs, int, Profile profile) {
    generators.push_back({false, fs.incompatible, profile.vk_use_spirv_generator != 0});
    return "generated";
}
}
namespace SPIRV {
std::vector<u32> GenerateFragmentShader(FSConfig fs, Profile profile) {
    generators.push_back({true, fs.incompatible, profile.vk_use_spirv_generator != 0});
    return fail_compile ? std::vector<u32>{} : std::vector<u32>{2};
}
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
    std::atomic<u64> ready_fragment_spirv_builds{}, ready_fragment_glsl_builds{};
    std::atomic<u64> optional_resource_failures{}, optional_source_failures{},
        optional_device_failures{}, optional_unknown_failures{}, optional_failure_streak{},
        optional_circuit_until_ms{};
    void ReportOptionalFailure(const char*, const char*, unsigned) noexcept {}
    template<class Work> auto LoadOrCompileOptional(const std::vector<u32>&, bool, Work work) {
        return work();
    }
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
                    cache.parent.profile = MakeProfile(requested, true);
                    cache.fragment.key.profile = cache.parent.profile;
                    global_disable_optimizer = requested;
                    fail_compile = fail;
                    records.clear();
                    generators.clear();
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
                        cache.parent.profile = MakeProfile(!requested, false);
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
    // CodexAstraLocal: Keep all 48 legacy policy cases above, then independently
    // cross generator request, compatibility, completion and intervening setting
    // mutation. The real queued body must use its owned entry, not live settings.
    for (bool requested_spirv : {false, true}) {
    for (bool incompatible : {false, true}) {
    for (bool change_options : {false, true}) {
    for (bool fail : {false, true}) {
        Cache cache;
        cache.parent.profile = MakeProfile(true, true, requested_spirv);
        cache.fragment.key = {{incompatible}, cache.parent.profile};
        global_disable_optimizer = true;
        fail_compile = fail;
        records.clear(); generators.clear();
        cache.Fragment();
        Check(!cache.fragment.shader.done && generators.empty() && records.empty(),
              "Full fragment generator ran or published before its queued job");
        Check(cache.parent.shader_workers.jobs.empty() &&
              cache.parent.optional_worker.jobs.size() == 1,
              "Production compiler job used the wrong worker lane");
        if (change_options) cache.parent.profile = MakeProfile(false, false, !requested_spirv);
        cache.parent.optional_worker.Drain();
        const bool direct = requested_spirv && !incompatible;
        Check(generators.size() == 1 && generators[0].direct == direct &&
              generators[0].incompatible == incompatible &&
              generators[0].requested == requested_spirv,
              "Full fragment generator choice or compatibility snapshot changed");
        Check(direct ? records.empty() : records.size() == 1 && !records[0].disabled &&
              records[0].stage == vk::ShaderStageFlagBits::eFragment,
              "Full fragment frontend policy does not match effective generator");
        const auto& shader = cache.fragment.shader;
        Check(shader.done && shader.failed == fail &&
              shader.module == (fail ? 0 : direct ? 2 : 1),
              "Full fragment generator result was not published correctly");
        Check(cache.ready_fragment_builds == 1 && cache.ready_fragment_failures == fail &&
              cache.ready_fragment_spirv_builds == direct &&
              cache.ready_fragment_glsl_builds == !direct,
              "Full fragment effective generator accounting changed");
        ++checks;
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
    # CodexAstraLocal: Retain the original 48 cases plus 16 generator cases, and require actual
    # wrong-lane and unoptimized-optional jobs to fail its behavioral assertions.
    variants = [("", fragment, None),
                ("-wrong-worker", replace_once(fragment,
                    "parent.ready_vertex_worker->QueueWork(", "parent.shader_workers.QueueWork("),
                 "Production compiler job used the wrong worker lane"),
                ("-wrong-frontend", replace_once(fragment,
                    "DisableShaderOptimizer(true, profile.vk_disable_spirv_optimizer != 0)",
                    "DisableShaderOptimizer(false, profile.vk_disable_spirv_optimizer != 0)"),
                 "Production compiler job did not preserve the optional/required policy snapshot")]
    # CodexAstraLocal: A forced legacy GLSL path, missing compatibility check or
    # late mutable-profile read must fail behavior, never merely compilation.
    guard = 'profile.vk_use_spirv_generator && !fs.UsesSpirvIncompatibleConfig()'
    for suffix, replacement in (
            ('-ignored-generator', 'false'),
            ('-ignored-compatibility', 'profile.vk_use_spirv_generator'),
            ('-mutable-generator',
             'parent.profile.vk_use_spirv_generator && !fs.UsesSpirvIncompatibleConfig()')):
        variants.append((suffix, replace_once(fragment, guard, replacement),
                         'Full fragment generator choice or compatibility snapshot changed'))
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

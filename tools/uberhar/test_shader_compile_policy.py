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
def job(source: str, signature: str, indent: str, options: bool) -> str:
    start = source.index(signature)
    queue = source.index("parent.shader_workers.QueueWork(", start)
    end = source.index("\n" + indent + "});", queue) + len(indent) + 5
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
        Worker shader_workers;
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
int main() {
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
                    Check(!shader.done && cache.parent.shader_workers.jobs.size() == 1,
                          "Optional/required work must remain unpublished until the queued job runs");
                    // CodexAstraUlt: A later UI/global/profile change must not alter this job.
                    if (change_options) {
                        global_disable_optimizer = !requested;
                        cache.parent.profile = {!requested, false};
                    }
                    cache.parent.shader_workers.Drain();
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
}
'''


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
    vertex = job(source, "ShaderDiskCache::UseProgrammableVertexShader(", "            ", True)
    geometry = job(source, "ShaderDiskCache::UseFixedGeometryShader(", "            ", True)
    fragment = job(source, "ShaderDiskCache::UseReadyFragmentShader(", "    ", False)
    cpp = HEADER + vertex + r'''
    }
    void Geometry(bool ready_only) {
        auto& shader = output; const int gs_config = 0; const u64 gs_config_hash = 9;
''' + geometry + r'''
    }
    void Fragment() {
        auto* entry = &fragment; const vk::Device device = 1;
''' + fragment + "\n    }\n" + TESTS
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cpp_path = args.output.with_suffix(".cpp")
    cpp_path.write_text(cpp)
    subprocess.run(["c++", "-std=c++20", "-O2", "-Isrc", str(cpp_path),
                    "-o", str(args.output)], check=True)
    subprocess.run([str(args.output)], check=True)


# CodexAstraLocal: Keep direct invocation suitable for the existing probe gate.
if __name__ == "__main__":
    main()

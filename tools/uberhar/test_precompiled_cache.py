#!/usr/bin/env python3
"""CodexAstraUlt-2: Compile the real preparation bank with instrumented API stand-ins.

This tests ownership, selection and scheduling policy without a Vulkan driver.
Recipe validation and generated shader output have independent production gates.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


# CodexAstraUlt-2: Only external dependencies are replaced. The bank implementation
# is copied unchanged; stand-ins track driver calls, terminal state and ownership.
STUBS = r'''
#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
using u64 = std::uint64_t;
namespace vk { using PipelineCache = int; using PipelineLayout = int; }
namespace Common {
inline u64 HashCombine(u64 a, u64 b) { return a * 0x9e3779b97f4a7c15ULL + b; }
}
namespace Pica::Shader {
struct FSConfig {
    unsigned id{};
    bool eligible{true};
    u64 Hash() const { return id % 4; }
    bool operator==(const FSConfig&) const = default;
};
}
namespace VideoCore {
enum class LoadCallbackStage { Build };
using DiskResourceLoadCallback = std::function<void(LoadCallbackStage,std::size_t,std::size_t,const std::string&)>;
}
namespace Vulkan {
inline constexpr std::size_t MAX_SHADER_STAGES = 3;
class Instance {
public:
    bool dynamic{true};
    bool IsExtendedDynamicStateSupported() const { return dynamic; }
};
class RenderManager {};
struct StaticPipelineInfo {
    std::array<u64,3> shader_ids{};
    unsigned state{}, inactive{};
    bool eligible{true};
    u64 ExecutionHash(bool dynamic, std::array<u64,3>) const {
        return Common::HashCombine(state, dynamic ? 0 : inactive);
    }
    bool ExecutionEquals(const StaticPipelineInfo& other, bool dynamic) const {
        return state == other.state && (dynamic || inactive == other.inactive);
    }
};
struct PipelineInfo { StaticPipelineInfo state; };
class Shader {
public:
    static inline unsigned live{}, peak{};
    static inline std::unordered_map<const Shader*,unsigned> references;
    bool done{true}, failed{};
    int handle{1};
    Shader() { ++live; if (live > peak) peak = live; }
    ~Shader() { assert(references[this] == 0); references.erase(this); --live; }
    bool IsDone() const { return done; }
    bool HasFailed() const { return failed; }
    int Handle() const { return handle; }
};
struct PipelineBuildOptions { void* completion; void* stats; bool fallback; bool recoverable; };
class GraphicsPipeline {
public:
    static inline unsigned builds{}, live{};
    static inline bool fail_next{}, lose_next{};
    bool done{}, failed{}, lost{};
    int handle{};
    std::array<Shader*,3> stages;
    GraphicsPipeline(const Instance&, RenderManager&, const PipelineInfo&, int cache,
                     int layout, std::array<Shader*,3> shaders, void* worker, PipelineBuildOptions options)
        : stages{shaders} {
        assert(cache == 7 && layout == 9 && worker == nullptr && options.recoverable);
        ++live;
        for (auto* shader : stages) if (shader) ++Shader::references[shader];
    }
    ~GraphicsPipeline() {
        for (auto* shader : stages) if (shader) --Shader::references[shader];
        --live;
    }
    bool Build() {
        ++builds;
        for (auto* shader : stages) if (shader)
            assert(shader->IsDone() && !shader->HasFailed() && shader->Handle());
        lost = lose_next;
        failed = fail_next || lost;
        fail_next = false;
        lose_next = false;
        done = true;
        handle = failed ? 0 : 1;
        return !failed;
    }
    bool IsDone() const { return done; }
    bool HasFailed() const { return failed; }
    bool DeviceLost() const { return lost; }
    int Handle() const { return handle; }
};
enum class OptionalCompileFailure { Source, Resource, DeviceLost, Unknown };
class OptionalCompileError : public std::runtime_error {
public:
    OptionalCompileFailure kind;
    OptionalCompileError(OptionalCompileFailure kind, const char* text) : std::runtime_error(text), kind{kind} {}
};
namespace Precompile {
inline constexpr std::size_t MaxRecipes = 8192;
struct Recipe { Pica::Shader::FSConfig config; StaticPipelineInfo pipeline; u64 uses; };
inline bool IsEligible(const Pica::Shader::FSConfig& config, const StaticPipelineInfo& state) {
    return config.eligible && state.eligible;
}
}
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    header = root / "src/video_core/renderer_vulkan/uberhar_precompiled_cache.h"
    source = (args.source or header).read_text()
    fixture = root / "tools/uberhar/test_precompiled_cache.cpp"
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise SystemExit(f"Compiler unavailable: {args.compiler}")
    with tempfile.TemporaryDirectory(prefix="uberhar-precompiled-bank-") as temporary:
        directory = Path(temporary)
        include = directory / "include"
        include.mkdir()
        (include / "dependencies.h").write_text(STUBS)
        for relative in (
            "video_core/rasterizer_interface.h",
            "video_core/renderer_vulkan/uberhar_precompile_recipes.h",
            "video_core/renderer_vulkan/vk_graphics_pipeline.h",
            "video_core/renderer_vulkan/vk_instance.h",
            "video_core/renderer_vulkan/vk_shader_util.h",
        ):
            path = include / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "dependencies.h"\n')
        production = include / "video_core/renderer_vulkan/uberhar_precompiled_cache.h"

        def execute(name, body, expected_success):
            production.write_text(body)
            binary = directory / name
            result = subprocess.run([compiler, "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                                     "-I", str(include), str(fixture), "-o", str(binary)],
                                    text=True, capture_output=True, timeout=60)
            if result.returncode:
                raise AssertionError(f"{name}: compilation failed, not a valid control\n{result.stderr}")
            result = subprocess.run([str(binary)], cwd=directory, text=True,
                                    capture_output=True, timeout=20)
            if (result.returncode == 0) != expected_success:
                raise AssertionError(f"{name}: unexpected outcome\n{result.stdout}\n{result.stderr}")
            if expected_success:
                print(result.stdout.strip())

        execute("production", source, True)
        # CodexAstraUlt-2: Deliberate behavioral defects must fail execution after
        # successful compilation, proving these checks reach the real bank code.
        mutations = {
            "missing-exact-fragment": ("entry.config == config &&", "true &&"),
            "ignored-cancellation": ("if (stop_loading.load(std::memory_order_relaxed))", "if (false)"),
            "shader-before-pipeline-destruction": (
                "for (auto& pipeline : resident_pipelines) pipeline.reset();\n        for (auto& module : resident_modules) module.reset();",
                "for (auto& module : resident_modules) module.reset();\n        for (auto& pipeline : resident_pipelines) pipeline.reset();"),
        }
        for name, (old, new) in mutations.items():
            if old not in source:
                raise AssertionError(f"{name}: mutation anchor missing")
            execute(name, source.replace(old, new), False)
        print("PASS three executed defect controls; no driver or gameplay performance claim")


if __name__ == "__main__":
    main()

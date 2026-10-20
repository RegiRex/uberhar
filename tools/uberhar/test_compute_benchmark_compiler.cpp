// CodexAstraLocal: Execute the scratch owner's extracted shader generation and
// explicit compiler call through the unchanged production CompileGLSL source.
// Only logging/settings and unused renderer endpoints are host boundaries;
// this test creates real SPIR-V without a Vulkan device or pipeline.
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <json.hpp>
#include "common/common_funcs.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/uberhar_compute_benchmark.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect_shader.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"

namespace Common::Log {
// CodexAstraLocal: Retain actual parser errors, including the rejected original
// macro definition; assertions and unexpected setting reads remain fatal.
void Stop() {}
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*,
                      fmt::string_view format, const fmt::format_args& args) {
    std::cerr << fmt::vformat(format, args) << '\n';
}
}

// CodexAstraLocal: Reuse the original CPU vertex constructor for fixed workload
// admission, while leaving unused renderer/command ownership to its existing gate.
#define LAYOUT_HASH static_cast<u64>(sizeof(T)), static_cast<u64>(alignof(T))
#define FIELD_HASH(x) static_cast<u64>(offsetof(T, x)), static_cast<u64>(sizeof(x))
namespace VideoCore {
using Pica::f24;
constexpr u32 MAX_VERTEX_BINDINGS = 13, MAX_VERTEX_ATTRIBUTES = 16;
#include "layout-types.inc"
struct RasterizerAccelerated {
#include "hardware-fields.inc"
    void AddTriangle(const Pica::OutputVertex&, const Pica::OutputVertex&,
                     const Pica::OutputVertex&);
    std::vector<HardwareVertex> vertex_batch;
};
#include "hardware-bodies.inc"
}

void Check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "one output directory required");
        const std::filesystem::path out{argv[1]};
        Check(std::filesystem::create_directory(out), "fresh compiler output required");
        using namespace Vulkan;
        using namespace Vulkan::ComputeBenchmarkData;
        using namespace Pica::Shader::Generator;
        using Vertex = VideoCore::RasterizerAccelerated::HardwareVertex;
        static_assert(sizeof(Vertex) == 88);
        nlohmann::json rows = nlohmann::json::array();
        // CodexAstraLocal: Both actual clip/logic interfaces must compile with
        // the mandatory optimizer-disabled policy before pixels can qualify them.
        for (u32 profile_id = 0; profile_id < 4; ++profile_id) {
            Pica::Shader::Profile profile{};
            profile.is_vulkan = profile.has_separable_shaders = profile.enable_accurate_mul = true;
            profile.has_clip_planes = profile_id & 1;
            profile.has_logic_op = !(profile_id & 2);
            Workload<Vertex> workload;
            GLSL::DynamicTevState fragment_state{};
#include "benchmark-shader-preparation.inc"
            Check(sources.size() == 3 && stages.size() == 3, "three production stages required");
            for (u32 i = 0; i < sources.size(); ++i) {
                // CodexAstraLocal: This exact statement is also compiled with
                // the original bad preamble; that executable must fail here.
#include "benchmark-compile-call.inc"
                Check(words.size() >= 5 && words[0] == 0x07230203 &&
                          words[1] == 0x00010300,
                      "production compiler returned no SPIR-V 1.3 module");
                const auto name = std::to_string(profile_id) + "-" + std::to_string(i);
                std::ofstream source(out / (name + ".glsl"));
                source << sources[i];
                Check(source.good(), "source output failed");
                std::ofstream module(out / (name + ".spv"), std::ios::binary);
                module.write(reinterpret_cast<const char*>(words.data()),
                             static_cast<std::streamsize>(words.size() * sizeof(u32)));
                Check(module.good(), "module output failed");
                rows.push_back({{"profile", profile_id}, {"stage", i},
                                {"source_bytes", sources[i].size()}, {"module_words", words.size()}});
            }
        }
        Check(rows.size() == 12, "all profiles and stages required");
        std::ofstream report(out / "report.json");
        report << rows.dump(2) << '\n';
        Check(report.good(), "compiler report output failed");
        std::cout << "PASS actual production compiler: 12 modules across four profiles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}

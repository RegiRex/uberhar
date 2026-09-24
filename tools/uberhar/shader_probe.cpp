// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

// Standalone generator probe. It links the production generator and replaces
// only its logging sink, so errors abort rather than disappearing into a log.
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include "common/logging/log.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) {
        throw std::runtime_error(fmt::vformat(format, args));
    }
}
} // namespace Common::Log

int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const std::filesystem::path output{argv[1]};
    std::filesystem::create_directories(output);
    using namespace Pica::Shader;
    using Tev = Pica::TexturingRegs::TevStageConfig;
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    FSConfig base{regs};
    const UserConfig user{};
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_custom_border_color = true;
    profile.has_logic_op = true;
    std::mt19937 random{0x55424552};
    constexpr std::array<u32, 10> sources{0, 1, 2, 3, 4, 5, 6, 13, 14, 15};
    constexpr std::array<u32, 10> modifiers{0, 1, 2, 3, 4, 5, 8, 9, 12, 13};
    constexpr std::array<u32, 8> alpha_ops{0, 1, 2, 3, 4, 5, 8, 9};
    for (u32 test = 0; test < 192; ++test) {
        auto config = base;
        config.texture.combiner_buffer_input.Assign(test < 16 ? test : random() & 255);
        for (auto& stage : config.texture.tev_stages) {
            stage = {};
            for (u32 i = 0; i < 3; ++i) {
                stage.sources_raw |= sources[random() % sources.size()] << (i * 4);
                stage.sources_raw |= sources[random() % sources.size()] << (16 + i * 4);
                stage.modifiers_raw |= modifiers[random() % modifiers.size()] << (i * 4);
                stage.modifiers_raw |= (random() % 8) << (12 + i * 4);
            }
            stage.ops_raw = random() % 10 | (alpha_ops[random() % alpha_ops.size()] << 16);
            stage.scales_raw = random() % 4 | ((random() % 4) << 16);
        }
        // Directed coverage: stage-0 Previous redirection and passthrough,
        // scale=3 meaning 1, DOT3_RGBA alpha, and all buffer-mask combinations.
        if (test < 16) {
            config.texture.tev_stages[0] = {0x000f000f, 0, 0, test & 1 ? 0x00030003U : 0U};
            config.texture.tev_stages[1].ops_raw = 7;
            config.texture.tev_stages[1].scales_raw = (test % 4) << 16;
        }
        const auto prefix = output / std::to_string(test);
        std::ofstream(prefix.string() + ".frag")
            << "#version 450\n"
            << Generator::GLSL::FragmentModule{config, user, profile}.Generate();
        std::ofstream constants(prefix.string() + ".bin", std::ios::binary);
        constants.write(reinterpret_cast<const char*>(config.texture.tev_stages.data()), 96);
        const u32 mask = config.texture.combiner_buffer_input;
        constants.write(reinterpret_cast<const char*>(&mask), 4);
    }
    // Full fragment modules exercise Vulkan bindings and code outside TEV too.
    for (u32 test = 0; test < 16; ++test) {
        auto config = base;
        constexpr std::array types{Pica::TexturingRegs::TextureConfig::Texture2D,
                                   Pica::TexturingRegs::TextureConfig::TextureCube,
                                   Pica::TexturingRegs::TextureConfig::Disabled,
                                   Pica::TexturingRegs::TextureConfig::Projection2D};
        config.texture.texture0_type.Assign(types[test % 4]);
        config.texture.fog_mode.Assign(test & 4 ? Pica::TexturingRegs::FogMode::Fog
                                               : Pica::TexturingRegs::FogMode::None);
        config.framebuffer.alpha_test_func.Assign(
            test & 8 ? Pica::FramebufferRegs::CompareFunc::LessThan
                     : Pica::FramebufferRegs::CompareFunc::Always);
        std::ofstream(output / fmt::format("dynamic-{}.frag", test))
            << "#version 450\n"
            << Generator::GLSL::FragmentModule{config, user, profile, true}.Generate();
    }
    fmt::print("Emitted 192 TEV cases and 16 dynamic fragment families\n");
}

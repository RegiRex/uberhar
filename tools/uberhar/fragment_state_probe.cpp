// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Emit complete specialized/generic fragment shaders and the production
// uniform/108-byte transport for offscreen state, texture, color and depth tests.
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "common/logging/log.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

template <typename T>
void Binary(const std::filesystem::path& path, const T& value) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator;
    using Texture = Pica::TexturingRegs::TextureConfig;
    using Raster = Pica::RasterizerRegs;
    using Fog = Pica::TexturingRegs::FogMode;
    const std::filesystem::path output{argv[1]};
    std::filesystem::create_directories(output);
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_logic_op = true;
    profile.has_custom_border_color = false;
    const UserConfig user{};
    constexpr std::array scissor{Raster::ScissorMode::Disabled, Raster::ScissorMode::Include,
                                 Raster::ScissorMode::Exclude};
    constexpr std::array types{Texture::Texture2D, Texture::Projection2D, Texture::Disabled};
    for (u32 i = 0; i < 192; ++i) {
        Pica::RegsInternal regs{};
        regs.framebuffer.output_merger.alphablend_enable.Assign(1);
        regs.lighting.disable.Assign(i < 144);
        FSConfig config{regs};
        config.framebuffer.alpha_test_func.Assign(
            static_cast<Pica::FramebufferRegs::CompareFunc>(i % 8));
        config.framebuffer.scissor_test_mode.Assign(scissor[(i / 8) % 3]);
        config.framebuffer.depthmap_enable.Assign((i / 24) % 2 ? Raster::WBuffering
                                                               : Raster::ZBuffering);
        config.texture.fog_mode.Assign((i / 3) % 3 ? Fog::Fog : Fog::None);
        config.texture.fog_flip.Assign((i / 7) % 2);
        config.texture.texture0_type.Assign(types[(i / 5) % 3]);
        config.texture.texture2_use_coord1.Assign((i / 11) % 2);
        for (u32 axis = 0; axis < 6; ++axis) {
            auto& wrap = config.texture.requested_wrap[axis / 2];
            (axis & 1 ? wrap.t : wrap.s) =
                (i >> axis) & 1 ? Texture::ClampToBorder : Texture::Repeat;
        }
        // AstraEH: Select each real texture unit; final cases also exercise lighting
        // outputs through a modulate stage without altering the lighting algorithm.
        config.texture.tev_stages[0].sources_raw = (3 + i % 3) * 0x10001U;
        for (u32 stage = 1; stage < 6; ++stage)
            config.texture.tev_stages[stage].sources_raw = 0x000f000f;
        if (i >= 144) {
            config.texture.tev_stages[1].sources_raw = 0x001f001f;
            config.texture.tev_stages[1].ops_raw = 0x00010001;
        }
        const auto state = GLSL::MakeDynamicTevState(config, profile);
        const auto family = GLSL::MakeDynamicTevFamilyConfig(config, profile);
        const auto prefix = output / std::to_string(i);
        std::ofstream(prefix.string() + "-specialized.frag")
            << "#version 450\n"
            << GLSL::FragmentModule{config, user, profile}.Generate();
        std::ofstream(prefix.string() + "-generic.frag")
            << "#version 450\n"
            << GLSL::FragmentModule{family, user, profile, true}.Generate();
        Binary(prefix.string() + "-state.bin", state);
        FSUniformData uniforms{};
        uniforms.framebuffer_scale = 1;
        uniforms.alphatest_ref = (i % 4 == 0) ? 0 : (i % 4 == 1) ? 255 : 128;
        uniforms.depth_scale = 0.7f;
        uniforms.depth_offset = 0.9f;
        uniforms.scissor_x1 = 5;
        uniforms.scissor_y1 = 7;
        uniforms.scissor_x2 = 27;
        uniforms.scissor_y2 = 25;
        uniforms.fog_color = {0.2f, 0.7f, 0.4f};
        uniforms.tex_border_color[0] = {1.f, 0.f, 0.f, 0.5f};
        uniforms.tex_border_color[1] = {0.f, 1.f, 0.f, 1.f};
        uniforms.tex_border_color[2] = {0.f, 0.f, 1.f, 0.f};
        uniforms.lighting_global_ambient = {0.1f, 0.2f, 0.3f};
        uniforms.light_src[0].position = {0.f, 0.f, 1.f};
        uniforms.light_src[0].diffuse = {0.4f, 0.5f, 0.6f};
        uniforms.light_src[0].ambient = {0.1f, 0.1f, 0.1f};
        uniforms.light_src[0].specular_0 = {0.2f, 0.3f, 0.4f};
        uniforms.light_src[0].specular_1 = {0.3f, 0.4f, 0.5f};
        uniforms.light_src[0].spot_direction = {0.f, 0.f, -1.f};
        uniforms.light_src[0].dist_atten_scale = 1.f;
        Binary(prefix.string() + "-uniforms.bin", uniforms);
    }
    fmt::print("Emitted 192 full fragment state comparisons\n");
}

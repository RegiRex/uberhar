// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#include <filesystem>
#include <fstream>
// CodexAstraLocal: Exercise actual prepared-key and compiler ownership with synthetic state only.
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "common/logging/log.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"

// CodexAstraLocal: Expected normal generation is silent; an actual generator error fails this control.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view text, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(text, args));
}
}

// CodexAstraLocal: G1 is confined to cacheable full Vulkan GLSL. The existing
// pixel corpus checks formulas; these directed source witnesses ensure the
// intended dead work disappears without pruning the other generation domains.
void CheckFullPruning() {
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator::GLSL;
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(0);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    FSConfig config{regs};
    config.framebuffer.alpha_test_func.Assign(Pica::FramebufferRegs::CompareFunc::Always);
    for (auto& stage : config.texture.tev_stages) stage = {0, 0, 0, 0};
    // Dead earlier lighting and texture demands must not reach the full body.
    config.texture.tev_stages[0] = {0x00010003, 0, 0, 0};
    config.texture.tev_stages[5] = {0x05400540, 0, 0, 0};
    const auto original = config;
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_logic_op = true;
    const auto full = GenerateFragmentShader(config, {}, profile);
    if (full.find("color_output_0") != std::string::npos ||
        full.find("color_output_5") == std::string::npos ||
        full.find("vec4 diffuse_sum") != std::string::npos ||
        full.find("\ncolor_results_2 = ") != std::string::npos ||
        full.find("\nalpha_results_2 = ") != std::string::npos)
        throw std::runtime_error("Full pruning retained dead TEV/lighting/operands");
    if (!(config == original)) throw std::runtime_error("Full pruning mutated FSConfig identity");
    const auto require_unpruned = [](const std::string& code, bool full_source) {
        if (code.find("vec4 diffuse_sum") == std::string::npos ||
            (full_source && code.find("color_output_0") == std::string::npos))
            throw std::runtime_error("Full pruning escaped its generation domain");
    };
    auto opengl = profile;
    opengl.is_vulkan = false;
    require_unpruned(GenerateFragmentShader(config, {}, opengl), true);
    UserConfig custom{};
    custom.use_custom_normal.Assign(1);
    require_unpruned(GenerateFragmentShader(config, custom, profile), true);
    const auto family = MakeDynamicTevFamilyConfig(config, profile);
    const auto plan = MakeStaticTevPlan(MakeDynamicTevState(config, profile));
    require_unpruned(FragmentModule{family, {}, profile, true}.Generate(), false);
    require_unpruned(GenerateStaticTevFragmentShader(family, {}, profile, plan), false);
    for (u32 source : {1U, 2U}) {
        config.texture.tev_stages[5].sources_raw = source | (source << 16);
        if (GenerateFragmentShader(config, {}, profile).find("vec4 diffuse_sum") ==
            std::string::npos)
            throw std::runtime_error("Full pruning removed live lighting output");
    }
    std::puts("PASS full GLSL pruning boundaries, dead operands and live lighting controls");
}

void CheckPlan() {
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator;
    using namespace Pica::Shader::Generator::GLSL;
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    FSConfig config{regs};
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_logic_op = true;
    UserConfig user{};
    config.texture.combiner_buffer_input.Assign(0xa5);
    config.texture.tev_stages[0] = {0x00430143, 0x0055101, 0x00040004, 0x00020001};
    for (u32 i = 1; i < 6; ++i)
        config.texture.tev_stages[i] = {0x00ed00df, 0x0002104, 0x00010004, 0x00010002};
    if (!SupportsDynamicTev(config, user)) throw std::runtime_error("Fixture admission changed");
    auto state = MakeDynamicTevState(config, profile);
    const auto family = MakeDynamicTevFamilyConfig(config, profile);
    const auto plan = MakeStaticTevPlan(state);
    // CodexAstraLocal: Exact structure bytes are independently compared with the
    // original prepared prefix, including every unused bit and stage word.
    if (std::memcmp(&plan, &state, sizeof(plan)) != 0)
        throw std::runtime_error("Plan did not preserve the full structural prefix");
    for (u32 bit = 0; bit < sizeof(plan) * 8; ++bit) {
        auto changed = plan;
        reinterpret_cast<unsigned char*>(&changed)[bit / 8] ^= 1U << (bit % 8);
        if (changed == plan) throw std::runtime_error("Plan equality lost a structural bit");
    }
    for (u32 byte = sizeof(plan); byte < sizeof(state); ++byte)
        reinterpret_cast<unsigned char*>(&state)[byte] ^= 0xa5;
    if (!(MakeStaticTevPlan(state) == plan)) throw std::runtime_error("Plan captured runtime state");
    const auto expected = GenerateStaticTevFragmentShader(family, user, profile, plan);
    auto submitted = plan;
    FragmentModule delayed{family, user, profile, true, submitted};
    submitted.stages = {};
    submitted.buffer_mask = 0;
    if (delayed.Generate() != expected) throw std::runtime_error("Compiler borrowed the submitter plan");
    const auto wrong = GenerateStaticTevFragmentShader(family, user, profile, submitted);
    if (wrong == expected) throw std::runtime_error("Fixture cannot expose a stale/empty plan");
    if (expected.find("switch (color_op)") != std::string::npos ||
        expected.find("vec4 uber_source(") != std::string::npos ||
        expected.find("tev_index < stage_end") != std::string::npos)
        throw std::runtime_error("Static TEV still emits runtime interpretation");
    std::puts("PASS 805 key/prefix/runtime/compiler-ownership controls; wrong plan changes generated code");
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    CheckFullPruning();
    CheckPlan();
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator;
    namespace GLSL = Pica::Shader::Generator::GLSL;
    const std::filesystem::path output{argv[1]};
    std::filesystem::create_directory(output);
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    const FSConfig base{regs};
    const UserConfig user{};
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_custom_border_color = true;
    profile.has_logic_op = true;
    constexpr std::array types{Pica::TexturingRegs::TextureConfig::Texture2D,
                              Pica::TexturingRegs::TextureConfig::TextureCube,
                              Pica::TexturingRegs::TextureConfig::Disabled,
                              Pica::TexturingRegs::TextureConfig::Projection2D};
    for (u32 test = 0; test < 64; ++test) {
        auto config = base;
        config.texture.texture0_type.Assign(types[test % 4]);
        config.texture.fog_mode.Assign(test & 4 ? Pica::TexturingRegs::FogMode::Fog
                                              : Pica::TexturingRegs::FogMode::None);
        config.framebuffer.alpha_test_func.Assign(test & 8 ? Pica::FramebufferRegs::CompareFunc::LessThan
                                                           : Pica::FramebufferRegs::CompareFunc::Always);
        if (test & 16) {
            auto lit = regs;
            lit.lighting.disable.Assign(0);
            config.lighting = LightConfig{lit.lighting};
        }
        if (test & 32) {
            config.proctex.enable.Assign(1);
            config.proctex.lut_width = 128;
        }
        // CodexAstraLocal: Exercise all four real sampling helper interfaces,
        // unlike an empty TEV plan whose unused samplers could disappear.
        config.texture.tev_stages[0] = {0x00030003,0,0,0};
        config.texture.tev_stages[1] = {0x004f004f,0,0x00010001,0};
        config.texture.tev_stages[2] = {0x005f005f,0,0x00010001,0};
        config.texture.tev_stages[3] = {0x006f006f,0,0x00010001,0};
        config.texture.tev_stages[4] = {0x000f000f,0,0,0};
        config.texture.tev_stages[5] = {0x000f000f,0,0,0};
        if (!GLSL::SupportsDynamicTev(config,user)) throw std::runtime_error("Unsupported fixture family");
        const auto plan = GLSL::MakeStaticTevPlan(GLSL::MakeDynamicTevState(config,profile));
        const auto family = GLSL::MakeDynamicTevFamilyConfig(config,profile);
        const auto partial = GLSL::GenerateStaticTevFragmentShader(family,user,profile,plan);
        auto accurate = profile;
        accurate.enable_accurate_mul = true;
        if (partial != GLSL::GenerateStaticTevFragmentShader(family,user,accurate,plan))
            throw std::runtime_error("Accurate multiplication changed FS; extend numerical profile coverage");
        std::ofstream(output/(std::to_string(test)+".frag")) << "#version 450\n" << partial;
    }
    std::puts("PASS64 nonempty complete families and64 accurate-multiply source equalities");
}

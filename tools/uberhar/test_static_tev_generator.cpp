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
// CodexAstraLocal: Exercise the real original-config transport cache across
// family aliases and GPU-preflight/CPU-retry-like consecutive preparations.
#include "video_core/renderer_vulkan/uberhar_dynamic_tev_preparation.h"

// CodexAstraLocal: Expected normal generation is silent; an actual generator error fails this control.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view text, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(text, args));
}
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

// CodexAstraLocal: Directed consumer controls complement the executable
// independent forward-graph/legacy-emitter pixel corpus. No GPU speed claim.
void CheckGraphicsOptimizations() {
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator::GLSL;
    using Tev = Pica::TexturingRegs::TevStageConfig;
    unsigned checks = 0;
    const auto need = [&](bool okay, const char* message) {
        ++checks;
        if (!okay) throw std::runtime_error(message);
    };
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    FSConfig config{regs};
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_logic_op = true;
    for (auto& stage : config.texture.tev_stages) stage = {0x000f000f, 0, 0, 0};
    config.texture.tev_stages[0] = {0x00030003, 0, 0, 0};
    Vulkan::DynamicTevPreparation cache;
    const auto original = cache.Get(config, profile);
    auto overwritten = config;
    overwritten.texture.tev_stages[5] = {0x000e000e, 0, 0, 0};
    const auto later = cache.Get(overwritten, profile);
    need((original.buffer_mask & (1U << 12)) != 0 && (later.buffer_mask & (15U << 12)) == 0,
         "Dead overwritten texture remained live or snapshot mutated");
    need((later.stages[0].operations & (TevLiveRgb | TevLiveAlpha)) == 0,
         "Overwritten stage channels survived");
    need(MakeDynamicTevFamilyConfig(config, profile) == MakeDynamicTevFamilyConfig(overwritten, profile),
         "Fixture must reuse a family with different original liveness");
    const auto retry = cache.Get(config, profile);
    need(std::memcmp(&retry, &original, sizeof(retry)) == 0 && cache.Stats().builds == 3,
         "GPU refusal/CPU retry-like sequence reused another draw's masks");
    const auto constants = AnalyzeFragmentWork(overwritten, profile);
    need(constants.constant_rgb == 1 && constants.constant_alpha == 1 && constants.live_textures == 0,
         "Constant-only live output evidence misclassified");
    auto delayed = config;
    delayed.texture.combiner_buffer_input.Assign(0x11);
    delayed.texture.tev_stages[1] = {0x00040004, 0, 0, 0};
    delayed.texture.tev_stages[2] = {0x000d000d, 0, 0, 0};
    const auto buffer_plan = MakeDynamicTevState(delayed, profile);
    need((buffer_plan.buffer_mask & (15U << 12)) == (1U << 12), "Delayed buffer chose wrong texture producer");
    need((buffer_plan.stages[1].operations & (TevLiveRgb | TevLiveAlpha)) == 0,
         "Intermediate overwritten channel remained live");
    auto dot = config;
    dot.texture.tev_stages[0] = {0x00000033, 0, 7, 0};
    dot.texture.tev_stages[5] = {0x000f000e, 0, 0, 0};
    const auto dot_plan = MakeDynamicTevState(dot, profile);
    need((dot_plan.stages[0].operations & (TevLiveRgb | TevLiveAlpha)) == TevLiveAlpha &&
         (dot_plan.buffer_mask & (1U << 12)), "Dot3_RGBA lost alpha-to-RGB demand");
    auto identity = config;
    identity.texture.tev_stages[0] = {0x030f030f, 0, 0, 0};
    need((MakeDynamicTevState(identity, profile).buffer_mask & (15U << 12)) == 0,
         "Stage-zero whole identity incorrectly remapped Previous");
    auto lit = config;
    lit.lighting.enable.Assign(1);
    lit.lighting.src_num.Assign(1);
    lit.lighting.lights[0].directional.Assign(1);
    lit.lighting.bump_mode.Assign(Pica::LightingRegs::LightingBumpMode::NormalMap);
    lit.lighting.bump_selector.Assign(1);
    lit.texture.tev_stages[0] = {0x00410041, 0, 0x00010001, 0};
    const auto lit_work = AnalyzeFragmentWork(lit, profile);
    need(lit_work.lighting_texture_overlap == 2 && lit_work.directional_slots == 1,
         "Same-unit/directional evidence did not follow live lighting");
    const auto full = GenerateFragmentShader(lit, {}, profile);
    need(full.find("2.0 * (uber_shared_tex1()).rgb") != std::string::npos,
         "Lighting did not join the identical color-unit accessor");
    auto shadow = lit;
    shadow.texture.texture0_type.Assign(Pica::TexturingRegs::TextureConfig::ShadowCube);
    shadow.lighting.bump_selector.Assign(0);
    const auto typed = GenerateFragmentShader(shadow, {}, profile);
    need(typed.find("uber_shared_tex0") == std::string::npos,
         "Typed shadow sampling entered color memo");
    auto legacy = profile;
    legacy.is_vulkan = false;
    need(GenerateFragmentShader(lit, {}, legacy).find("uber_shared_sampled") == std::string::npos,
         "OpenGL was changed by the Vulkan-only optimization");
    UserConfig custom{};
    custom.use_custom_normal.Assign(1);
    need(GenerateFragmentShader(lit, custom, profile).find("uber_shared_sampled") == std::string::npos,
         "Custom-normal domain entered shared color memo");
    const auto family = MakeDynamicTevFamilyConfig(lit, profile);
    const auto dynamic = FragmentModule{family, {}, profile, true}.Generate();
    const auto main = dynamic.find("void main()");
    const auto early = dynamic.find("if ((uber_tev.framebuffer & 7u) == 0u) discard;", main);
    need(early != std::string::npos && early < dynamic.find("float depth =", main) &&
         early < dynamic.find("vec4 diffuse_sum", main), "Alpha-Never rejection was not before fragment work");
    // CodexAstraLocal: Controls change their intended generator/transport paths
    // independently; old cache identity cannot stand in for a numeric certificate.
    auto off = profile;
    off.fragment_alpha_never = false;
    need(FragmentModule{family, {}, off, true}.Generate().find(
        "if ((uber_tev.framebuffer & 7u) == 0u) discard;") == std::string::npos,
        "Alpha-Never OFF still emits early dynamic discard");
    off = profile; off.fragment_memo = false;
    need(FragmentModule{family, {}, off, true}.Generate().find("uber_shared_tex") == std::string::npos,
        "Memo OFF retained shared cache");
    off = profile; off.fragment_liveness = false;
    const auto all_live = MakeDynamicTevState(overwritten, off);
    need((all_live.stages[0].operations & (TevLiveRgb | TevLiveAlpha)) == (TevLiveRgb | TevLiveAlpha),
        "Liveness OFF retained eliminated outputs");
    // CodexAstraLocal: OFF must change actual emitter shape, not only set live
    // uniform bits; source structure complements independently rendered pixels.
    const auto unguarded = FragmentModule{family, {}, off, true}.Generate();
    need(unguarded.find("live_rgb") == std::string::npos &&
         unguarded.find("live_alpha") == std::string::npos &&
         unguarded.find("if ((uber_tev.buffer_mask & 2048u)") == std::string::npos &&
         unguarded.find("uber_tev.stages[tev_index].z & 256u") == std::string::npos &&
         unguarded.find("uber_tev.stages[tev_index].z & 512u") == std::string::npos,
         "Liveness OFF retained runtime guard instructions");
    // A varying texture plan has no possible endpoint branch. Its single
    // declaration must dominate RGB/alpha uses without a dynamic cache mask.
    for (bool memo : {false, true}) {
        auto local_profile = profile; local_profile.fragment_memo = memo;
        const auto local_family = MakeDynamicTevFamilyConfig(config, local_profile);
        const auto local_plan = MakeStaticTevPlan(MakeDynamicTevState(config, local_profile));
        const auto local = GenerateStaticTevFragmentShader(local_family, {}, local_profile, local_plan);
        const auto first = local.find("vec4 uber_static_texel0 = ");
        need(first != std::string::npos && local.find("uber_static_sampled") == std::string::npos &&
             local.find("vec4 uber_static_texel0 = ", first + 1) == std::string::npos,
             "Branch-free static texture did not use one dominated local");
    }
    need(dynamic.find("uber_texel0 = uber_shared_tex0") == std::string::npos,
        "Generic ordinary sampler retained duplicate cache");
    std::printf("PASS %u graphics liveness/memo/transport/evidence controls\n", checks);
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    CheckPlan();
    CheckGraphicsOptimizations();
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

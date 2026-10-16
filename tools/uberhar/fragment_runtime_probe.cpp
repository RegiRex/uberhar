// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Reuse only fixture plumbing; production code supplies both
// generated shader routes and the actual runtime certificate/cache implementation.
#include <bit> // CodexAstraLocal: Require nonempty actual canonical-slot coverage.
#define main inherited_fragment_fixture_main
#include "fragment_state_probe.cpp"
#undef main
#include "video_core/renderer_vulkan/uberhar_dynamic_tev_preparation.h"
#include "video_core/renderer_vulkan/uberhar_static_tev_policy.h"

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator;
    using Tev = Pica::TexturingRegs::TevStageConfig;
    const std::filesystem::path output{argv[1]};
    std::filesystem::create_directories(output);
    Profile profile{};
    profile.is_vulkan = profile.has_separable_shaders = profile.has_logic_op = true;
    auto original = profile;
    original.fragment_liveness = original.fragment_alpha_never = original.fragment_memo = false;
    original.fragment_constant_tev = original.fragment_directional_precompute = false;
    Vulkan::DynamicTevPreparation cache;
    u64 folded_rgb = 0, folded_alpha = 0, canonical_slots = 0, controls = 0;
    const auto need = [&](bool okay, const char* why) { ++controls; if (!okay) throw std::runtime_error(why); };
    constexpr u32 cases = 144;
    for (u32 n = 0; n < cases; ++n) {
        Pica::RegsInternal regs{};
        regs.framebuffer.output_merger.alphablend_enable.Assign(1);
        regs.lighting.disable.Assign(n < 96);
        FSConfig config{regs};
        config.framebuffer.alpha_test_func.Assign(Pica::FramebufferRegs::CompareFunc::Always);
        config.texture.texture0_type.Assign(Pica::TexturingRegs::TextureConfig::Texture2D);
        for (auto& stage : config.texture.tev_stages) stage = {0x000f000f, 0, 0, 0};
        GLSL::FragmentRuntimeValues values{};
        values.available = true;
        FSUniformData uniforms{};
        uniforms.framebuffer_scale = 1;
        uniforms.depth_scale = 0.7f; uniforms.depth_offset = 0.9f;
        const auto color = [](u32 raw) {
            return Common::Vec4f{float(raw & 255) / 255, float((raw >> 8) & 255) / 255,
                float((raw >> 16) & 255) / 255, float(raw >> 24) / 255};
        };
        // CodexAstraLocal: Consecutive numeric changes retain exactly the same
        // structural config. Include endpoint/nonendpoint and independent RGB/A.
        for (u32 i = 0; i < 6; ++i) {
            u32 rgba = 0;
            for (u32 c = 0; c < 4; ++c)
                rgba |= u32(((n + i * 5 + c * 3) >> (c % 3)) & 1 ? 255 : 0) << (8 * c);
            if (n % 5 == 4) rgba = 0x7F80817EU;
            values.constants[i] = rgba;
            uniforms.const_color[i] = color(rgba);
        }
        values.buffer = n & 1 ? 0xFF00FFFFU : 0x00FF0000U;
        uniforms.tev_combiner_buffer_color = color(values.buffer);
        if (n < 16) {
            config.texture.tev_stages[0] = {0x00030003, 0, 0, 0};
            config.texture.tev_stages[5] = {0x000e000f, (n / 5 % 2) << 12, 0, 0};
        } else if (n < 32) {
            config.texture.tev_stages[5] = {0x000e000e, (n / 5 % 2) * 0x1001U, 0, 0};
        } else if (n < 96) {
            constexpr std::array<u32, 9> ops{0, 1, 2, 4, 5, 6, 7, 8, 9};
            constexpr std::array<u32, 7> alpha{0, 1, 2, 4, 5, 8, 9};
            constexpr std::array<u32, 10> mods{0, 1, 2, 3, 4, 5, 8, 9, 12, 13};
            config.texture.combiner_buffer_input.Assign((n * 37) & 255);
            for (u32 i = 0; i < 6; ++i) {
                auto& stage = config.texture.tev_stages[i];
                stage = {i ? 0x00ed00dfU : 0x00ee00efU,
                    mods[(n + i) % mods.size()] | (((n + i) % 8) << 12),
                    ops[(n + i) % ops.size()] | (alpha[(n + i) % alpha.size()] << 16),
                    n % 4 | (((n / 4) % 4) << 16)};
            }
        } else {
            // CodexAstraLocal: Remapped/duplicate slots, both distance consumers,
            // every LUT input and actual normal-map/TEV same-unit first use.
            auto& lighting = config.lighting;
            lighting.config.Assign(Pica::LightingRegs::LightingConfig::Config7);
            lighting.src_num.Assign(1 + n % 4);
            lighting.enable_primary_alpha.Assign(1); lighting.enable_secondary_alpha.Assign(1);
            lighting.bump_mode.Assign(n % 3 == 0 ? Pica::LightingRegs::LightingBumpMode::NormalMap :
                                                 Pica::LightingRegs::LightingBumpMode::None);
            lighting.bump_selector.Assign(0);
            lighting.bump_renorm.Assign(1);
            config.texture.tev_stages[0] = {0x00310031, 0, 0x00010001, 0};
            for (u32 i = 0; i < 8; ++i) {
                auto& light = lighting.lights[i];
                light.num.Assign(n % 2 ? (i * 3 + 1) % 8 : 3);
                light.directional.Assign(1); light.dist_atten_enable.Assign(n & 1);
                light.spot_atten_enable.Assign(1); light.two_sided_diffuse.Assign(i & 1);
                light.geometric_factor_0.Assign(n & 2); light.geometric_factor_1.Assign(n & 4);
                auto& uniform = uniforms.light_src[i];
                // CodexAstraLocal: Light position uniforms are exact float16 conversions.
                constexpr std::array<u16, 6> zs{0x3C00, 0x4000, 0xBC00, 0x3C00, 0x3C00, 0x3C00};
                const u16 x = n % 6 == 3 ? 0x8000 : n % 6 == 4 ? 0x3800 : 0;
                const u16 z = zs[n % 6];
                values.light_positions[i] = u64{x} | (u64{z} << 32);
                uniform.position = {Pica::f16::FromRaw(x).ToFloat32(), 0.f, Pica::f16::FromRaw(z).ToFloat32()};
                uniform.diffuse = {0.07f, 0.11f, 0.13f}; uniform.ambient = {0.01f, 0.02f, 0.03f};
                uniform.specular_0 = {0.09f, 0.05f, 0.12f}; uniform.specular_1 = {0.31f, 0.23f, 0.37f};
                uniform.spot_direction = {0.1f, -0.2f, 0.7f};
                uniform.dist_atten_scale = 0.07f * (i + 1); uniform.dist_atten_bias = 0.03f * i;
            }
            for (auto* lut : {&lighting.lut_d0, &lighting.lut_d1, &lighting.lut_sp, &lighting.lut_fr,
                              &lighting.lut_rr, &lighting.lut_rg, &lighting.lut_rb}) {
                lut->enable.Assign(1); lut->type.Assign(static_cast<Pica::LightingRegs::LightingLutInput>(n % 6));
                lut->abs_input.Assign(n & 1); lut->SetScale(1.f);
            }
            for (u32 table = 0; table < 24; ++table) uniforms.lighting_lut_offset[table / 4][table % 4] = table * 256;
        }
        const auto state = cache.Get(config, profile, values);
        need(std::memcmp(&state, &cache.Get(config, profile, values), sizeof(state)) == 0, "exact reuse changed bytes");
        // CodexAstraLocal: Directed qualification witnesses are semantic:
        // every endpoint Replace channel must fold; every deliberately interior
        // byte remains ordinary. Do not require an arbitrary random-corpus total.
        if (n >= 16 && n < 32) {
            const bool endpoint = n % 5 != 4;
            need(bool(state.stages[5].operations & GLSL::TevConstantRgb) == endpoint,
                 "Replace RGB endpoint qualification changed");
            need(bool(state.stages[5].operations & GLSL::TevConstantAlpha) == endpoint,
                 "Replace alpha endpoint qualification changed");
        }
        if (n >= 96) {
            const u32 mask = n % 6 == 0 || n % 6 == 5 ? (1U << (1 + n % 4)) - 1 : 0;
            need((state.lighting_luts_hi >> 24) == mask,
                 "canonical versus signed-zero/nonunit/other-axis slot qualification changed");
            auto invalid_values = values;
            invalid_values.light_positions.fill(0);
            auto invalid_state = state;
            GLSL::ApplyFragmentRuntime(invalid_state, profile, invalid_values);
            need(!(invalid_state.lighting_luts_hi >> 24), "zero direction admitted");
            invalid_values.light_positions.fill(u64{0x7E00} << 32);
            GLSL::ApplyFragmentRuntime(invalid_state, profile, invalid_values);
            need(!(invalid_state.lighting_luts_hi >> 24), "nonfinite direction admitted");
        }
        const auto plan = GLSL::MakeStaticTevPlan(state);
        const auto plain = GLSL::MakeDynamicTevState(config, profile);
        need(plan == GLSL::MakeStaticTevPlan(plain), "numeric values entered module identity");
        auto disabled = GLSL::MakeDynamicTevState(config, original);
        GLSL::ApplyFragmentRuntime(disabled, original, values);
        for (const auto& stage : disabled.stages) need(!(stage.operations & GLSL::TevConstantBits), "OFF retained result bits");
        need(!(disabled.lighting_luts_hi >> 24), "OFF retained canonical mask");
        auto unavailable = state; GLSL::ApplyFragmentRuntime(unavailable, profile, {});
        for (const auto& stage : unavailable.stages) need(!(stage.operations & GLSL::TevConstantBits), "unavailable reused result bits");
        need(!(unavailable.lighting_luts_hi >> 24), "unavailable reused light certificate");
        for (const auto& stage : state.stages) {
            folded_rgb += bool(stage.operations & GLSL::TevConstantRgb);
            folded_alpha += bool(stage.operations & GLSL::TevConstantAlpha);
        }
        canonical_slots += std::popcount(state.lighting_luts_hi >> 24);
        const auto family = GLSL::MakeDynamicTevFamilyConfig(config, profile);
        const auto prefix = output / std::to_string(n);
        std::ofstream(prefix.string() + "-specialized.frag") << "#version 450\n" << GLSL::GenerateFragmentShader(config, {}, original);
        std::ofstream(prefix.string() + "-generic.frag") << "#version 450\n" << GLSL::FragmentModule{family, {}, profile, true}.Generate();
        std::ofstream(prefix.string() + "-partial.frag") << "#version 450\n" << GLSL::GenerateStaticTevFragmentShader(family, {}, profile, plan);
        std::ofstream(prefix.string() + "-off.frag") << "#version 450\n" << GLSL::FragmentModule{family, {}, original, true}.Generate();
        Binary(prefix.string() + "-state.bin", state); Binary(prefix.string() + "-off-state.bin", disabled);
        Binary(prefix.string() + "-uniforms.bin", uniforms);
        // CodexAstraLocal: Retain exact normalization inputs for a bit-level
        // original-versus-optimized GLSL discriminator, in addition to pixels/LUTs.
        std::array<Common::Vec4f, 8> directions{};
        for (u32 i = 0; i < 8; ++i) {
            const auto& v = uniforms.light_src[i].position;
            directions[i] = {v.x, v.y, v.z, 0.f};
        }
        Binary(prefix.string() + "-directions.bin", directions);

    }
    // CodexAstraLocal: Actual preparation and qualification must be exercised,
    // not merely helper text. Capacity negatives preserve the fixed old limits.
    need(folded_rgb >= 13 && folded_alpha >= 13 && canonical_slots > 10, "empty arithmetic coverage");
    need(cache.Stats().hits >= cases && cache.Stats().builds >= cases, "cache controls did not execute");
    need(Vulkan::StaticTevPolicy::AllowCheapAdmission(5, 47, 8, 64), "cheap capacity refused early");
    need(!Vulkan::StaticTevPolicy::AllowCheapAdmission(6, 47, 8, 64), "reserved owner leaked");
    need(!Vulkan::StaticTevPolicy::AllowCheapAdmission(5, 48, 8, 64), "reserved attempts leaked");
    std::ofstream(output / "counts.json") << fmt::format("{{\"cases\":{},\"controls\":{},\"rgb\":{},\"alpha\":{},\"canonical_slots\":{},\"cache_hits\":{}}}\n",
        cases, controls, folded_rgb, folded_alpha, canonical_slots, cache.Stats().hits);
    fmt::print("PASS runtime preparation: {} controls, RGB{} alpha{} canonical{}\n", controls, folded_rgb, folded_alpha, canonical_slots);
}

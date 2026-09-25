// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

// AstraEH: Exercise the production family canonicalizer against generated GLSL,
// including states that must stay distinct. A logging failure aborts the probe.
#include <cstring>
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

namespace {
using namespace Pica::Shader;
using namespace Pica::Shader::Generator::GLSL;
using Framebuffer = Pica::FramebufferRegs;
using Texture = Pica::TexturingRegs::TextureConfig;
using Fog = Pica::TexturingRegs::FogMode;

// AstraEH: Comparing the full representation also verifies deterministic keys,
// not just the extremely likely absence of a hash collision in a small corpus.
bool Equal(const FSConfig& a, const FSConfig& b) {
    return std::memcmp(&a, &b, sizeof(FSConfig)) == 0;
}

u32 comparisons{};
u32 aliases{};
u32 distinctions{};

std::string Verify(const FSConfig& original, const Profile& profile) {
    const auto saved = original;
    const auto canonical = MakeDynamicTevFamilyConfig(original, profile);
    if (!Equal(original, saved) ||
        !Equal(canonical, MakeDynamicTevFamilyConfig(canonical, profile))) {
        throw std::runtime_error("Canonicalization mutated its input or is not idempotent");
    }
    const UserConfig user{};
    const auto source = FragmentModule{original, user, profile, true}.Generate();
    if (source != FragmentModule{canonical, user, profile, true}.Generate()) {
        throw std::runtime_error("Canonicalization changed generated fallback GLSL");
    }
    ++comparisons;
    return source;
}

void CheckPair(const FSConfig& a, const FSConfig& b, const Profile& profile, bool same,
               const char* label) {
    const auto source_a = Verify(a, profile);
    const auto source_b = Verify(b, profile);
    const auto key_a = MakeDynamicTevFamilyConfig(a, profile);
    const auto key_b = MakeDynamicTevFamilyConfig(b, profile);
    if (Equal(a, b) || Equal(key_a, key_b) != same || (key_a.Hash() == key_b.Hash()) != same ||
        (source_a == source_b) != same) {
        throw std::runtime_error(label);
    }
    same ? ++aliases : ++distinctions;
}
} // namespace

int main() {
    // AstraEH: Cross device profiles and live fog/lighting/blending choices so
    // aliases are checked in both simple and more expensive fragment families.
    for (u32 flags = 0; flags < 32; ++flags) {
        Profile profile{};
        profile.is_vulkan = true;
        profile.has_separable_shaders = true;
        profile.has_custom_border_color = (flags & 1) != 0;
        profile.has_logic_op = (flags & 2) != 0;
        Pica::RegsInternal regs{};
        regs.framebuffer.output_merger.alphablend_enable.Assign((flags & 4) != 0);
        regs.framebuffer.output_merger.logic_op.Assign(Framebuffer::LogicOp::Copy);
        regs.lighting.disable.Assign((flags & 8) == 0);
        regs.texturing.fog_mode.Assign((flags & 16) ? Fog::Fog : Fog::None);
        const FSConfig base{regs};

        // AstraEH: All six wrap axes now share source; active border checks move
        // into per-draw data rather than being dropped from the actual behavior.
        for (u32 axis = 0; axis < 6; ++axis) {
            for (u32 mode = 1; mode < 8; ++mode) {
                auto variant = base;
                auto& wrap = variant.texture.requested_wrap[axis / 2];
                (axis & 1 ? wrap.t : wrap.s) = static_cast<Texture::WrapMode>(mode);
                CheckPair(base, variant, profile, true,
                          "Sampler alias or active border distinction failed");
                const auto state = MakeDynamicTevState(variant, profile);
                const u32 expected =
                    !profile.has_custom_border_color && mode == Texture::ClampToBorder
                        ? (1U << axis)
                        : 0U;
                if ((state.texture & 63U) != expected) {
                    throw std::runtime_error(
                        "Runtime border transport differs from device profile");
                }
            }
        }

        // AstraEH: Native Vulkan blending retains its own pipeline key. Its
        // requested equations/factors must not fragment the fragment-module cache.
        if (base.framebuffer.alphablend_enable) {
            for (u32 equation = 0; equation < 5; ++equation) {
                for (u32 factor = 0; factor < 15; ++factor) {
                    auto variant = base;
                    variant.framebuffer.requested_rgb_blend = {
                        static_cast<Framebuffer::BlendEquation>(equation),
                        static_cast<Framebuffer::BlendFactor>(factor),
                        static_cast<Framebuffer::BlendFactor>(14 - factor)};
                    variant.framebuffer.requested_alpha_blend =
                        variant.framebuffer.requested_rgb_blend;
                    CheckPair(base, variant, profile, true, "Native blend alias failed");
                }
            }
        }

        // AstraEH: Active shader-emulated clear/set operations must survive;
        // unsupported emulated logic operations are not introduced by this test.
        for (u32 operation = 0; operation < 16; ++operation) {
            const auto op = static_cast<Framebuffer::LogicOp>(operation);
            if (op == Framebuffer::LogicOp::Copy) {
                continue;
            }
            const bool inactive = profile.has_logic_op || base.framebuffer.alphablend_enable;
            if (!inactive && op != Framebuffer::LogicOp::Clear && op != Framebuffer::LogicOp::Set) {
                continue;
            }
            auto variant = base;
            variant.framebuffer.requested_logic_op = op;
            CheckPair(base, variant, profile, inactive, "Logic-operation distinction failed");
        }

        auto variant = base;
        variant.texture.fog_flip.Assign(1);
        CheckPair(base, variant, profile, true, "Fog orientation distinction failed");

        variant = base;
        variant.framebuffer.alpha_test_func.Assign(Framebuffer::CompareFunc::LessThan);
        CheckPair(base, variant, profile, true, "Runtime alpha test failed to share source");
        variant = base;
        variant.texture.texture2_use_coord1.Assign(1);
        CheckPair(base, variant, profile, true, "Runtime coordinate choice failed to share source");
        variant = base;
        variant.texture.texture0_type.Assign(Texture::TextureType::TextureCube);
        CheckPair(base, variant, profile, false, "Texture resource type collapsed");

        // AstraEH: TEV instructions and buffer writes are runtime inputs in both
        // paths. Canonicalizing them must not mutate the caller's push constants.
        variant = base;
        variant.texture.tev_stages[2].sources_raw = 0x000f000f;
        variant.texture.combiner_buffer_input.Assign(255);
        CheckPair(base, variant, profile, true, "Runtime TEV data entered the family key");
    }
    // AstraEH: Each runtime lighting dimension must alias without changing source;
    // structural lighting changes must remain distinct. Numerical behavior is
    // independently checked against specialized rendering by the full fragment corpus.
    for (u32 mode : {0U, 8U}) {
        using Lighting = Pica::LightingRegs;
        Profile profile{};
        profile.is_vulkan = true;
        profile.has_separable_shaders = true;
        Pica::RegsInternal regs{};
        regs.framebuffer.output_merger.alphablend_enable.Assign(1);
        FSConfig base{regs};
        base.lighting.config.Assign(static_cast<Lighting::LightingConfig>(mode));
        base.lighting.src_num.Assign(3);
        base.lighting.enable_primary_alpha.Assign(1);
        for (auto* lut : {&base.lighting.lut_d0, &base.lighting.lut_d1, &base.lighting.lut_sp,
                          &base.lighting.lut_fr, &base.lighting.lut_rr, &base.lighting.lut_rg,
                          &base.lighting.lut_rb}) {
            lut->enable.Assign(1);
            lut->type.Assign(Lighting::LightingLutInput::LN);
            lut->abs_input.Assign(1);
            lut->SetScale(1.0f);
        }
        for (u32 slot = 0; slot < 7; ++slot) {
            for (u32 input = 0; input < 6; ++input) {
                for (float scale : {0.0f, 0.25f, 0.5f, 2.0f, 4.0f, 8.0f}) {
                    auto variant = base;
                    const std::array luts{&variant.lighting.lut_d0, &variant.lighting.lut_d1,
                                          &variant.lighting.lut_sp, &variant.lighting.lut_fr,
                                          &variant.lighting.lut_rr, &variant.lighting.lut_rg,
                                          &variant.lighting.lut_rb};
                    luts[slot]->SetScale(scale);
                    luts[slot]->type.Assign(static_cast<Lighting::LightingLutInput>(input));
                    luts[slot]->abs_input.Assign(input & 1);
                    CheckPair(base, variant, profile, true, "LUT controls split a runtime family");
                    const auto state = MakeDynamicTevState(variant, profile);
                    const u32 word = slot < 4 ? state.lighting_luts_lo : state.lighting_luts_hi;
                    const u32 control = (word >> ((slot % 4) * 8)) & 127;
                    constexpr std::array decoded{0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
                    if ((control & 7) != input || ((control >> 3) & 1) != (input & 1) ||
                        decoded.at(control >> 4) != scale) {
                        throw std::runtime_error("Runtime LUT transport mismatch");
                    }
                }
            }
        }
        for (u32 slot = 0; slot < 8; ++slot) {
            for (u32 source = 1; source < 8; ++source) {
                auto variant = base;
                variant.lighting.lights[slot].num.Assign(source);
                CheckPair(base, variant, profile, true, "Light source split a runtime family");
                variant.lighting.lights[slot].two_sided_diffuse.Assign(1);
                const auto state = MakeDynamicTevState(variant, profile);
                if (((state.lighting_sources >> (slot * 3)) & 7) != source ||
                    ((state.lighting_sources >> (24 + slot)) & 1) != 1) {
                    throw std::runtime_error("Light source/two-sided transport mismatch");
                }
            }
        }
        auto variant = base;
        variant.lighting.src_num.Assign(2);
        CheckPair(base, variant, profile, false, "Unrolled light count was collapsed");
        variant = base;
        variant.lighting.lights[0].directional.Assign(1);
        CheckPair(base, variant, profile, false, "Directional operation was collapsed");
        variant = base;
        variant.lighting.lut_d0.SetScale(1.5f);
        if (SupportsDynamicTev(variant, UserConfig{})) {
            throw std::runtime_error("Unknown lighting scale bypassed recovery");
        }
        variant.lighting.lut_d0.SetScale(1.0f);
        variant.lighting.lut_d0.type.Assign(static_cast<Lighting::LightingLutInput>(6));
        if (SupportsDynamicTev(variant, UserConfig{})) {
            throw std::runtime_error("Unknown lighting input bypassed recovery");
        }
    }
    fmt::print("PASS: {} source-equivalence/idempotence checks, {} merged pairs, "
               "{} shader-affecting distinctions\n",
               comparisons, aliases, distinctions);
}

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

// AstraEH: Keep an independent copy of the pre-0.1.7 admission predicate. Reason
// diagnostics must not widen coverage or silently alter the established fallback.
bool LegacySupport(const FSConfig& c, const UserConfig& u) {
    if (c.UsesSpirvIncompatibleConfig() || c.texture.texture0_type == Texture::Shadow2D ||
        c.texture.fog_mode == Fog::Gas || !u.IsCacheable()) return false;
    if (c.lighting.enable) {
        if (c.lighting.src_num > 8) return false;
        for (const auto* lut : std::array{&c.lighting.lut_d0, &c.lighting.lut_d1,
                 &c.lighting.lut_sp, &c.lighting.lut_fr, &c.lighting.lut_rr,
                 &c.lighting.lut_rg, &c.lighting.lut_rb}) {
            const auto scale = lut->GetScale();
            if (lut->enable && (static_cast<u32>(lut->type.Value()) > 5 ||
                !(scale == 0 || scale == .25f || scale == .5f || scale == 1 ||
                  scale == 2 || scale == 4 || scale == 8))) return false;
        }
    }
    using Op = Pica::TexturingRegs::TevStageConfig::Operation;
    for (const Pica::TexturingRegs::TevStageConfig stage : c.texture.tev_stages)
        if (stage.color_op == Op::AddSigned ||
            (stage.color_op != Op::Dot3_RGBA && stage.alpha_op == Op::AddSigned)) return false;
    return true;
}

void CheckSupportReasons() {
    using Reason = DynamicTevSupport;
    using Op = Pica::TexturingRegs::TevStageConfig::Operation;
    static_assert(static_cast<u32>(Reason::Count) == 8);
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    const FSConfig base{regs};
    u32 checked = 0;
    const auto verify = [&](const FSConfig& c, const UserConfig& u, Reason expected) {
        if (CheckDynamicTevSupport(c, u) != expected ||
            SupportsDynamicTev(c, u) != LegacySupport(c, u))
            throw std::runtime_error("Recovery classification changed admission");
        ++checked;
    };
    verify(base, {}, Reason::Ready);
    auto c = base;
    c.framebuffer.shadow_rendering.Assign(1);
    verify(c, {}, Reason::SpirvIncompatible);
    c = base; c.texture.texture0_type.Assign(Texture::ShadowCube);
    verify(c, {}, Reason::SpirvIncompatible);
    c = base; c.texture.texture0_type.Assign(Texture::Shadow2D);
    verify(c, {}, Reason::Shadow2D);
    c = base; c.texture.fog_mode.Assign(Fog::Gas);
    verify(c, {}, Reason::GasFog);
    UserConfig custom{}; custom.use_custom_normal.Assign(1);
    verify(base, custom, Reason::CustomUser);
    c = base; c.lighting.enable.Assign(1); c.lighting.src_num.Assign(9);
    verify(c, {}, Reason::LightCount);
    c = base; c.lighting.enable.Assign(1); c.lighting.lut_d0.enable.Assign(1);
    c.lighting.lut_d0.type.Assign(static_cast<Pica::LightingRegs::LightingLutInput>(6));
    verify(c, {}, Reason::LightingLut);
    c.lighting.lut_d0.enable.Assign(0);
    verify(c, {}, Reason::Ready);
    c.lighting.lut_d0.enable.Assign(1); c.lighting.lut_d0.type.Assign(
        static_cast<Pica::LightingRegs::LightingLutInput>(0));
    c.lighting.lut_d0.SetScale(1.5f);
    verify(c, {}, Reason::LightingLut);
    c.lighting.enable.Assign(0);
    verify(c, {}, Reason::Ready);
    // AstraEH: Alpha AddSigned is ignored under Dot3_RGBA. Test every TEV slot.
    for (u32 slot = 0; slot < 6; ++slot) {
        c = base;
        Pica::TexturingRegs::TevStageConfig stage = c.texture.tev_stages[slot];
        stage.color_op.Assign(Op::AddSigned);
        c.texture.tev_stages[slot].ops_raw = stage.ops_raw;
        verify(c, {}, Reason::AddSigned);
        stage.color_op.Assign(Op::Replace); stage.alpha_op.Assign(Op::AddSigned);
        c.texture.tev_stages[slot].ops_raw = stage.ops_raw;
        verify(c, {}, Reason::AddSigned);
        stage.color_op.Assign(Op::Dot3_RGBA); c.texture.tev_stages[slot].ops_raw = stage.ops_raw;
        verify(c, {}, Reason::Ready);
    }
    // AstraEH: Mixed-invalid cases verify first-reason precedence against the
    // original boolean gate without constructing or executing invalid shaders.
    for (u32 bits = 0; bits < 1024; ++bits) {
        c = base;
        c.framebuffer.shadow_rendering.Assign((bits >> 0) & 1);
        c.texture.texture0_type.Assign((bits & 2) ? Texture::Shadow2D : Texture::Texture2D);
        c.texture.fog_mode.Assign((bits & 4) ? Fog::Gas : Fog::None);
        UserConfig u{}; u.use_custom_normal.Assign((bits >> 3) & 1);
        c.lighting.enable.Assign((bits >> 4) & 1);
        c.lighting.src_num.Assign((bits & 32) ? 9 : 0);
        c.lighting.lut_d0.enable.Assign((bits >> 6) & 1);
        c.lighting.lut_d0.SetScale((bits & 128) ? 1.5f : 1.0f);
        Pica::TexturingRegs::TevStageConfig stage = c.texture.tev_stages[0];
        stage.color_op.Assign((bits & 256) ? Op::AddSigned : Op::Replace);
        stage.alpha_op.Assign((bits & 512) ? Op::AddSigned : Op::Replace);
        c.texture.tev_stages[0].ops_raw = stage.ops_raw;
        const Reason expected = (bits & 1) ? Reason::SpirvIncompatible :
            (bits & 2) ? Reason::Shadow2D : (bits & 4) ? Reason::GasFog :
            (bits & 8) ? Reason::CustomUser :
            ((bits & 16) && (bits & 32)) ? Reason::LightCount :
            ((bits & 16) && (bits & 64) && (bits & 128)) ? Reason::LightingLut :
            (bits & 768) ? Reason::AddSigned : Reason::Ready;
        verify(c, u, expected);
    }
    fmt::print("PASS: {} recovery reason/legacy-admission checks\n", checked);
}
} // namespace

int main() {
    CheckSupportReasons();
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
    // AstraEH: Each runtime lighting dimension must alias without changing
    // source; structural lighting changes must remain distinct. Numerical
    // behavior is independently checked against specialized rendering by the full
    // fragment corpus.
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
                    const u32 control = (word >> ((slot % 4) * 8)) & 255;
                    constexpr std::array decoded{0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
                    const u32 expected_input = input == 5 && mode != 8 ? 6 : input;
                    if ((control & 7) != expected_input || ((control >> 3) & 1) != (input & 1) ||
                        decoded.at((control >> 4) & 7) != scale) {
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
        // AstraEH: Exhaust all global LUT enable masks and configurations. An
        // independent support matrix checks packing, including spotlight's dummy
        // enable and red fallback channels; the render corpus checks their values.
        constexpr std::array modes{0U, 1U, 2U, 3U, 4U, 5U, 6U, 8U};
        // Bits: D0, D1, SP, FR, RR, RG, RB. Do not derive this oracle from
        // production.
        constexpr std::array supported{0x15U, 0x1cU, 0x13U, 0x0bU, 0x77U, 0x7dU, 0x1fU, 0x7fU};
        for (u32 c = 0; c < modes.size(); ++c) {
            for (u32 mask = 0; mask < 128; ++mask) {
                auto variant = base;
                variant.lighting.config.Assign(static_cast<Lighting::LightingConfig>(modes[c]));
                const std::array luts{&variant.lighting.lut_d0, &variant.lighting.lut_d1,
                                      &variant.lighting.lut_sp, &variant.lighting.lut_fr,
                                      &variant.lighting.lut_rr, &variant.lighting.lut_rg,
                                      &variant.lighting.lut_rb};
                for (u32 slot = 0; slot < luts.size(); ++slot) {
                    luts[slot]->enable.Assign((mask >> slot) & 1);
                }
                Verify(variant, profile);
                if (!Equal(MakeDynamicTevFamilyConfig(base, profile),
                           MakeDynamicTevFamilyConfig(variant, profile)) ||
                    (!Equal(base, variant) &&
                     Equal(MakeDynamicTevFamilyConfig(base, profile, LightingFamilyKey::Alpha13),
                           MakeDynamicTevFamilyConfig(variant, profile,
                                                      LightingFamilyKey::Alpha13)))) {
                    throw std::runtime_error("Lighting support/enable key reduction mismatch");
                }
                const auto state = MakeDynamicTevState(variant, profile);
                const u32 expected = (mask | 4U) & supported[c];
                for (u32 slot = 0; slot < luts.size(); ++slot) {
                    const u32 word = slot < 4 ? state.lighting_luts_lo : state.lighting_luts_hi;
                    if (((word >> ((slot % 4) * 8 + 7)) & 1) != ((expected >> slot) & 1)) {
                        throw std::runtime_error("Effective LUT enable transport mismatch");
                    }
                }
            }
        }
        auto variant = base;
        variant.lighting.src_num.Assign(2);
        CheckPair(base, variant, profile, true, "Runtime light count split a family");
        variant = base;
        variant.lighting.lights[0].directional.Assign(1);
        CheckPair(base, variant, profile, true, "Runtime directional flag split a family");
        // AstraEH: Cover both transport words and every slot bit independently;
        // compact/current keys merge these states while the 0.0.14 census does not.
        for (u32 count = 0; count <= 8; ++count) {
            for (u32 slot = 0; slot < 8; ++slot) {
                for (u32 bit = 0; bit < 7; ++bit) {
                    variant = base;
                    variant.lighting.src_num.Assign(count);
                    variant.lighting.lights[slot].raw = static_cast<u16>(1U << (bit + 3));
                    CheckPair(base, variant, profile, true,
                              "Runtime slot operation split a family");
                    const auto state = MakeDynamicTevState(variant, profile);
                    const u32 word = slot < 4 ? state.lighting_ops_lo : state.lighting_ops_hi;
                    if ((state.lighting_ops_lo >> 28) != count ||
                        ((word >> ((slot % 4) * 7)) & 127U) != (1U << bit) ||
                        Equal(MakeDynamicTevFamilyConfig(base, profile, LightingFamilyKey::Alpha14),
                              MakeDynamicTevFamilyConfig(variant, profile,
                                                         LightingFamilyKey::Alpha14))) {
                        throw std::runtime_error("Runtime slot transport/previous key mismatch");
                    }
                }
            }
        }
        variant = base;
        variant.lighting.src_num.Assign(9);
        if (SupportsDynamicTev(variant, UserConfig{})) {
            throw std::runtime_error("Out-of-range light count bypassed recovery");
        }
        variant = base;
        variant.lighting.bump_mode.Assign(Lighting::LightingBumpMode::NormalMap);
        CheckPair(base, variant, profile, false, "Bump structure was collapsed");
        variant = base;
        variant.lighting.enable_shadow.Assign(1);
        CheckPair(base, variant, profile, false, "Global shadow structure was collapsed");
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

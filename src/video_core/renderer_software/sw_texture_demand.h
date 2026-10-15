// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include "video_core/pica/regs_lighting.h"
#include "video_core/pica/uberhar_tev_liveness.h"

namespace SwRenderer {

// CodexAstraLocal: Share actual operation arity between preparation and pixel
// execution. Unused operand fields must not request sampling or source work.
inline u32 TevOperandCount(Pica::TexturingRegs::TevStageConfig::Operation op) {
    return Pica::TevLiveness::OperandCount(op);
}

// CodexAstraLocal: These values have paths to the final RGBA in the real pixel
// loop. Dead intermediates must not force texture reads or invalid UV errors.
struct FragmentDemand {
    u32 textures{};
    bool primary_color{};
    bool lighting{};
};

inline FragmentDemand GetFragmentDemand(const Pica::TevLiveness::Plan& plan,
                                       const Pica::LightingRegs& lighting) {
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    FragmentDemand demand;
    demand.textures = (plan.external_sources >> static_cast<u32>(Source::Texture0)) & 15U;
    demand.primary_color = plan.Uses(Source::PrimaryColor);
    demand.lighting = plan.Uses(Source::PrimaryFragmentColor) ||
                      plan.Uses(Source::SecondaryFragmentColor);
    // CodexAstraLocal: Enabled lighting without a TEV consumer cannot affect
    // output. Its bump/shadow samplers likewise have no consumed coordinates.
    demand.lighting &= !lighting.disable;
    if (demand.lighting) {
        if (lighting.config0.enable_shadow) {
            demand.textures |= 1U << lighting.config0.shadow_selector.Value();
        }
        if (lighting.config0.bump_mode != Pica::LightingRegs::LightingBumpMode::None) {
            demand.textures |= 1U << lighting.config0.bump_selector.Value();
        }
    }
    return demand;
}

inline FragmentDemand GetFragmentDemand(const Pica::TexturingRegs& texturing,
                                       const Pica::LightingRegs& lighting) {
    return GetFragmentDemand(Pica::TevLiveness::Analyze(
        texturing, Pica::TevLiveness::Stage0Policy::Software), lighting);
}

inline u32 TextureDemandMask(const Pica::TexturingRegs& texturing,
                            const Pica::LightingRegs& lighting) {
    return GetFragmentDemand(texturing, lighting).textures;
}

} // namespace SwRenderer

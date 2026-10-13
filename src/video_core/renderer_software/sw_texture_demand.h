// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <span>
#include "video_core/pica/regs_lighting.h"
#include "video_core/pica/regs_texturing.h"

namespace SwRenderer {

// CodexAstraLocal: Share actual operation arity between preparation and pixel
// execution. Unused operand fields must not request sampling or source work.
inline u32 TevOperandCount(Pica::TexturingRegs::TevStageConfig::Operation op) {
    using Stage = Pica::TexturingRegs::TevStageConfig;
    switch (op) {
    case Stage::Operation::Replace:
        return 1;
    case Stage::Operation::Modulate:
    case Stage::Operation::Add:
    case Stage::Operation::AddSigned:
    case Stage::Operation::Subtract:
    case Stage::Operation::Dot3_RGB:
    case Stage::Operation::Dot3_RGBA:
        return 2;
    default:
        return 3; // Unknown operations remain conservative.
    }
}

// CodexAstraLocal: These values have consumers in the real pixel loop. Keep all
// six stages, even overwritten results; this is not backward shader elimination.
struct FragmentDemand {
    u32 textures{};
    bool primary_color{};
    bool lighting{};
};

inline FragmentDemand GetFragmentDemand(
    std::span<const Pica::TexturingRegs::TevStageConfig, 6> stages,
    const Pica::LightingRegs& lighting) {
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    FragmentDemand demand;
    const auto consume = [&](Source source) {
        if (source >= Source::Texture0 && source <= Source::Texture3) {
            demand.textures |= 1U << (static_cast<u32>(source) - static_cast<u32>(Source::Texture0));
        }
        demand.primary_color |= source == Source::PrimaryColor;
        demand.lighting |= source == Source::PrimaryFragmentColor ||
                           source == Source::SecondaryFragmentColor;
    };
    for (u32 index = 0; index < stages.size(); ++index) {
        const auto& stage = stages[index];
        for (u32 operand = 0; operand < TevOperandCount(stage.color_op); ++operand) {
            auto source = static_cast<Source>((stage.sources_raw >> (4 * operand)) & 15U);
            if (index == 0 && operand < 2 && source == Source::Previous) {
                source = stage.color_source3;
            }
            consume(source);
        }
        // CodexAstraLocal: Dot3_RGBA supplies alpha from its RGB dot result;
        // the separate alpha inputs do not cause texture reads. Preserve the
        // existing Software alpha Previous behavior rather than changing TEV.
        if (stage.color_op != Stage::Operation::Dot3_RGBA) {
            for (u32 operand = 0; operand < TevOperandCount(stage.alpha_op); ++operand) {
                consume(static_cast<Source>((stage.sources_raw >> (16 + 4 * operand)) & 15U));
            }
        }
    }
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

inline u32 TextureDemandMask(std::span<const Pica::TexturingRegs::TevStageConfig, 6> stages,
                            const Pica::LightingRegs& lighting) {
    return GetFragmentDemand(stages, lighting).textures;
}

} // namespace SwRenderer

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <span>
#include "video_core/pica/regs_lighting.h"
#include "video_core/pica/regs_texturing.h"

namespace SwRenderer {

// CodexAstraLocal: Determine sampling demand once per triangle from the actual
// Software TEV/lighting consumers, not merely enabled samplers. This deliberately
// keeps every stage (including overwritten results); it is not shader elimination.
inline u32 TextureDemandMask(std::span<const Pica::TexturingRegs::TevStageConfig, 6> stages,
                            const Pica::LightingRegs& lighting) {
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    const auto arity = [](Stage::Operation op) -> u32 {
        switch (op) {
        case Stage::Operation::Replace: return 1;
        case Stage::Operation::Modulate:
        case Stage::Operation::Add:
        case Stage::Operation::AddSigned:
        case Stage::Operation::Subtract:
        case Stage::Operation::Dot3_RGB:
        case Stage::Operation::Dot3_RGBA: return 2;
        default: return 3; // Unknown operations remain conservative.
        }
    };
    u32 mask = 0;
    const auto consume = [&](Source source) {
        if (source >= Source::Texture0 && source <= Source::Texture3) {
            mask |= 1U << (static_cast<u32>(source) - static_cast<u32>(Source::Texture0));
        }
    };
    for (u32 index = 0; index < stages.size(); ++index) {
        const auto& stage = stages[index];
        for (u32 operand = 0; operand < arity(stage.color_op); ++operand) {
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
            for (u32 operand = 0; operand < arity(stage.alpha_op); ++operand) {
                consume(static_cast<Source>((stage.sources_raw >> (16 + 4 * operand)) & 15U));
            }
        }
    }
    if (!lighting.disable) {
        if (lighting.config0.enable_shadow) {
            mask |= 1U << lighting.config0.shadow_selector.Value();
        }
        if (lighting.config0.bump_mode != Pica::LightingRegs::LightingBumpMode::None) {
            mask |= 1U << lighting.config0.bump_selector.Value();
        }
    }
    return mask;
}

} // namespace SwRenderer

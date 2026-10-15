// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <span>
#include "video_core/pica/regs_texturing.h"

namespace Pica::TevLiveness {

using Config = TexturingRegs::TevStageConfig;

// CodexAstraLocal: The existing consumers differ at stage zero. Make that
// distinction explicit instead of changing either renderer's Previous semantics
// while removing computations with no path to the final fragment.
enum class Stage0Policy { Software, GeneratedGlsl };

inline u32 OperandCount(Config::Operation op) {
    switch (op) {
    case Config::Operation::Replace:
        return 1;
    case Config::Operation::Modulate:
    case Config::Operation::Add:
    case Config::Operation::AddSigned:
    case Config::Operation::Subtract:
    case Config::Operation::Dot3_RGB:
    case Config::Operation::Dot3_RGBA:
        return 2;
    default:
        return 3; // Retain conservative demand for unknown operations.
    }
}

struct Stage {
    bool rgb{};
    bool alpha{};

    // Dot3_RGBA alpha uses the unscaled RGB dot result, not the alpha combiner.
    bool NeedsColor(Config::Operation op) const {
        return rgb || (alpha && op == Config::Operation::Dot3_RGBA);
    }
};

struct Plan {
    std::array<Stage, 6> stages{};
    u32 external_sources{}; // Bit indexed by Config::Source, before unit enables.

    bool Uses(Config::Source source) const {
        return (external_sources & (1U << static_cast<u32>(source))) != 0;
    }
};

// CodexAstraLocal: Analyze pure TEV data flow once at the consuming draw or
// triangle. Final RGB and alpha remain live for fog, alpha/depth/stencil tests,
// blend/logic operations and shadow output. Never infer liveness from values:
// in particular, multiplication by zero does not make a NaN operand unused.
inline Plan Analyze(std::span<const Config, 6> stages, u32 rgb_update_mask,
                    u32 alpha_update_mask, Stage0Policy policy) {
    using Source = Config::Source;
    Plan plan;
    plan.stages.back() = {true, true};
    for (int index = 5; index >= 0; --index) {
        const auto& stage = stages[index];
        const auto require = [&](Source source, bool rgb, bool alpha) {
            if (source == Source::Previous) {
                if (index > 0) {
                    plan.stages[index - 1].rgb |= rgb;
                    plan.stages[index - 1].alpha |= alpha;
                } // Stage zero's unresolved Previous is the initial constant.
            } else if (source == Source::PreviousBuffer) {
                // CodexAstraLocal: The buffer is advanced AFTER evaluating a
                // stage. An update at j is first visible at j+2, independently
                // for RGB and alpha; an earlier unobserved write stays dead.
                const auto producer = [&](u32 mask, bool color) {
                    for (int prior = index - 2; prior >= 0; --prior) {
                        if (prior < 4 && (mask & (1U << prior))) {
                            if (color) plan.stages[prior].rgb = true;
                            else plan.stages[prior].alpha = true;
                            break;
                        }
                    }
                };
                if (rgb) producer(rgb_update_mask, true);
                if (alpha) producer(alpha_update_mask, false);
            } else if (source != Source::Constant) {
                plan.external_sources |= 1U << static_cast<u32>(source);
            }
        };
        if (plan.stages[index].NeedsColor(stage.color_op)) {
            for (u32 operand = 0; operand < OperandCount(stage.color_op); ++operand) {
                auto source = static_cast<Source>((stage.sources_raw >> (4 * operand)) & 15U);
                if (index == 0 && source == Source::Previous &&
                    (policy == Stage0Policy::GeneratedGlsl || operand < 2)) {
                    source = stage.color_source3;
                }
                const auto modifier = static_cast<Config::ColorModifier>(
                    (stage.modifiers_raw >> (4 * operand)) & 15U);
                const bool alpha = modifier == Config::ColorModifier::SourceAlpha ||
                                   modifier == Config::ColorModifier::OneMinusSourceAlpha;
                require(source, !alpha, alpha);
            }
        }
        if (plan.stages[index].alpha && stage.color_op != Config::Operation::Dot3_RGBA) {
            for (u32 operand = 0; operand < OperandCount(stage.alpha_op); ++operand) {
                auto source = static_cast<Source>((stage.sources_raw >> (16 + 4 * operand)) & 15U);
                if (index == 0 && source == Source::Previous &&
                    policy == Stage0Policy::GeneratedGlsl) {
                    source = stage.alpha_source3;
                }
                const auto modifier = static_cast<Config::AlphaModifier>(
                    (stage.modifiers_raw >> (12 + 4 * operand)) & 7U);
                const bool alpha = modifier == Config::AlphaModifier::SourceAlpha ||
                                   modifier == Config::AlphaModifier::OneMinusSourceAlpha;
                require(source, !alpha, alpha);
            }
        }
    }
    return plan;
}

inline Plan Analyze(const TexturingRegs& regs, Stage0Policy policy) {
    return Analyze(regs.GetTevStages(), regs.tev_combiner_buffer_input.update_mask_rgb,
                   regs.tev_combiner_buffer_input.update_mask_a, policy);
}

} // namespace Pica::TevLiveness

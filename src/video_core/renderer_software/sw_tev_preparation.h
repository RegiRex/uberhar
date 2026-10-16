// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include <array>
#include <optional>
#include "common/vector_math.h"
#include "video_core/renderer_software/sw_texture_demand.h"
namespace SwRenderer {
// CodexAstraLocal: Decode immutable TEV registers into a value-owned plan. Workers
// borrow it only until the existing full triangle join; no guest data is cached.
struct PreparedTevConfig {
    using Config = Pica::TexturingRegs::TevStageConfig;
    struct Stage {
        std::array<Config::Source, 3> color_sources, alpha_sources;
        std::array<Config::ColorModifier, 3> color_modifiers;
        std::array<Config::AlphaModifier, 3> alpha_modifiers;
        Common::Vec4<u8> constant;
        Config::Operation color_op, alpha_op;
        u32 color_count, alpha_count, color_multiplier, alpha_multiplier;
        bool update_color, update_alpha;
        Pica::TevLiveness::Stage live;
    };
    std::array<Stage, 6> stages;
    Common::Vec4<u8> initial_buffer;
    FragmentDemand demand;

    PreparedTevConfig(const Pica::TexturingRegs& texturing, const Pica::LightingRegs& lighting) {
        const auto config = texturing.GetTevStages();
        // CodexAstraLocal: One backward plan drives both sampler admission and
        // execution. Overwritten TEV intermediates cannot request dead textures,
        // while delayed buffer producers and cross-channel inputs stay live.
        const auto live = Pica::TevLiveness::Analyze(
            config, texturing.tev_combiner_buffer_input.update_mask_rgb,
            texturing.tev_combiner_buffer_input.update_mask_a,
            Pica::TevLiveness::Stage0Policy::Software);
        demand = GetFragmentDemand(live, lighting);
        const auto& buffer = texturing.tev_combiner_buffer_color;
        initial_buffer = {static_cast<u8>(buffer.r.Value()), static_cast<u8>(buffer.g.Value()),
                          static_cast<u8>(buffer.b.Value()), static_cast<u8>(buffer.a.Value())};
        for (u32 i = 0; i < stages.size(); ++i) {
            const auto& source = config[i];
            auto& stage = stages[i];
            stage.live = live.stages[i];
            stage.color_sources = {source.color_source1, source.color_source2, source.color_source3};
            stage.alpha_sources = {source.alpha_source1, source.alpha_source2, source.alpha_source3};
            // CodexAstraLocal: Only Software's first two RGB inputs remap
            // Previous at stage0. Alpha keeps the inherited initial-zero source.
            if (i == 0) {
                for (u32 input = 0; input < 2; ++input) {
                    if (stage.color_sources[input] == Config::Source::Previous) {
                        stage.color_sources[input] = source.color_source3;
                    }
                }
            }
            stage.color_modifiers = {source.color_modifier1, source.color_modifier2,
                                     source.color_modifier3};
            stage.alpha_modifiers = {source.alpha_modifier1, source.alpha_modifier2,
                                     source.alpha_modifier3};
            stage.constant = {static_cast<u8>(source.const_r.Value()),
                              static_cast<u8>(source.const_g.Value()),
                              static_cast<u8>(source.const_b.Value()),
                              static_cast<u8>(source.const_a.Value())};
            stage.color_op = source.color_op;
            stage.alpha_op = source.alpha_op;
            stage.color_count = TevOperandCount(stage.color_op);
            stage.alpha_count = stage.color_op == Config::Operation::Dot3_RGBA
                                    ? 0
                                    : TevOperandCount(stage.alpha_op);
            stage.color_multiplier = source.GetColorMultiplier();
            stage.alpha_multiplier = source.GetAlphaMultiplier();
            stage.update_color =
                texturing.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferColor(i);
            stage.update_alpha =
                texturing.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferAlpha(i);
        }
    }
};

// CodexAstraLocal: One exact last-state entry reuses only immutable arithmetic
// preparation. Every consumed field is copied explicitly, never object padding;
// guest mappings, texture bytes and feedback proofs remain fresh each triangle.
class PreparedTevCache {
public:
    const PreparedTevConfig& Get(const Pica::TexturingRegs& texturing,
                                 const Pica::LightingRegs& lighting, bool reuse = true, bool observe = true) {
        // CodexAstraLocal: Disabled reuse bypasses key work; diagnostics never select output.
        if (!reuse) {
            plan.emplace(texturing, lighting); key_valid = false;
            if (observe) ++generation;
            return *plan;
        }
        std::array<u32, 33> next{};
        u32 n{};
        for (const auto& stage : texturing.GetTevStages()) {
            next[n++] = stage.sources_raw; next[n++] = stage.modifiers_raw;
            next[n++] = stage.ops_raw; next[n++] = stage.const_color;
            next[n++] = stage.scales_raw;
        }
        next[n++] = texturing.tev_combiner_buffer_input.update_mask_rgb.Value() |
                    (texturing.tev_combiner_buffer_input.update_mask_a.Value() << 4);
        next[n++] = texturing.tev_combiner_buffer_color.raw;
        next[n++] = lighting.disable.Value() | (lighting.config0.enable_shadow.Value() << 1) |
                    (static_cast<u32>(lighting.config0.bump_mode.Value()) << 2) |
                    (lighting.config0.bump_selector.Value() << 4) |
                    (lighting.config0.shadow_selector.Value() << 6);
        if (key_valid && plan && key == next) { if (observe) ++hits; return *plan; }
        plan.emplace(texturing, lighting); key = next; key_valid = true;
        if (observe) ++generation;
        return *plan;
    }
    u64 Generation() const { return generation; }
    u64 Hits() const { return hits; }
private:
    std::array<u32, 33> key{};
    std::optional<PreparedTevConfig> plan;
    u64 generation{}, hits{};
    bool key_valid{};
};

} // namespace SwRenderer

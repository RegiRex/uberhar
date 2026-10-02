// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <optional>
#include "video_core/pica/uberhar_vertex_output.h"

namespace Pica {

// AstraPro: Reuse ONLY the pure register-to-semantic plan, never guest memory,
// input/default values, constants, output vertices or shader execution results.
// Compare the complete constructor inputs on every lookup, so restores and
// register edits need no dirty-bit or hash-collision assumptions. One entry
// bounds memory and avoids per-draw allocation in Native and CPU Combo recovery.
class NativeVertexPlanCache {
public:
    const NativeVertexPlan& Get(const ShaderRegs& shader, const RasterizerRegs& rasterizer) {
        const auto incoming = Capture(shader, rasterizer);
        if (plan && key == incoming) {
            ++hits;
        } else {
            plan.emplace(shader, rasterizer);
            key = incoming;
            ++builds;
        }
        return *plan;
    }

    u64 Hits() const { return hits; }
    u64 Builds() const { return builds; }
    void Reset() { plan.reset(); }

private:
    using Key = std::array<u32, 12>;
    static Key Capture(const ShaderRegs& shader, const RasterizerRegs& rasterizer) {
        Key result{};
        result[0] = shader.max_input_attribute_index.Value();
        result[1] = shader.input_attribute_to_register_map_low;
        result[2] = shader.input_attribute_to_register_map_high;
        result[3] = shader.output_mask.Value();
        result[4] = rasterizer.vs_output_total & 7;
        // AstraPro: NativeVertexPlan reads only active output-map words. Inactive
        // words stay zero here; changing an active count includes newly read maps.
        for (u32 i = 0; i < result[4]; ++i)
            result[5 + i] = rasterizer.vs_output_attributes[i].raw;
        return result;
    }
    Key key{};
    std::optional<NativeVertexPlan> plan;
    u64 hits{}, builds{};
};
} // namespace Pica

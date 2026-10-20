// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <optional>
#include <chrono> // CodexAstraLocal: Sparse pure mapping cost, never per vertex.
#include "video_core/pica/uberhar_vertex_output.h"

namespace Pica {

// AstraPro: Reuse ONLY the pure register-to-semantic plan, never guest memory,
// input/default values, constants, output vertices or shader execution results.
// Compare the complete constructor inputs on every lookup, so restores and
// register edits need no dirty-bit or hash-collision assumptions.
// CodexAstraLocal: Four exact mappings retain recurring layouts across intervening
// draws; the last-hit path stays first and no payload or executable is cached.
class NativeVertexPlanCache {
public:
    // CodexAstraLocal: This title-frozen switch prices complete lookup/build
    // separately from broader vertex planning; no source/output caching changes.
    explicit NativeVertexPlanCache(bool diagnostics = false) : diagnostics{diagnostics} {}
    const NativeVertexPlan& Get(const ShaderRegs& shader, const RasterizerRegs& rasterizer) {
        const bool measure = diagnostics && (++requests % 1024 == 0);
        if (!measure) return Lookup(shader, rasterizer);
        const auto start = std::chrono::steady_clock::now();
        const auto previous = builds;
        const auto& result = Lookup(shader, rasterizer);
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (builds != previous) { ++build_samples; build_ns += ns; }
        else { ++hit_samples; hit_ns += ns; }
        return result;
    }
private:
    const NativeVertexPlan& Lookup(const ShaderRegs& shader, const RasterizerRegs& rasterizer) {
        const auto incoming = Capture(shader, rasterizer);
        if (entries[last] && entries[last]->key == incoming) {
            ++hits;
            return entries[last]->plan;
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i] && entries[i]->key == incoming) {
                last = i;
                ++hits;
                ++history_hits;
                return entries[i]->plan;
            }
        }
        last = next++ % entries.size();
        evictions += entries[last].has_value();
        entries[last].emplace(Entry{incoming, NativeVertexPlan{shader, rasterizer}});
        ++builds;
        return entries[last]->plan;
    }

public:
    // CodexAstraLocal TEST-ONLY LOG: Remove cost census after CPU-only .48 mapping
    // qualification. Hit/build wall includes full lookup and is not saved time.
    u64 HitSamples() const { return hit_samples; }
    u64 HitNs() const { return hit_ns; }
    u64 BuildSamples() const { return build_samples; }
    u64 BuildNs() const { return build_ns; }
    u64 Hits() const { return hits; }
    u64 Builds() const { return builds; }
    // CodexAstraLocal TEST-ONLY LOG: Existing owner reports distinguish recurring
    // mapping reuse from adjacent hits; remove after CPU-only .48 hardware qualification.
    u64 HistoryHits() const { return history_hits; }
    u64 Evictions() const { return evictions; }
    void Reset() { for (auto& entry : entries) entry.reset(); last = next = 0; }

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
    struct Entry { Key key; NativeVertexPlan plan; };
    std::array<std::optional<Entry>, 4> entries;
    std::size_t last{}, next{};
    u64 hits{}, builds{}, history_hits{}, evictions{};
    const bool diagnostics;
    u64 requests{}, hit_samples{}, hit_ns{}, build_samples{}, build_ns{};
};
} // namespace Pica

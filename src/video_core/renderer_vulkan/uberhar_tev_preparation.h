// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#pragma once

#include <array>
#include <optional>
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"

namespace Vulkan {

// AstraEH: Cache only pure family/census preparation. Pipeline handles, readiness,
// per-draw constants and dynamic Vulkan state never enter this cache.
struct TevPreparation {
    Pica::Shader::FSConfig family;
    std::array<u64, 16> candidates;
    u32 light_counts;
};

// AstraEH: Preserve the pre-0.1.5 calculations and dimension order, including
// historical lighting keys and inactive native fields used by the census.
inline TevPreparation PrepareTev(const Pica::Shader::FSConfig& raw,
                                 const Common::HashableStruct<StaticPipelineInfo>& input,
                                 const Pica::Shader::Profile& profile, bool extended_dynamic) {
    namespace GLSL = Pica::Shader::Generator::GLSL;
    const auto family = GLSL::MakeDynamicTevFamilyConfig(raw, profile);
    const u64 raw_hash = raw.Hash();
    const u64 family_hash = family.Hash();
    auto pipeline = input;
    pipeline.state.shader_ids[1] = raw_hash;
    const u64 raw_pipeline =
        pipeline.state.ExecutionHash(extended_dynamic, pipeline.state.shader_ids);
    pipeline.state.shader_ids[1] = family_hash;
    const u64 canonical_pipeline =
        pipeline.state.ExecutionHash(extended_dynamic, pipeline.state.shader_ids);
    auto previous = family;
    previous.lighting = raw.lighting;
    const auto alpha13 =
        GLSL::MakeDynamicTevFamilyConfig(raw, profile, GLSL::LightingFamilyKey::Alpha13);
    const auto alpha14 =
        GLSL::MakeDynamicTevFamilyConfig(raw, profile, GLSL::LightingFamilyKey::Alpha14);
    const auto& state = input.state;
    // AstraEH: Stage order is VS/FS/GS, matching the released census and pipeline key.
    return {family,
            {raw_hash, family_hash, raw_pipeline, canonical_pipeline, state.shader_ids[0],
             state.shader_ids[2], Common::ComputeStructHash64(state.vertex_layout),
             Common::ComputeStructHash64(state.attachments),
             Common::ComputeStructHash64(state.blending),
             Common::ComputeStructHash64(state.rasterization),
             Common::ComputeStructHash64(state.depth_stencil), previous.Hash(),
             Common::ComputeStructHash64(family.lighting),
             Common::ComputeStructHash64(family.proctex), alpha13.Hash(), alpha14.Hash()},
            raw.lighting.enable ? 1U << raw.lighting.src_num.Value() : 0U};
}

// AstraEH: Fixed storage, renderer-thread-only. Hashes choose slots, never prove
// equality. Collisions/evictions recompute; a last-entry check also avoids hashing
// consecutive identical inputs. HashableStruct preserves native padding bytes.
template <std::size_t Capacity = 256> class TevPreparationCache {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);
    using PipelineKey = Common::HashableStruct<StaticPipelineInfo>;
    struct Entry {
        Pica::Shader::FSConfig raw;
        PipelineKey pipeline;
        TevPreparation prepared;
    };

public:
    struct Counters {
        u64 requests{}, hits{}, misses{}, evictions{}, invalidations{};
    };
    struct Result {
        const TevPreparation& prepared;
        bool reused;
    };
    static constexpr std::size_t Entries = Capacity;

    // AstraEH: Call at construction and every profile change, outside the draw
    // lookup. Full profile equality avoids a hand-maintained subset of fields.
    void Configure(const Pica::Shader::Profile& next, bool dynamic) {
        if (configured && profile == next && extended_dynamic == dynamic) {
            return;
        }
        ClearEntries();
        if (configured) {
            ++counters.invalidations;
        }
        profile = next;
        extended_dynamic = dynamic;
        configured = true;
    }

    Result Get(const Pica::Shader::FSConfig& raw, const PipelineKey& pipeline) {
        ++counters.requests;
        const auto matches = [&](const Entry& entry) {
            return entry.raw == raw && entry.pipeline == pipeline;
        };
        if (last < Capacity && entries[last] && matches(*entries[last])) {
            ++counters.hits;
            return {entries[last]->prepared, true};
        }
        const std::size_t slot = Common::HashCombine(raw.Hash(), pipeline.Hash()) & (Capacity - 1);
        last = slot;
        auto& entry = entries[slot];
        if (entry && matches(*entry)) {
            ++counters.hits;
            return {entry->prepared, true};
        }
        ++counters.misses;
        counters.evictions += entry.has_value();
        entry.emplace(Entry{raw, pipeline, PrepareTev(raw, pipeline, profile, extended_dynamic)});
        return {entry->prepared, false};
    }

    // AstraEH: Clear together with the title's census sets; an old hit must never
    // suppress the first observation in a new title. Keep the configured profile.
    void Reset() {
        ClearEntries();
        counters = {};
    }
    const Counters& Stats() const {
        return counters;
    }

private:
    void ClearEntries() {
        for (auto& entry : entries) {
            entry.reset();
        }
        last = Capacity;
    }
    std::array<std::optional<Entry>, Capacity> entries;
    std::size_t last{Capacity};
    Counters counters;
    Pica::Shader::Profile profile{};
    bool extended_dynamic{}, configured{};
};

} // namespace Vulkan

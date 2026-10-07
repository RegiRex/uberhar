// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include "video_core/pica/uberhar_vertex_output.h"
#include "video_core/pica/uberhar_vertex_timing.h"

namespace Pica {

// CodexAstraLocal: This optional Combo runner retains ONE complete draw-local
// FIFO/cache, live shader unit callbacks, and assembler across prefix/chunk/suffix.
// Never split a draw into calls to RunNativeVertexBatch: that would reset carry
// and cache semantics. The unchanged ordinary runner remains the disabled path.
template <typename VertexAt, typename Load, typename Run, typename Convert, typename Submit>
NativeVertexCounts RunTimedNativeVertexBatch(u32 count, bool indexed, VertexAt&& vertex_at,
    Load&& load, Run&& run, Convert&& convert, Submit&& submit,
    VertexTiming::Session& timing, VertexTiming::Range range) {
    VertexCacheIndex index;
    std::array<OutputVertex, VertexCacheIndex::Capacity> cache;
    NativeVertexCounts counts;
    for (u32 i = 0; i < count; ++i) {
        const bool observed = i >= range.first && i - range.first < range.count;
        if (i == range.first)
            timing.Begin(Common::UberharActivity::Capture());
        const u32 vertex = vertex_at(i);
        const int slot = indexed ? index.Find(static_cast<u16>(vertex)) : -1;
        if (observed) timing.Mark(VertexTiming::Stage::Lookup);
        OutputVertex result;
        const OutputVertex* output;
        if (slot >= 0) {
            output = &cache[slot];
            ++counts.hits;
        } else {
            const bool fused = load(vertex, i);
            if (observed) timing.InputRoute(fused);
            if (observed) timing.Mark(VertexTiming::Stage::Input);
            run();
            if (observed) timing.Mark(VertexTiming::Stage::Shader);
            result = convert();
            if (observed) timing.Mark(VertexTiming::Stage::Output);
            ++counts.invocations;
            if (indexed) {
                auto& cached = cache[index.Insert(static_cast<u16>(vertex))];
                cached = result;
                output = &cached;
            } else {
                output = &result;
            }
        }
        // CodexAstraLocal: Include hit selection/miss insertion and value copies
        // in the explicit cache interval. Submission includes AddTriangle work.
        if (observed) timing.Mark(VertexTiming::Stage::Cache);
        submit(*output);
        if (observed) {
            timing.Mark(VertexTiming::Stage::Submit);
            timing.Input(slot >= 0);
        }
        if (observed && i - range.first + 1 == range.count)
            timing.End(Common::UberharActivity::Capture());
    }
    return counts;
}
} // namespace Pica

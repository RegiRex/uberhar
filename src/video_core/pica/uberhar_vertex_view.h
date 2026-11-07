// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <span>
#include "video_core/pica/output_vertex.h"

namespace Pica {

// CodexAstraLocal: An owner consumes this immutable FIFO/result view immediately.
// It never escapes append, submits GPU work, or outlives its joined shader job.
// References below results.size() address this chunk; the others address the
// retained preceding-chunk FIFO. Subranges preserve that same index domain.
struct OrderedVertexView {
    std::span<const u32> references;
    std::span<const OutputVertex> results;
    std::span<const OutputVertex> previous;

    const OutputVertex& operator()(u32 index) const noexcept {
        const auto source = references[index];
        return source < results.size() ? results[source] : previous[source - results.size()];
    }
    u32 Size() const noexcept { return static_cast<u32>(references.size()); }
    OrderedVertexView Subspan(u32 start, u32 count) const noexcept {
        return {references.subspan(start, count), results, previous};
    }
};
} // namespace Pica

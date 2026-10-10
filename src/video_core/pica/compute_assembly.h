// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include "common/common_types.h"

namespace Pica {
// CodexAstraLocal: The existing PICA assembler remains the only persistent owner.
// Compute snapshots copy only live semantic slots, while completion imports only
// slots actually written. Unused, potentially uninitialized buffer bytes are never
// read for a snapshot or overwritten just because a draw emits no triangle.
struct ComputeAssemblyState {
    u32 topology{};
    u32 index{};
    bool ready{};
    bool winding{};
    std::array<u32, 48> words{};

    bool Valid() const noexcept {
        if (topology > 3) return false;
        if (topology == 0 || topology == 3) return index < 3 && !ready;
        return index < 2 && !(topology == 2 && ready && index != 1);
    }
    u32 LiveMask() const noexcept {
        if (topology == 0 || topology == 3) return (1U << index) - 1;
        return ready ? 3 : index ? 1 : 0;
    }
    u32 Triangles(u32 count) const noexcept {
        if (topology == 0 || topology == 3) return (index + count) / 3;
        const u32 total = (ready ? 2 : index) + count;
        return total > 2 ? total - 2 : 0;
    }
};

struct ComputeAssemblyResult {
    ComputeAssemblyState state;
    u32 written_mask{};
};

// CodexAstraLocal: Validate completion metadata with closed-form scalar state
// transitions, without executing guest shading, expanding triangles or touching
// vertex payloads on the CPU. The actual emitted order remains GPU-produced.
inline ComputeAssemblyResult ExpectedComputeAssembly(const ComputeAssemblyState& old, u32 count) {
    ComputeAssemblyResult result{};
    auto& next = result.state;
    next.topology = old.topology;
    next.index = old.index;
    next.ready = old.ready;
    next.winding = old.winding;
    if (!count) return result;
    if (old.topology == 0 || old.topology == 3) {
        result.written_mask = (count > (3 - old.index) % 3 ? 1U : 0U) |
                              (count > (4 - old.index) % 3 ? 2U : 0U);
        next.index = (old.index + count) % 3;
        if (old.topology == 3 && old.Triangles(count)) next.winding = false;
    } else {
        result.written_mask = old.topology == 1 ? (count > 1 ? 3U : 1U << old.index)
                                               : (old.index == 1 ? 2U : count > 1 ? 3U : 1U);
        next.ready = old.ready || count > (old.index == 1 ? 0U : 1U);
        next.index = old.topology == 1 ? old.index ^ (count & 1U) : 1U;
    }
    return result;
}
} // namespace Pica

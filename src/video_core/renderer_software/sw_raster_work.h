// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include <algorithm>
#include <array>
#include <limits>
#include "common/common_types.h"

namespace SwRenderer {
using RasterPoints = std::array<std::array<s32, 2>, 3>;
struct RasterSpan { u32 first{}, end{}; };

// CodexAstraLocal: Intersect the three exact integer edge inequalities on one
// pixel-center row. Covered pixels retain the original weights/FP operations.
// If legacy signed-int products or biased edges could overflow, retain its
// complete traversal rather than inventing different extreme-coordinate output.
inline RasterSpan CoveredRow(const RasterPoints& points, const std::array<s32, 3>& bias,
                             u32 x, u32 y, u32 columns, u64* unsafe_rows = nullptr) {
    if (!columns) return {};
    RasterSpan result{0, columns};
    constexpr s64 low = std::numeric_limits<s32>::min();
    constexpr s64 high = std::numeric_limits<s32>::max();
    for (u32 edge = 0; edge < 3; ++edge) {
        const auto& a = points[(edge + 1) % 3];
        const auto& b = points[(edge + 2) % 3];
        const s64 dx = s64(b[0]) - a[0], dy = s64(b[1]) - a[1];
        const s64 first_product = dx * (s64(y) - a[1]);
        const s64 second_product = dy * (s64(x) - a[0]);
        const s64 last_product = dy * (s64(x) + s64(columns - 1) * 16 - a[0]);
        const auto safe = [&](s64 value) { return value >= low && value <= high; };
        const s64 value = first_product - second_product + bias[edge];
        const s64 last = first_product - last_product + bias[edge];
        if (!safe(first_product) || !safe(second_product) || !safe(last_product) ||
            !safe(first_product - second_product) || !safe(first_product - last_product) ||
            !safe(value) || !safe(last)) {
            // CodexAstraLocal TEST-ONLY LOG: Count exact overflow refusals on the owner.
            // Remove this optional count after scanline coverage qualification.
            if (unsafe_rows) ++*unsafe_rows;
            return {0, columns};
        }
        const s64 step = -dy * 16;
        if (step > 0 && value < 0) {
            result.first = std::max(result.first,
                static_cast<u32>(std::min<s64>(columns, (-value + step - 1) / step)));
        } else if (step < 0) {
            const s64 end = value < 0 ? 0 : std::min<s64>(columns, value / -step + 1);
            result.end = std::min(result.end, static_cast<u32>(end));
        } else if (step == 0 && value < 0) {
            result.end = 0;
        }
    }
    if (result.first >= result.end) return {};
    return result;
}

struct RasterRowWork { u32 grain{}; bool parallel{}; };
// CodexAstraLocal: Bound dispatch by proven row-span candidates, not bounding
// box size or desired occupancy. Scissor/depth can reject more later; the work
// thresholds remain conservative proxies pending matched hardware timing.
inline RasterRowWork PlanRasterRows(u32 rows, u32 candidates) {
    if (!rows || !candidates) return {4, false};
    const u32 per_row = std::max(1U, candidates / rows);
    const u32 grain = std::clamp((512U + per_row - 1) / per_row, 4U, 32U);
    return {grain, candidates >= 1024 && rows / grain >= 2};
}
} // namespace SwRenderer

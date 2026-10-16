// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <cstdint>

namespace VideoCore {
// CodexAstraLocal: This is only the cheap shape gate. Production must also prove
// current RAM/no GPU dirtiness, pinned complete host spans, no host alias and no
// concurrent guest writer before any CPU copy. Keep size above tiny-invalidate
// flush behavior and cap dispatch/memory traffic for this measured experiment.
// CodexAstraLocal: Exclusive cheap refusals expose why no transfers qualify
// without weakening authoritative-RAM, complete-span or ownership checks.
enum class CpuTransferShape : std::uint32_t { Eligible, BelowSize, AboveSize, Gaps, Range, Overlap, Count };
constexpr CpuTransferShape ClassifyCpuRawTransfer(std::uint64_t src, std::uint64_t dst,
                                                 std::uint32_t size, std::uint32_t input_gap,
                                                 std::uint32_t output_gap) {
    constexpr std::uint64_t limit = std::uint64_t{1} << 32;
    if (input_gap || output_gap) return CpuTransferShape::Gaps;
    if (size < 16384) return CpuTransferShape::BelowSize;
    if (size > 8 * 1024 * 1024) return CpuTransferShape::AboveSize;
    if (src >= limit || dst >= limit || src + size >= limit || dst + size >= limit)
        return CpuTransferShape::Range;
    if (src < dst + size && dst < src + size) return CpuTransferShape::Overlap;
    return CpuTransferShape::Eligible;
}
constexpr bool CpuRawTransferShape(std::uint64_t src, std::uint64_t dst,
                                   std::uint32_t size, std::uint32_t input_gap,
                                   std::uint32_t output_gap) {
    return ClassifyCpuRawTransfer(src, dst, size, input_gap, output_gap) == CpuTransferShape::Eligible;
}
} // namespace VideoCore

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <cstdint>

namespace VideoCore {
// CodexAstraLocal: This is only the cheap shape gate. Production must also prove
// current RAM/no GPU dirtiness, pinned complete host spans, no host alias and no
// concurrent guest writer before any CPU copy. Keep size above tiny-invalidate
// flush behavior and cap dispatch/memory traffic for this measured experiment.
constexpr bool CpuRawTransferShape(std::uint64_t src, std::uint64_t dst,
                                   std::uint32_t size, std::uint32_t input_gap,
                                   std::uint32_t output_gap) {
    constexpr std::uint64_t limit = std::uint64_t{1} << 32;
    return !input_gap && !output_gap && size >= 16384 && size <= 8 * 1024 * 1024 &&
           src < limit && dst < limit && src + size < limit && dst + size < limit &&
           !(src < dst + size && dst < src + size);
}
} // namespace VideoCore

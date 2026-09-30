// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <optional>
#include <span>
#include "common/common_types.h"

namespace Pica {
// AstraPro: Rescan only when a conservative full 8/16-bit domain rejected the
// fused loader. This is bounded, reads only the admitted index bytes, and does
// not cache guest memory across draws. Unaligned little-endian indices are safe.
inline std::optional<u32> NativeIndexMaximum(std::span<const u8> bytes, u32 count,
                                             bool wide) {
    constexpr u32 MaxScannedIndices = 262144;
    const u32 width = wide ? 2 : 1;
    if (!count || count > MaxScannedIndices || !bytes.data() ||
        count > bytes.size() / width)
        return std::nullopt;
    u32 maximum = 0;
    for (u32 i = 0; i < count; ++i) {
        const auto* p = bytes.data() + static_cast<std::size_t>(i) * width;
        const u32 value = p[0] | (wide ? static_cast<u32>(p[1]) << 8 : 0);
        maximum = value > maximum ? value : maximum;
    }
    return maximum;
}
} // namespace Pica

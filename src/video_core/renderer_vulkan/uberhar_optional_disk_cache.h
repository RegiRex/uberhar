// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include <algorithm>
#include "video_core/renderer_vulkan/uberhar_spirv_cache.h"

namespace Vulkan::OptionalDiskCache {
// CodexAstraLocal: Persist an exact reconstruction identity with framed SPIR-V.
// Hashes locate fixed disk slots; equality of every identity word admits reuse.
constexpr u32 Magic = 0x554F5054, Schema = 1;
constexpr std::size_t MaxIdentityWords = 2048, MaxProgramWords = 64 * 1024;
constexpr std::size_t MaxBytes = (MaxIdentityWords + MaxProgramWords + 16) * sizeof(u32);
inline std::vector<u32> Encode(std::span<const u32> identity, u64 compiler,
                               std::span<const u32> code) {
    if (identity.empty() || identity.size() > MaxIdentityWords || code.size() > MaxProgramWords)
        return {};
    const UberharSpirvCache::Key key{Common::ComputeHash64(identity.data(), identity.size_bytes()), compiler};
    auto program = UberharSpirvCache::Encode(key, code);
    if (program.empty()) return {};
    std::vector<u32> result{Magic, Schema, static_cast<u32>(identity.size()), 0};
    result.insert(result.end(), identity.begin(), identity.end());
    result.insert(result.end(), program.begin(), program.end());
    return result;
}
inline std::vector<u32> Decode(std::span<const u32> identity, u64 compiler,
                               std::span<const u32> words) {
    if (identity.empty() || identity.size() > MaxIdentityWords || words.size_bytes() > MaxBytes ||
        words.size() < 4 + identity.size() || words[0] != Magic || words[1] != Schema ||
        words[2] != identity.size() || words[3] != 0 ||
        !std::equal(identity.begin(), identity.end(), words.begin() + 4)) return {};
    const UberharSpirvCache::Key key{Common::ComputeHash64(identity.data(), identity.size_bytes()), compiler};
    auto code = UberharSpirvCache::Decode(key, words.subspan(4 + identity.size()));
    if (code.size() > MaxProgramWords) return {};
    return code;
}
} // namespace Vulkan::OptionalDiskCache

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <span>
#include <vector>
#include "common/hash.h"

namespace Vulkan::UberharSpirvCache {
// AstraEH: A bounded cache envelope, independent of driver binaries. Source and
// compiler-setting fingerprints prevent reuse across incompatible generated programs.
// The checksum detects damaged local files; it is not an authentication mechanism.
constexpr u32 Magic = 0x55535056;
constexpr u32 Schema = 1;
constexpr std::size_t HeaderWords = 10;
constexpr std::size_t MaxCodeWords = 256 * 1024;
constexpr std::size_t MaxFileBytes = (HeaderWords + MaxCodeWords) * sizeof(u32);
struct Key {
    u64 source{}, compiler{};
};
inline u64 Join(u32 lo, u32 hi) {
    return u64{lo} | (u64{hi} << 32);
}
inline bool FramedSpirv(std::span<const u32> code) {
    if (code.size() < 5 || code.size() > MaxCodeWords || code[0] != 0x07230203 ||
        code[1] < 0x10000 || code[1] > 0x10600 || code[3] == 0 || code[4] != 0)
        return false;
    for (std::size_t i = 5; i < code.size();) {
        const u32 count = code[i] >> 16;
        if (count == 0 || count > code.size() - i)
            return false;
        i += count;
    }
    return true;
}
inline std::vector<u32> Encode(Key key, std::span<const u32> code) {
    if (!FramedSpirv(code))
        return {};
    const u64 checksum = Common::ComputeHash64(code.data(), code.size_bytes());
    std::vector<u32> result{Magic,
                            Schema,
                            u32(key.source),
                            u32(key.source >> 32),
                            u32(key.compiler),
                            u32(key.compiler >> 32),
                            u32(code.size()),
                            0,
                            u32(checksum),
                            u32(checksum >> 32)};
    result.insert(result.end(), code.begin(), code.end());
    return result;
}
inline std::vector<u32> Decode(Key key, std::span<const u32> words) {
    if (words.size() < HeaderWords || words.size_bytes() > MaxFileBytes || words[0] != Magic ||
        words[1] != Schema || Join(words[2], words[3]) != key.source ||
        Join(words[4], words[5]) != key.compiler || words[7] != 0 ||
        words[6] != words.size() - HeaderWords)
        return {};
    const auto code = words.subspan(HeaderWords);
    if (!FramedSpirv(code) ||
        Join(words[8], words[9]) != Common::ComputeHash64(code.data(), code.size_bytes()))
        return {};
    return {code.begin(), code.end()};
}
} // namespace Vulkan::UberharSpirvCache

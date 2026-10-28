// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <vector>
#include <cryptopp/sha.h>
#include "common/common_types.h"

namespace FileSys {

// CodexAstraLocal: Verify decrypted NCCH regions before replacing installed content.
// A present but wrong key must not turn a valid encrypted input into a published corrupt output.
class NCCHInstallVerifier {
public:
    explicit NCCHInstallVerifier(u64 content_size) : content_size(content_size) {}

    // CodexAstraLocal: Hash spans are fixed by the original header and cannot exceed the content.
    bool Add(u64 offset, u64 size, std::span<const u8, 32> expected) {
        if (size == 0 || offset > content_size || size > content_size - offset) {
            failed = true;
            return false;
        }
        Region region{};
        region.offset = offset;
        region.size = size;
        std::copy(expected.begin(), expected.end(), region.expected.begin());
        regions.push_back(std::move(region));
        return true;
    }

    // CodexAstraLocal: Feed actual transformed output, preserving streaming order across chunk boundaries.
    bool Update(u64 offset, std::span<const u8> data) {
        if (offset > content_size || data.size() > content_size - offset) {
            failed = true;
            return false;
        }
        for (auto& region : regions) {
            const u64 begin = std::max(offset, region.offset);
            const u64 end = std::min(offset + data.size(), region.offset + region.size);
            if (begin >= end) {
                continue;
            }
            if (begin != region.offset + region.received) {
                failed = true;
                return false;
            }
            region.hash.Update(data.data() + (begin - offset), end - begin);
            region.received += end - begin;
        }
        return !failed;
    }

    // CodexAstraLocal: Require complete matching regions; absent data and wrong primary/secondary keys fail.
    bool Verify() {
        if (failed || regions.empty()) {
            return false;
        }
        for (auto& region : regions) {
            std::array<u8, 32> actual{};
            if (region.received != region.size) {
                return false;
            }
            region.hash.Final(actual.data());
            if (actual != region.expected) {
                return false;
            }
        }
        return true;
    }

private:
    struct Region {
        u64 offset{};
        u64 size{};
        u64 received{};
        std::array<u8, 32> expected{};
        CryptoPP::SHA256 hash;
    };
    u64 content_size{};
    std::vector<Region> regions;
    bool failed = false;
};

} // namespace FileSys

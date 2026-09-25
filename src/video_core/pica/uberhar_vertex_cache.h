// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <array>
#include <cassert>
#include "common/common_types.h"

namespace Pica {
// AstraEH: Index the existing 64-slot FIFO without changing its hit/eviction order.
// Fixed bucket chains avoid a 64-entry scan per index and allocate no heap memory.
// Payloads remain with the caller; this index is reset for every draw batch.
class VertexCacheIndex {
public:
    static constexpr unsigned Capacity = 64;
    VertexCacheIndex() {
        heads.fill(-1);
    }
    int Find(u16 key) const {
        for (int slot = heads[Bucket(key)]; slot >= 0; slot = next[slot]) {
            if (keys[slot] == key)
                return slot;
        }
        return -1;
    }
    // Only insert a cache miss. Return the exact FIFO payload slot to overwrite.
    unsigned Insert(u16 key) {
        const unsigned slot = cursor;
        if (size == Capacity) {
            auto* link = &heads[Bucket(keys[slot])];
            while (*link != static_cast<s16>(slot)) {
                assert(*link >= 0);
                link = &next[*link];
            }
            *link = next[slot];
        } else {
            ++size;
        }
        const auto bucket = Bucket(key);
        keys[slot] = key;
        next[slot] = heads[bucket];
        heads[bucket] = static_cast<s16>(slot);
        cursor = (cursor + 1) % Capacity;
        return slot;
    }

private:
    static unsigned Bucket(u16 key) {
        return (key ^ (key >> 7)) & 127;
    }
    std::array<s16, 128> heads;
    std::array<s16, Capacity> next;
    std::array<u16, Capacity> keys;
    unsigned cursor{}, size{};
};
} // namespace Pica

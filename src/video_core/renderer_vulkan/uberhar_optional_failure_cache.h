// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace Vulkan {
// CodexAstraLocal: Failed compiler metadata does not occupy a live module slot.
// Exact equality prevents a hash collision from rejecting unrelated programs.
// The shared work budget/circuit bounds retries even when this fixed cache evicts.
template<class Key, std::size_t Capacity> class OptionalFailureCache {
public:
    struct Entry {
        std::optional<Key> key;
        std::uint64_t hash{}, retry_after_ms{};
        unsigned retries{};
        bool retryable{};
    };
    const Entry* Find(std::uint64_t hash, const Key& key) const {
        const auto& e = entries[hash % Capacity];
        return e.key && e.hash == hash && *e.key == key ? &e : nullptr;
    }
    void Remember(std::uint64_t hash, const Key& key, unsigned retries,
                  bool retryable, std::uint64_t after) {
        entries[hash % Capacity] = {key, hash, after, retries, retryable};
    }
    void Forget(std::uint64_t hash, const Key& key) {
        if (Find(hash, key)) entries[hash % Capacity] = {};
    }
private:
    std::array<Entry, Capacity> entries{};
};
} // namespace Vulkan

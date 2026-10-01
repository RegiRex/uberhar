// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include "common/common_types.h"

namespace Vulkan::ReadyFragmentPolicy {
// AstraPro: Bound optional specialization independently of mandatory recovery.
// Repeated draw demand, not FPS, controls admission. A miss always retains a CPU
// draw; this gate cannot select a shader or infer that specialization is faster.
constexpr std::size_t MaxModules = 128;
constexpr u32 WarmupDraws = 16;
constexpr bool PreferSpecialized(bool force_generic, bool cacheable) {
    return !force_generic && cacheable;
}
// AstraPro: Only an optional GPU specialization can omit generic transport.
// Any failed promotion re-enters the CPU draw with ready_gpu=false.
constexpr bool NeedsDynamicTransport(bool covered, bool ready_gpu, bool prefer_specialized) {
    return covered && !(ready_gpu && prefer_specialized);
}
constexpr bool CanAdmit(std::size_t resident, bool pending) {
    return resident < MaxModules && !pending;
}

template <typename Key, std::size_t Capacity = 64, u32 Threshold = WarmupDraws>
class DemandGate {
    static_assert(Capacity > 0 && Threshold > 0);
    struct Entry {
        std::optional<Key> key;
        u64 hash{};
        u32 count{};
    };
public:
    // AstraPro: A hash collision can defer work, never alias two shader configs.
    // Saturate counts to avoid wrap and retain no guest pointers or vertex data.
    bool Observe(u64 hash, const Key& key) {
        auto& entry = entries[hash % Capacity];
        if (!entry.key || entry.hash != hash || !(*entry.key == key)) {
            replacements += entry.key.has_value();
            entry = Entry{key, hash, 1};
        } else if (entry.count < Threshold) {
            ++entry.count;
        }
        return entry.count >= Threshold;
    }
    u64 Replacements() const { return replacements; }
private:
    std::array<Entry, Capacity> entries{};
    u64 replacements{};
};
} // namespace Vulkan::ReadyFragmentPolicy

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

// Claude: Set-associative with least-recently-used replacement. The former
// direct-mapped table let two hot configs whose hashes shared a slot reset each
// other's count on every draw, so neither ever reached the threshold and both
// stayed on the generic shader. Same capacity; Ways keys per set coexist.
template <typename Key, std::size_t Capacity = 64, u32 Threshold = WarmupDraws,
          std::size_t Ways = 4>
class DemandGate {
    static_assert(Capacity > 0 && Threshold > 0 && Ways > 0 && Capacity % Ways == 0);
    static constexpr std::size_t Sets = Capacity / Ways;
    struct Entry {
        std::optional<Key> key;
        u64 hash{};
        u32 count{};
        u64 used{};
    };
public:
    // AstraPro: A hash collision can defer work, never alias two shader configs.
    // Saturate counts to avoid wrap and retain no guest pointers or vertex data.
    bool Observe(u64 hash, const Key& key) {
        Entry* const set = entries.data() + (hash % Sets) * Ways;
        ++clock;
        Entry* victim = nullptr;
        for (std::size_t way = 0; way < Ways; ++way) {
            Entry& entry = set[way];
            if (entry.key && entry.hash == hash && *entry.key == key) {
                entry.used = clock;
                if (entry.count < Threshold)
                    ++entry.count;
                return entry.count >= Threshold;
            }
            // Claude: Prefer an empty way, then the least recently observed key.
            if (!victim || (victim->key && (!entry.key || entry.used < victim->used)))
                victim = &entry;
        }
        replacements += victim->key.has_value();
        *victim = Entry{key, hash, 1, clock};
        return victim->count >= Threshold;
    }
    u64 Replacements() const { return replacements; }
private:
    std::array<Entry, Capacity> entries{};
    u64 clock{};
    u64 replacements{};
};
} // namespace Vulkan::ReadyFragmentPolicy

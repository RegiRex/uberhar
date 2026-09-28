// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <atomic>
#include <string_view>
#include "common/common_types.h"

namespace Common::UberharActivity {
// AstraEH: Host-only evidence shared by frontend, core and renderer. A game does
// not expose a universal loading flag. Only frontend startup or a user's explicit
// marker confirms a phase; read/submission activity is never a rendering policy.
enum class Phase : u32 { Unknown, Startup, Loading, Gameplay, Mixed, Count };
inline constexpr std::array<std::string_view, 5> Names{"unknown", "startup_loading", "user_loading",
                                                       "user_gameplay", "mixed"};
inline constexpr std::size_t PhaseCount = Names.size();
inline std::atomic<u64> state{}, session{}, read_requests{}, requested_bytes{}, submissions{};

struct Snapshot {
    u64 token{}, run{}, reads{}, bytes{}, presents{};
    Phase GetPhase() const {
        return (token & 4U) ? Phase::Startup : static_cast<Phase>(token & 3U);
    }
};

// AstraEH: Reset only at a new PerfStats lifetime, after the previous run drains.
inline void Reset(u64 run) {
    state.store(0);
    read_requests.store(0);
    requested_bytes.store(0);
    submissions.store(0);
    session.store(run);
}
inline Snapshot Capture() {
    return {state.load(), session.load(), read_requests.load(std::memory_order_relaxed),
            requested_bytes.load(std::memory_order_relaxed),
            submissions.load(std::memory_order_relaxed)};
}
inline u32 ManualPhase() {
    return static_cast<u32>(state.load() & 3U);
}
// AstraEH: One CAS updates both phase and generation. A Loading->Gameplay->Loading
// change during a wait remains mixed, even if endpoint labels happen to match.
inline void Change(u64 mask, u64 bits) {
    auto previous = state.load();
    while ((previous & mask) != bits) {
        const auto next = ((previous + 8U) & ~mask) | bits;
        if (state.compare_exchange_weak(previous, next))
            return;
    }
}
inline bool SetManualPhase(u32 phase) {
    if (phase != 0 && phase != 2 && phase != 3)
        return false;
    Change(3U, phase);
    return true;
}
inline void SetStartup(bool enabled) {
    Change(4U, enabled ? 4U : 0U);
}
inline void NoteRead(u64 bytes) {
    // AstraEH: Requested guest bytes include cache hits/failures, not physical I/O.
    read_requests.fetch_add(1, std::memory_order_relaxed);
    requested_bytes.fetch_add(bytes, std::memory_order_relaxed);
}
inline Phase Between(const Snapshot& first, const Snapshot& last) {
    return first.run == last.run && first.token == last.token ? first.GetPhase() : Phase::Mixed;
}
inline std::string_view Name(Phase phase) {
    return Names[static_cast<std::size_t>(phase)];
}
// AstraEH: A conservative clue, explicitly not a confirmed loading-screen label.
// Games can stream while playing or animate a loading screen at full frame rate.
inline std::string_view Evidence(u64 reads, u64 bytes, u64 frames, u64 wall_ns) {
    if (reads && bytes >= 1024 * 1024 && wall_ns &&
        static_cast<double>(frames) * 1e9 / wall_ns <= 10.0)
        return "loading_candidate";
    if (reads)
        return "guest_read_activity";
    return frames ? "presenting_unknown" : "no_submissions";
}
} // namespace Common::UberharActivity

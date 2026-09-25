// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <algorithm>
#include <array>
#include "common/common_types.h"

namespace Core {
// AstraEH: Fixed-size frame accounting independent of the overlay's resettable statistics.
// Times are supplied by the caller so pause, clock and savestate boundaries are testable.
// These are emulator frame-end intervals, not Android display presentation timestamps.
class UberharFrameDiagnostics {
public:
    struct Counters {
        u64 frames{}, game_frames{}, wall_ns{}, guest_us{}, work_ns{};
        u64 max_interval_ns{}, max_work_ns{};
        double limit_min{}, limit_max{};
        u64 temporary_limit_frames{};
        std::array<u64, 8> intervals{};
    };
    struct SlowFrame {
        u64 frame{}, end_ns{}, interval_ns{}, work_ns{};
    };

    void Observe(u64 now_ns, s64 guest_us, u64 game_frames, u64 work_ns, double frame_limit,
                 bool temporary_limit) {
        if (!anchored || now_ns <= last_ns || guest_us < last_guest_us ||
            game_frames < last_game_frames) {
            ++excluded_intervals;
            if (anchored)
                ++discontinuities;
        } else {
            const u64 elapsed = now_ns - last_ns;
            const u64 guest_elapsed = static_cast<u64>(guest_us - last_guest_us);
            const u64 games = game_frames - last_game_frames;
            Add(window, elapsed, guest_elapsed, games, work_ns, frame_limit, temporary_limit);
            Add(total, elapsed, guest_elapsed, games, work_ns, frame_limit, temporary_limit);
            // AstraEH: Retain late severe hitches without per-frame output or an unbounded trace.
            if (elapsed >= 50'000'000 && elapsed > worst.back().interval_ns) {
                std::size_t slot = worst.size() - 1;
                while (slot > 0 && elapsed > worst[slot - 1].interval_ns) {
                    worst[slot] = worst[slot - 1];
                    --slot;
                }
                worst[slot] = {total.frames, now_ns, elapsed, work_ns};
            }
        }
        anchored = true;
        last_ns = now_ns;
        last_guest_us = guest_us;
        last_game_frames = game_frames;
    }

    // AstraEH: Discard the interval crossing an explicit pause; retain all completed samples.
    void BreakInterval() {
        anchored = false;
    }
    void ResetWindow() {
        window = {};
    }
    const Counters& Window() const {
        return window;
    }
    const Counters& Total() const {
        return total;
    }
    u64 ExcludedIntervals() const {
        return excluded_intervals;
    }
    u64 Discontinuities() const {
        return discontinuities;
    }
    const std::array<SlowFrame, 8>& Worst() const {
        return worst;
    }

private:
    static void Add(Counters& out, u64 elapsed, u64 guest, u64 games, u64 work, double frame_limit,
                    bool temporary_limit) {
        if (out.frames == 0)
            out.limit_min = out.limit_max = frame_limit;
        out.limit_min = std::min(out.limit_min, frame_limit);
        out.limit_max = std::max(out.limit_max, frame_limit);
        out.temporary_limit_frames += temporary_limit;
        ++out.frames;
        out.game_frames += games;
        out.wall_ns += elapsed;
        out.guest_us += guest;
        out.work_ns += work;
        out.max_interval_ns = std::max(out.max_interval_ns, elapsed);
        out.max_work_ns = std::max(out.max_work_ns, work);
        constexpr std::array<u64, 7> limits{16'666'667,  33'333'334,  50'000'000,   100'000'000,
                                            250'000'000, 500'000'000, 1'000'000'000};
        std::size_t bucket = 0;
        while (bucket < limits.size() && elapsed >= limits[bucket])
            ++bucket;
        ++out.intervals[bucket];
    }
    Counters window, total;
    std::array<SlowFrame, 8> worst{};
    bool anchored{};
    u64 last_ns{}, last_game_frames{}, excluded_intervals{}, discontinuities{};
    s64 last_guest_us{};
};
} // namespace Core

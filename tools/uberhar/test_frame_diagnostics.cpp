// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Synthetic timing sequences test real accounting without sleeping or depending on FPS.
#include <cmath>
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include "core/uberhar_frame_diagnostics.h"

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    Core::UberharFrameDiagnostics stats;
    u64 now = 0, games = 0;
    s64 guest = 0;
    stats.Observe(now, guest, games, 999'000'000, 100, false);
    // 250 frames at 50 Hz: each 20 ms host interval advances 10 ms in the guest.
    for (int i = 0; i < 250; ++i) {
        now += 20'000'000;
        guest += 10'000;
        games += (i % 2 == 0);
        stats.Observe(now, guest, games, 12'000'000, i < 100 ? 100 : 300, i >= 100);
    }
    const auto first = stats.Window();
    Check(first.frames == 250 && first.game_frames == 125, "Frame/submission count mismatch");
    Check(first.wall_ns == 5'000'000'000ULL && first.guest_us == 2'500'000,
          "Incorrect speed/FPS interval");
    Check(first.work_ns == 3'000'000'000ULL && first.max_work_ns == 12'000'000,
          "Work costs changed or startup work included");
    Check(first.intervals[1] == 250 && first.max_interval_ns == 20'000'000,
          "Incorrect pacing histogram");
    Check(first.limit_min == 100 && first.limit_max == 300 && first.temporary_limit_frames == 150,
          "Fast-forward window lost");
    stats.ResetWindow();
    Check(stats.Window().frames == 0 && stats.Total().frames == 250,
          "Window reset corrupted totals");

    // A ten-minute pause and its crossing frame must not become a shader stall.
    stats.BreakInterval();
    now += 600'000'000'000ULL;
    stats.Observe(now, guest, games, 600'000'000'000ULL, 100, false);
    Check(stats.Total().frames == 250 && stats.ExcludedIntervals() == 2,
          "Pause/startup exclusion failed");
    now += 20'000'000;
    guest += 10'000;
    stats.Observe(now, guest, ++games, 12'000'000, 100, false);
    Check(stats.Window().wall_ns == 20'000'000, "Pause leaked into next window");
    // Guest rewind, clock reset, and explicit forward-load boundary also re-anchor.
    stats.Observe(now + 20'000'000, 1, games, 800'000'000, 100, false);
    stats.Observe(0, 2, games, 800'000'000, 100, false);
    stats.BreakInterval();
    stats.Observe(1, 900'000'000, games, 800'000'000, 100, false);
    Check(stats.Discontinuities() == 2 && stats.Total().frames == 251,
          "Clock/savestate discontinuity was counted as rendering");

    // Each bound belongs to the next bin, while the nanosecond below it remains below.
    Core::UberharFrameDiagnostics boundaries;
    constexpr std::array<u64, 7> limits{16'666'667,  33'333'334,  50'000'000,   100'000'000,
                                        250'000'000, 500'000'000, 1'000'000'000};
    now = 0;
    boundaries.Observe(now, 0, 0, 0, 100, false);
    for (u64 bound : limits) {
        now += bound - 1;
        boundaries.Observe(now, 0, 0, 0, 100, false);
        now += bound;
        boundaries.Observe(now, 0, 0, 0, 100, false);
    }
    const auto bins = boundaries.Total().intervals;
    Check(bins.front() == 1 && bins.back() == 1, "Outer pacing boundaries incorrect");
    for (std::size_t i = 1; i + 1 < bins.size(); ++i)
        Check(bins[i] == 2, "Inner pacing boundaries incorrect");
    Check(std::accumulate(bins.begin(), bins.end(), u64{}) == 14, "Intervals lost");
    // Bounded retention must keep a late, worse hitch beyond the first eight slow events.
    for (u64 i = 1; i <= 40; ++i) {
        now += 1'000'000'000 + i;
        boundaries.Observe(now, 0, 0, i, 100, false);
    }
    const auto worst = boundaries.Worst();
    for (std::size_t i = 0; i < worst.size(); ++i)
        Check(worst[i].interval_ns == 1'000'000'040 - i && worst[i].work_ns == 40 - i,
              "Late worst-frame retention/order failed");
    Check(stats.Worst().front().interval_ns == 0, "Explicit pause became a worst-frame hitch");
    std::puts(
        "PASS: pacing, speed, fast-forward, pause, clock/state boundaries and late worst frames");
}

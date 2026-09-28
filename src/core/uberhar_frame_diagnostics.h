// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <algorithm>
#include <array>
#include "common/common_types.h"
#include "common/uberhar_activity.h" // AstraEH: Explicit phase and guest-I/O context.

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
        // AstraEH: Keep unknown/mixed time visible instead of hiding it as loading.
        std::array<u64, Common::UberharActivity::PhaseCount> phase_frames{};
        u64 read_requests{}, requested_bytes{};
    };
    struct SlowFrame {
        u64 frame{}, end_ns{}, interval_ns{}, work_ns{};
        Common::UberharActivity::Phase phase{Common::UberharActivity::Phase::Unknown};
        u64 reads{}, bytes{}, submissions{};
    };

    void Observe(u64 now_ns, s64 guest_us, u64 game_frames, u64 work_ns, double frame_limit,
                 bool temporary_limit, Common::UberharActivity::Snapshot activity = {}) {
        if (!anchored || now_ns <= last_ns || guest_us < last_guest_us ||
            game_frames < last_game_frames) {
            ++excluded_intervals;
            if (anchored)
                ++discontinuities;
        } else {
            const u64 elapsed = now_ns - last_ns;
            const u64 guest_elapsed = static_cast<u64>(guest_us - last_guest_us);
            const u64 games = game_frames - last_game_frames;
            const auto phase = Common::UberharActivity::Between(last_activity, activity);
            const u64 reads =
                activity.reads >= last_activity.reads ? activity.reads - last_activity.reads : 0;
            const u64 bytes =
                activity.bytes >= last_activity.bytes ? activity.bytes - last_activity.bytes : 0;
            Add(window, elapsed, guest_elapsed, games, work_ns, frame_limit, temporary_limit, phase,
                reads, bytes);
            Add(total, elapsed, guest_elapsed, games, work_ns, frame_limit, temporary_limit, phase,
                reads, bytes);
            Add(phase_bands[static_cast<std::size_t>(phase)], elapsed, guest_elapsed, games,
                work_ns, frame_limit, temporary_limit, phase, reads, bytes);
            // AstraEH: Separate normal, fast-forward and uncapped throughput without
            // per-frame logging. A limit transition remains in overall timing, but
            // cannot be assigned wholly to either band, so exclude it from bands.
            if (frame_limit == last_limit && temporary_limit == last_temporary) {
                const std::size_t band = frame_limit == 0 ? 2 : frame_limit > 100 ? 1 : 0;
                Add(bands[band], elapsed, guest_elapsed, games, work_ns, frame_limit,
                    temporary_limit, phase, reads, bytes);
            } else {
                ++limit_transitions;
            }
            // AstraEH: Loading must not evict all gameplay evidence. Retain the
            // global eight plus four per explicit phase, all fixed-size.
            if (elapsed >= 50'000'000) {
                const SlowFrame event{total.frames, now_ns, elapsed, work_ns,
                                      phase,        reads,  bytes,   games};
                Retain(worst, event);
                Retain(phase_worst[static_cast<std::size_t>(phase)], event);
            }
        }
        anchored = true;
        last_ns = now_ns;
        last_guest_us = guest_us;
        last_game_frames = game_frames;
        last_limit = frame_limit;
        last_temporary = temporary_limit;
        last_activity = activity;
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
    // AstraEH: Three fixed, lifetime bands; explicit pauses never enter them.
    const std::array<Counters, 3>& Bands() const {
        return bands;
    }
    // AstraEH: Lifetime phase bands retain every valid interval, including mixed boundaries.
    const auto& Phases() const {
        return phase_bands;
    }
    const auto& PhaseWorst() const {
        return phase_worst;
    }
    u64 LimitTransitions() const {
        return limit_transitions;
    }
    const std::array<SlowFrame, 8>& Worst() const {
        return worst;
    }

private:
    // AstraEH: Same stable ordering in both bounded retention banks.
    template <std::size_t Size>
    static void Retain(std::array<SlowFrame, Size>& bank, const SlowFrame& event) {
        if (event.interval_ns <= bank.back().interval_ns)
            return;
        std::size_t slot = Size - 1;
        while (slot > 0 && event.interval_ns > bank[slot - 1].interval_ns) {
            bank[slot] = bank[slot - 1];
            --slot;
        }
        bank[slot] = event;
    }
    static void Add(Counters& out, u64 elapsed, u64 guest, u64 games, u64 work, double frame_limit,
                    bool temporary_limit, Common::UberharActivity::Phase phase, u64 reads,
                    u64 bytes) {
        if (out.frames == 0)
            out.limit_min = out.limit_max = frame_limit;
        out.limit_min = std::min(out.limit_min, frame_limit);
        out.limit_max = std::max(out.limit_max, frame_limit);
        out.temporary_limit_frames += temporary_limit;
        ++out.frames;
        ++out.phase_frames[static_cast<std::size_t>(phase)];
        out.read_requests += reads;
        out.requested_bytes += bytes;
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
    std::array<Counters, Common::UberharActivity::PhaseCount> phase_bands{};
    Common::UberharActivity::Snapshot last_activity{};
    std::array<Counters, 3> bands{};
    double last_limit{};
    bool last_temporary{};
    u64 limit_transitions{};
    std::array<SlowFrame, 8> worst{};
    std::array<std::array<SlowFrame, 4>, Common::UberharActivity::PhaseCount> phase_worst{};
    bool anchored{};
    u64 last_ns{}, last_game_frames{}, excluded_intervals{}, discontinuities{};
    s64 last_guest_us{};
};
} // namespace Core

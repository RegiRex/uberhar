// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Constant-memory wait distribution and worst-event retention. Record
// only actual scheduler waits; these are not GPU/frame-time measurements.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include "common/common_types.h"

namespace Vulkan {

struct PipelineWaitRecord {
    u64 elapsed_ns{};
    u64 start_ns{};
    u64 draw{};
    u64 wait_key{};
    u64 chosen_key{};
    u32 stages{};
    u32 phase{};
    u32 alternative_phase{};
    u32 alternative_stages{};
    u32 topology{};
    bool alternative{};
    bool fallback{};
};

class PipelineWaitDiagnostics {
public:
    static constexpr std::array<u64, 7> UpperBounds{1000000,   16666667,  50000000,  100000000,
                                                    250000000, 500000000, 1000000000};
    static constexpr std::size_t WorstCount = 8;

    void Record(const PipelineWaitRecord& event) {
        const auto bucket =
            std::upper_bound(UpperBounds.begin(), UpperBounds.end(), event.elapsed_ns) -
            UpperBounds.begin();
        histogram[bucket].fetch_add(1, std::memory_order_relaxed);
        if (event.elapsed_ns < 50000000)
            return;
        // AstraEH: Only actual slow waits take this lock. Keeping eight records
        // avoids logging every event yet retains late-session stalls after the cap.
        std::scoped_lock lock{mutex};
        if (event.elapsed_ns <= worst.back().elapsed_ns)
            return;
        worst.back() = event;
        std::sort(worst.begin(), worst.end(),
                  [](const auto& a, const auto& b) { return a.elapsed_ns > b.elapsed_ns; });
    }

    std::array<u64, 8> Histogram() const {
        std::array<u64, 8> counts{};
        for (std::size_t i = 0; i < counts.size(); ++i) {
            counts[i] = histogram[i].load(std::memory_order_relaxed);
        }
        return counts;
    }

    std::array<PipelineWaitRecord, WorstCount> Worst() const {
        std::scoped_lock lock{mutex};
        return worst;
    }

private:
    std::array<std::atomic<u64>, 8> histogram{};
    mutable std::mutex mutex;
    std::array<PipelineWaitRecord, WorstCount> worst{};
};

} // namespace Vulkan

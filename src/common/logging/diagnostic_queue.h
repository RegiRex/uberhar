// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>

#include "common/logging/types.h"

namespace Common::Log {

// CodexAstraUlt: Producers keep one fixed-size omission counter; the existing
// consumer reports it at most once per five seconds, plus flush/shutdown barriers.
// Only the consumer accesses the cadence or acknowledges records actually written.
class DiagnosticDropCounter {
public:
    using Clock = std::chrono::steady_clock;

    void RecordDrop() {
        pending.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t Pending() const {
        return pending.load(std::memory_order_relaxed);
    }

    std::uint64_t Due(Clock::time_point now, bool force) {
        if (!force && now < next_report) {
            return 0;
        }
        const auto count = Pending();
        if (count) {
            next_report = now + std::chrono::seconds{5};
        }
        return count;
    }

    // CodexAstraUlt: Subtract only the reported snapshot, retaining drops that
    // arrived during output or an entire snapshot when its sink was unavailable.
    void Acknowledge(std::uint64_t count) {
        pending.fetch_sub(count, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> pending{0};
    Clock::time_point next_report{};
};

// CodexAstraUlt: Keep reliable lifecycle/error records on their existing path.
// Explicit diagnostics never wait for queue capacity or producer ownership, even
// when synchronous debug logging is enabled. Formatting still precedes this call.
template <typename Queue, typename Entry, typename SynchronousWriter>
void SubmitEntry(Queue& queue, Entry&& entry, Delivery delivery, bool instant_debug,
                 DiagnosticDropCounter& omissions, SynchronousWriter&& synchronous_write) {
    if (delivery == Delivery::Diagnostic) {
        if (!queue.TryEmplace(std::forward<Entry>(entry))) {
            omissions.RecordDrop();
        }
    } else if (instant_debug) {
        synchronous_write(entry);
    } else {
        queue.EmplaceWait(std::forward<Entry>(entry));
    }
}

} // namespace Common::Log

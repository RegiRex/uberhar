// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "common/logging/types.h"

namespace Common::Log {

// CodexAstraLocal: Fixed record families identify missing progress without a map,
// allocation, clock or extra producer work when the diagnostic enqueue succeeds.
enum class DiagnosticFamily : std::size_t {
    CpuDraw, Prefix, Decode, Adaptive, Fragment, GpuIdentity, GpuCost, Software, Other, Count,
};
inline constexpr auto DiagnosticFamilyCount = static_cast<std::size_t>(DiagnosticFamily::Count);

inline DiagnosticFamily ClassifyDiagnosticFamily(std::string_view message) {
    struct FamilyPrefix {
        std::string_view text;
        DiagnosticFamily family;
    };
    using enum DiagnosticFamily;
    // CodexAstraLocal: Match actual producer prefixes, with specific exceptions
    // before broader rows. Prefix covers the whole combined CPU parallel record;
    // deferred CPU draws (which also contains prefix counters) remains CpuDraw.
    static constexpr std::array prefixes{
        FamilyPrefix{"Uberhar CPU adaptive history ", Adaptive},
        FamilyPrefix{"Uberhar vertex scheduling ", Adaptive},
        FamilyPrefix{"Uberhar texture adaptation:", Adaptive},
        FamilyPrefix{"Uberhar CPU parallel ", Prefix},
        FamilyPrefix{"Uberhar CPU raw copy:", Decode},
        FamilyPrefix{"Uberhar CPU transfer:", Decode},
        FamilyPrefix{"Uberhar texture decode:", Decode},
        FamilyPrefix{"Uberhar CPU PSO census:", Fragment},
        FamilyPrefix{"Uberhar CPU ", CpuDraw},
        FamilyPrefix{"Uberhar deferred CPU draws ", CpuDraw},
        FamilyPrefix{"Uberhar GPU host attempts ", CpuDraw},
        FamilyPrefix{"Uberhar virtual vertices ", CpuDraw},
        FamilyPrefix{"Uberhar native vertices ", CpuDraw},
        FamilyPrefix{"Uberhar vertex input ", CpuDraw},
        FamilyPrefix{"Uberhar index bounds ", CpuDraw},
        FamilyPrefix{"Uberhar PICA routes ", CpuDraw},
        FamilyPrefix{"Uberhar GPU admission ", CpuDraw},
        FamilyPrefix{"Uberhar native plan ", CpuDraw},
        FamilyPrefix{"Uberhar ready GPU vertices ", CpuDraw},
        FamilyPrefix{"Uberhar fragment ", Fragment},
        FamilyPrefix{"Uberhar shader preparation ", Fragment},
        FamilyPrefix{"Uberhar push constants ", Fragment},
        FamilyPrefix{"Uberhar ready CPU fragments ", Fragment},
        FamilyPrefix{"Uberhar ready GPU fragments ", Fragment},
        FamilyPrefix{"Uberhar static CPU TEV modules ", Fragment},
        FamilyPrefix{"Uberhar lit module admission ", Fragment},
        FamilyPrefix{"Uberhar adaptive CPU PSOs ", Fragment},
        FamilyPrefix{"Uberhar TEV plan ", Fragment},
        FamilyPrefix{"Uberhar preparation ", Fragment},
        FamilyPrefix{"Uberhar virtual native ", Fragment},
        FamilyPrefix{"Uberhar recovery reasons ", Fragment},
        FamilyPrefix{"Uberhar variant census ", Fragment},
        FamilyPrefix{"Uberhar fallback utility ", Fragment},
        FamilyPrefix{"Uberhar GPU shader admission ", Fragment},
        FamilyPrefix{"Uberhar generic modules ", Fragment},
        FamilyPrefix{"Uberhar cache use ", Fragment},
        FamilyPrefix{"Uberhar GPU graphics detail:", GpuIdentity},
        FamilyPrefix{"Uberhar GPU cost ", GpuCost},
        FamilyPrefix{"Uberhar host cost ", GpuCost},
        FamilyPrefix{"Uberhar wait histogram ", GpuCost},
        FamilyPrefix{"Uberhar worst wait:", GpuCost},
        FamilyPrefix{"Uberhar compile phase ", GpuCost},
        FamilyPrefix{"Uberhar build ", GpuCost},
        FamilyPrefix{"Software ", Software},
        FamilyPrefix{"Uberhar software ", Software},
    };
    for (const auto& prefix : prefixes) {
        if (message.starts_with(prefix.text)) {
            return prefix.family;
        }
    }
    return Other;
}

// CodexAstraLocal: The family array is the sole accounting authority, so the total
// always equals the reported bins even when producers race reporting. Only the
// consumer accesses cadence and acknowledges snapshots written to a healthy sink.
class DiagnosticDropCounter {
public:
    using Clock = std::chrono::steady_clock;

    struct Snapshot {
        std::array<std::uint64_t, DiagnosticFamilyCount> families{};

        std::uint64_t operator[](DiagnosticFamily family) const {
            return families[static_cast<std::size_t>(family)];
        }

        std::uint64_t Total() const {
            std::uint64_t total{};
            for (const auto count : families) total += count;
            return total;
        }
    };

    void RecordDrop(DiagnosticFamily family = DiagnosticFamily::Other) {
        pending[static_cast<std::size_t>(family)].fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t Pending() const {
        return Capture().Total();
    }

    Snapshot Due(Clock::time_point now, bool force) {
        if (!force && now < next_report) {
            return {};
        }
        const auto snapshot = Capture();
        if (snapshot.Total()) {
            next_report = now + std::chrono::seconds{5};
        }
        return snapshot;
    }

    // CodexAstraLocal: Subtract each reported bin, retaining same-family and
    // other-family drops arriving during output. Failed sinks acknowledge nothing.
    void Acknowledge(const Snapshot& snapshot) {
        for (std::size_t i = 0; i < pending.size(); ++i) {
            pending[i].fetch_sub(snapshot.families[i], std::memory_order_relaxed);
        }
    }

private:
    Snapshot Capture() const {
        Snapshot snapshot;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            snapshot.families[i] = pending[i].load(std::memory_order_relaxed);
        }
        return snapshot;
    }

    std::array<std::atomic<std::uint64_t>, DiagnosticFamilyCount> pending{};
    Clock::time_point next_report{};
};

// CodexAstraUlt: Keep reliable lifecycle/error records on their existing path.
// Explicit diagnostics never wait for queue capacity or producer ownership, even
// when synchronous debug logging is enabled. Formatting still precedes this call.
template <typename Queue, typename Entry, typename SynchronousWriter>
void SubmitEntry(Queue& queue, Entry&& entry, Delivery delivery, bool instant_debug,
                 DiagnosticDropCounter& omissions, SynchronousWriter&& synchronous_write,
                 std::string_view diagnostic_message = {}) {
    if (delivery == Delivery::Diagnostic) {
        if (!queue.TryEmplace(std::forward<Entry>(entry))) {
            // CodexAstraLocal: TryEmplace preserves entry ownership on failure;
            // only that path examines its message. Successful moves never use the view.
            omissions.RecordDrop(ClassifyDiagnosticFamily(diagnostic_message));
        }
    } else if (instant_debug) {
        synchronous_write(entry);
    } else {
        queue.EmplaceWait(std::forward<Entry>(entry));
    }
}

} // namespace Common::Log

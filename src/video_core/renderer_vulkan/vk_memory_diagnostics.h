// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <atomic>
#include <span>
#include "common/common_types.h"

namespace Vulkan {

// CodexAstraUlt: Sum only VMA's explicit allocation statistics. The budget wrapper's
// usage/budget may be fallback estimates, so this helper cannot expose them as memory.
struct MemoryHeapTotals {
    u64 blocks{}, allocations{}, block_bytes{}, allocation_bytes{};
};

template <typename HeapBudget>
MemoryHeapTotals SumMemoryHeapStatistics(std::span<const HeapBudget> heaps) noexcept {
    MemoryHeapTotals total;
    for (const auto& heap : heaps) {
        total.blocks += heap.statistics.blockCount;
        total.allocations += heap.statistics.allocationCount;
        total.block_bytes += heap.statistics.blockBytes;
        total.allocation_bytes += heap.statistics.allocationBytes;
    }
    return total;
}

// CodexAstraUlt: Allocation-event counters never inspect a resource container or wait for
// the GPU. Each field is atomic; a concurrent snapshot is not a transaction. Units are
// explicit at each call site (bytes for memory, object capacity for descriptor/command pools).
class MemoryDiagnosticCounter {
public:
    struct Snapshot {
        u64 current{}, peak{}, allocations{}, frees{};
    };

    void Allocate(u64 amount) noexcept {
        const u64 now = current.fetch_add(amount, std::memory_order_relaxed) + amount;
        auto previous = peak.load(std::memory_order_relaxed);
        while (previous < now &&
               !peak.compare_exchange_weak(previous, now, std::memory_order_relaxed)) {}
        allocations.fetch_add(1, std::memory_order_relaxed);
    }

    void Free(u64 amount) noexcept {
        current.fetch_sub(amount, std::memory_order_relaxed);
        frees.fetch_add(1, std::memory_order_relaxed);
    }

    Snapshot Read() const noexcept {
        return {current.load(std::memory_order_relaxed), peak.load(std::memory_order_relaxed),
                allocations.load(std::memory_order_relaxed), frees.load(std::memory_order_relaxed)};
    }

private:
    std::atomic<u64> current{}, peak{}, allocations{}, frees{};
};

} // namespace Vulkan

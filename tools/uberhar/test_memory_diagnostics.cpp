// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#include <array>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>
#include "video_core/renderer_vulkan/vk_memory_diagnostics.h"

// CodexAstraUlt: Exercise the production event counters under simultaneous ownership
// changes and snapshots. No fake GPU allocation result is treated as driver evidence.
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    constexpr unsigned workers = 8;
    constexpr unsigned iterations = 20000;
    Vulkan::MemoryDiagnosticCounter counter;
    std::barrier owned{workers + 1};
    std::barrier release{workers + 1};
    std::atomic<unsigned> done{};
    std::vector<std::jthread> threads;
    for (unsigned worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&] {
            counter.Allocate(4096);
            owned.arrive_and_wait();
            release.arrive_and_wait();
            counter.Free(4096);
            for (unsigned i = 0; i < iterations; ++i) {
                counter.Allocate(128);
                counter.Free(128);
            }
            done.fetch_add(1);
        });
    }
    owned.arrive_and_wait();
    auto snapshot = counter.Read();
    Check(snapshot.current == workers * 4096 && snapshot.peak == workers * 4096,
          "Simultaneously owned bytes were not measured");
    Check(snapshot.allocations == workers && snapshot.frees == 0,
          "Allocation events lost before concurrent churn");
    release.arrive_and_wait();
    while (done.load() != workers) {
        snapshot = counter.Read();
        Check(snapshot.current <= workers * 4096 && snapshot.peak == workers * 4096,
              "Concurrent sample overflowed or lost its peak");
        std::this_thread::yield();
    }
    threads.clear();
    snapshot = counter.Read();
    Check(snapshot.current == 0 && snapshot.allocations == workers * (iterations + 1) &&
              snapshot.frees == snapshot.allocations && snapshot.peak == workers * 4096,
          "Balanced ownership did not return to zero with retained event history");

    // CodexAstraUlt: Object capacities count each successful batch once, while a new
    // renderer counter starts empty instead of inheriting a previous renderer's peak.
    Vulkan::MemoryDiagnosticCounter capacity;
    capacity.Allocate(64);
    capacity.Allocate(32);
    capacity.Free(96);
    snapshot = capacity.Read();
    Check(snapshot.current == 0 && snapshot.peak == 96 && snapshot.allocations == 2 &&
              snapshot.frees == 1, "Batch capacity was confused with event count");
    Vulkan::MemoryDiagnosticCounter next_generation;
    Check(next_generation.Read().peak == 0 && next_generation.Read().allocations == 0,
          "A new counter inherited a previous generation");

    // CodexAstraUlt: VMA wrappers contain estimates named usage/budget. Deliberately
    // misleading values must not affect actual explicit block/suballocation totals.
    struct Heap {
        struct Statistics { unsigned blockCount, allocationCount; u64 blockBytes, allocationBytes; } statistics;
        u64 usage, budget;
    };
    const std::array<Heap, 2> heaps{{{{2, 5, 4096, 3000}, 999999, 888888},
                                  {{1, 7, 8192, 6000}, 777777, 666666}}};
    const auto total = Vulkan::SumMemoryHeapStatistics(std::span<const Heap>{heaps});
    Check(total.blocks == 3 && total.allocations == 12 && total.block_bytes == 12288 &&
              total.allocation_bytes == 9000, "Heap total included budget heuristics");
    const auto empty = Vulkan::SumMemoryHeapStatistics(std::span<const Heap>{});
    Check(empty.blocks == 0 && empty.allocation_bytes == 0, "Empty heap sum is not zero");
    std::puts("Memory diagnostics: concurrent allocation balances, capacity batches, generation reset and explicit heap statistics passed");
}

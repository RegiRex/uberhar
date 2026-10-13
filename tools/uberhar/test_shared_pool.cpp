// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// Claude: Vertex batches and texture decoding now share one emulator pool. Prove
// that a nested or overlapping job runs exactly once on its own owner instead of
// overwriting the published job or deadlocking, and that a per-job participant
// cap bounds the threads one caller may use without shrinking the pool.
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#include "common/uberhar_parallel_work.h"

static void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    using Common::Uberhar::ParallelWork;
    ParallelWork pool{6};

    // Claude: A callback that re-enters the same pool must not wait on itself.
    {
        std::vector<std::atomic<unsigned>> outer(4096), inner(4096);
        const auto result = pool.Run(4096, 64, [&](std::uint32_t first, std::uint32_t last) {
            for (auto i = first; i < last; ++i)
                ++outer[i];
            if (first == 0) {
                const auto nested = pool.Run(4096, 64, [&](std::uint32_t a, std::uint32_t b) {
                    for (auto i = a; i < b; ++i)
                        ++inner[i];
                });
                Require(nested.worker_items == 0 && nested.owner_items == 4096,
                        "nested job used the busy pool's workers");
            }
        });
        for (std::size_t i = 0; i < outer.size(); ++i)
            Require(outer[i] == 1 && inner[i] == 1, "nested job lost or repeated items");
        Require(result.owner_items + result.worker_items == 4096, "outer job lost items");
        Require(pool.BusyFallbacks() == 1, "nested fallback not counted");
    }

    // Claude: Two owners on different threads share one pool. Each job must
    // complete exactly once whichever owner wins the pool.
    {
        constexpr unsigned Jobs = 200;
        std::atomic<std::uint64_t> total{};
        auto owner = [&] {
            for (unsigned job = 0; job < Jobs; ++job) {
                std::vector<std::atomic<unsigned>> seen(2048);
                pool.Run(2048, 32, [&](std::uint32_t first, std::uint32_t last) {
                    for (auto i = first; i < last; ++i)
                        ++seen[i];
                });
                for (auto& value : seen)
                    Require(value == 1, "overlapping owners corrupted a job");
                total += 2048;
            }
        };
        std::thread second{owner};
        owner();
        second.join();
        Require(total == 2ULL * Jobs * 2048, "overlapping owners lost a job");
    }

    // Claude: A participant cap limits one job without changing the allowance.
    {
        std::mutex mutex;
        std::set<std::thread::id> threads;
        const auto result = pool.Run(1 << 16, 16, [&](std::uint32_t, std::uint32_t) {
            std::lock_guard lock{mutex};
            threads.insert(std::this_thread::get_id());
        }, false, 3);
        Require(result.working_threads <= 3 && threads.size() <= 3, "participant cap exceeded");
        Require(result.owner_items + result.worker_items == (1U << 16), "capped job lost items");
        Require(pool.Available() == 6, "cap changed the pool allowance");
    }

    // Claude: An exception in an exclusive job releases the pool for the next owner.
    {
        bool threw = false;
        try {
            pool.Run(4096, 64, [](std::uint32_t first, std::uint32_t) {
                if (first == 0)
                    throw std::runtime_error("callback failure");
            });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        Require(threw, "callback failure not propagated");
        const auto before = pool.BusyFallbacks();
        std::atomic<unsigned> done{};
        const auto after = pool.Run(4096, 64, [&](std::uint32_t first, std::uint32_t last) {
            done += last - first;
        });
        Require(done == 4096 && after.owner_items + after.worker_items == 4096 &&
                    pool.BusyFallbacks() == before,
                "failed job kept the pool busy");
    }

    std::cout << "shared pool PASS: nested fallback, overlapping owners, participant cap, "
                 "failure release\n";
}

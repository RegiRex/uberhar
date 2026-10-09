// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Exercise the actual sleeping worker pool against independent
// exact-once, thread-participation, FP-environment and exception-lifetime oracles.
#include <atomic>
#include <barrier>
#include <bit>
#include <cfenv>
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
    const auto original = Common::Uberhar::ParallelFloatEnvironment::Capture();
    Require(Common::Uberhar::AvailableProcessors() >= 1, "processor discovery");
    // CodexAstraLocal: A naming failure must not strand joined work. Enabled
    // traps must keep even a large job entirely on the owner, without executing
    // faulting arithmetic in this test. Restore controls before assertions.
    {
        ParallelWork pool{2, +[] { throw std::runtime_error("name unavailable"); }};
        std::atomic<unsigned> done{};
        const auto result = pool.Run(2048, 32, [&](unsigned first, unsigned last) {
            done += last - first;
        });
        Require(done == 2048 && result.owner_items + result.worker_items == 2048,
                "naming failure stranded work");
#if defined(__i386__) || defined(__x86_64__)
        std::feclearexcept(FE_ALL_EXCEPT);
        const auto masked = Common::Uberhar::ParallelFloatEnvironment::Capture();
        _mm_setcsr((_mm_getcsr() & ~0x3fU) & ~(1U << 9));
        const bool sse_refused = !Common::Uberhar::ParallelFloatEnvironment::AllowsParallel();
        const auto serial = pool.Run(4096, 32, [](auto, auto) {});
        masked.Apply();
        Require(sse_refused && serial.owner_items == 4096 && serial.worker_items == 0,
                "SSE traps dispatched to workers");
        unsigned short control;
        asm volatile("fnstcw %0" : "=m"(control));
        control &= ~(1U << 2);
        asm volatile("fldcw %0" : : "m"(control));
        const bool x87_refused = !Common::Uberhar::ParallelFloatEnvironment::AllowsParallel();
        masked.Apply();
        Require(x87_refused, "x87 trap not detected");
#endif
    }
    // CodexAstraLocal: Generate a denormal-operand flag only on the worker, once
    // in SSE and once in x87. Both operations are exact and leave all standard
    // FE_ALL_EXCEPT bits clear, exposing the old portable-mask omission.
#if defined(__i386__) || defined(__x86_64__)
    {
        ParallelWork pool{2};
        const auto owner = std::this_thread::get_id();
        for (bool sse : {true, false}) {
            original.Apply();
            std::feclearexcept(FE_ALL_EXCEPT);
            asm volatile("fnclex");
            _mm_setcsr((_mm_getcsr() & ~(0x3fU | (1U << 6) | (1U << 15))) | 0x1f80U);
            std::barrier meet{2};
            const auto result = pool.Run(2, 1, [&](auto, auto) {
                if (std::this_thread::get_id() != owner) {
                    float value = std::bit_cast<float>(std::uint32_t{1});
                    if (sse) {
                        const float one = 1.0f;
                        asm volatile("mulss %1, %0" : "+x"(value) : "x"(one));
                    } else {
                        asm volatile("flds %0; fstp %%st(0)" : : "m"(value) : "st");
                    }
                }
                meet.arrive_and_wait();
            });
            const auto flags = Common::Uberhar::ParallelFloatEnvironment::Flags();
            Require(result.owner_items == 1 && result.worker_items == 1, "denormal worker missing");
            Require((flags & 2U) == (sse ? 2U : 0U), "SSE denormal status lost or invented");
            Require((flags & (2U << 16)) == (sse ? 0U : (2U << 16)),
                    "x87 denormal status lost or invented");
            Require(std::fetestexcept(FE_ALL_EXCEPT) == 0, "denormal control raised standard flags");
        }
        original.Apply();
    }
#endif
    // CodexAstraLocal: ARM's input-denormal and saturation flags are outside
    // portable FE_ALL_EXCEPT. Raise each only on a real worker, while the owner
    // performs no arithmetic, so a missing raw FPSR merge cannot pass by chance.
#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    {
        ParallelWork pool{2};
        const auto owner = std::this_thread::get_id();
        for (bool denormal : {true, false}) {
            const std::uint64_t control = denormal ? (1ULL << 24) : 0;
            constexpr std::uint64_t prior_status = 1U << 4;
            asm volatile("msr fpcr, %0; msr fpsr, %1" : : "r"(control), "r"(prior_status)
                         : "memory");
            std::barrier meet{2};
            const auto result = pool.Run(2, 1, [&](auto, auto) {
                if (std::this_thread::get_id() != owner) {
                    if (denormal) {
                        float value = std::bit_cast<float>(std::uint32_t{1});
                        const float one = 1.0f;
                        asm volatile("fmul %s0, %s0, %s1" : "+w"(value) : "w"(one));
                    } else {
                        asm volatile("movi v0.16b, #127; movi v1.16b, #1; "
                                     "sqadd v0.16b, v0.16b, v1.16b" : : : "v0", "v1");
                    }
                }
                meet.arrive_and_wait();
            });
            const auto after = Common::Uberhar::ParallelFloatEnvironment::Capture();
            const std::uint64_t expected = prior_status | (1ULL << (denormal ? 7 : 27));
            Require(result.owner_items == 1 && result.worker_items == 1,
                    "ARM sticky-status worker missing");
            Require(after.control == control, "ARM worker merge changed owner control");
            Require(after.status == expected, "ARM worker-only sticky status lost or invented");
        }
        original.Apply();
    }
#endif
    unsigned jobs = 0;
    for (unsigned cores : {1U, 2U, 6U, 8U, 12U, 17U}) {
        ParallelWork pool{cores};
        Require(pool.CreatedWorkers() == 0, "workers created without work");
        auto zero = pool.Run(0, 32, [](auto, auto) { throw std::runtime_error("empty callback"); });
        Require(zero.owner_items == 0 && zero.worker_items == 0, "empty accounting");
        // CodexAstraLocal: A single grain per participant and a common barrier
        // require every configured thread to execute; timing cannot fake coverage.
        std::barrier meet{static_cast<std::ptrdiff_t>(cores)};
        std::mutex seen_mutex;
        std::set<std::thread::id> seen;
        auto all = pool.Run(cores * 32, 32, [&](unsigned first, unsigned last) {
            Require(last - first == 32, "grain bounds");
            {
                std::lock_guard lock{seen_mutex};
                seen.insert(std::this_thread::get_id());
            }
            meet.arrive_and_wait();
        });
        Require(seen.size() == cores && all.working_threads == cores, "missing participant");
        Require(all.owner_items == 32 && all.worker_items == (cores - 1) * 32,
                "actual owner/worker accounting");
        ++jobs;
        for (unsigned round = 0; round < 120; ++round) {
            const unsigned count = (round * 193 + 17) % 12003;
            std::vector<std::atomic<unsigned>> visits(count);
            std::vector<std::uint64_t> values(count);
            // CodexAstraLocal: Change rounding after pool creation so accidental
            // inheritance or stale previous-job state fails, including serial jobs.
            const int rounding = round % 2 ? FE_UPWARD : FE_DOWNWARD;
            std::fesetround(rounding);
            std::feclearexcept(FE_ALL_EXCEPT);
            std::feraiseexcept(FE_INEXACT);
            const auto result = pool.Run(count, 47, [&](unsigned first, unsigned last) {
                Require(std::fegetround() == rounding, "owner rounding not propagated");
                Require((std::fetestexcept(FE_INEXACT) & FE_INEXACT) != 0,
                        "owner status not propagated");
                std::feraiseexcept(FE_DIVBYZERO);
                for (unsigned i = first; i < last; ++i) {
                    visits[i].fetch_add(1, std::memory_order_relaxed);
                    values[i] = std::uint64_t{i} * i + round;
                }
            });
            Require(result.owner_items + result.worker_items == count, "completed-item count");
            Require(std::fegetround() == rounding, "owner control changed");
            Require((std::fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0,
                    "worker exception flags lost");
            for (unsigned i = 0; i < count; ++i) {
                Require(visits[i] == 1, "duplicate or omitted item");
                Require(values[i] == std::uint64_t{i} * i + round, "output mismatch");
            }
            ++jobs;
        }
        // CodexAstraLocal: A callback failure must drain all outstanding borrows,
        // propagate to the owner, and leave the same pool reusable without stale work.
        std::atomic<unsigned> active{};
        bool caught = false;
        try {
            pool.Run(4096, 32, [&](unsigned first, unsigned) {
                if (first == 0)
                    throw std::runtime_error("expected worker failure");
                ++active;
                std::this_thread::yield();
                --active;
            });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        Require(caught && active == 0, "exception escaped before draining workers");
        auto after = pool.Run(5, 32, [](auto first, auto last) {
            Require(first == 0 && last == 5, "stale callback after failure");
        });
        Require(after.owner_items == 5 && after.worker_items == 0, "serial reuse after failure");
        jobs += 2;
    }
    original.Apply();
    std::cout << "parallel work PASS: " << jobs
              << " jobs; exact-once output, 1/2/6/8/12/17 participants, FP state, failure drain\n";
}

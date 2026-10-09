// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: A finite transport fixture uses the exact pool with
// two test-only call seams: synthetic syscall outcomes and a delayed final Wake.
// The normal path still executes real Linux/Android futex syscalls.
#include <atomic>
#include <barrier>
#include <cerrno>
#include <cstdlib>
#include <cfenv>
#include <chrono>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace FutexTest {
inline std::atomic<unsigned> mode{}, calls{}, retry_index{}, clock_reads{};
// CodexAstraLocal: Only the isolated counted-clock header calls this endpoint.
inline std::chrono::steady_clock::time_point ReadClock() {
    return std::chrono::steady_clock::time_point{std::chrono::nanoseconds{10 * ++clock_reads}};
}
inline std::atomic<bool> delay{};
inline std::binary_semaphore delayed{0}, resume{0};
long Syscall(long number, std::uint32_t* word, int operation, std::uint32_t value,
             const void* timeout, const void* second, unsigned mask) {
    ++calls;
    const auto selected = mode.load();
    // CodexAstraLocal: Error controls do not install seccomp or alter host policy.
    if (selected == 1) { errno = ENOSYS; return -1; }
    if (selected == 2) { errno = EINVAL; return -1; }
    if (selected == 3 && operation == FUTEX_WAIT_PRIVATE) {
        const auto index = retry_index.fetch_add(1);
        if (index < 3) {
            errno = index == 0 ? EINTR : EAGAIN;
            return -1;
        }
    }
    return ::syscall(number, word, operation, value, timeout, second, mask);
}
void BeforeFinalWake() {
    if (delay.exchange(false)) {
        delayed.release();
        resume.acquire();
    }
}
} // namespace FutexTest
#include "common/uberhar_parallel_work.h"

static std::atomic<unsigned> checks{};
static void Require(bool okay, const char* why) {
    ++checks;
    if (!okay) throw std::runtime_error(why);
}

// CodexAstraLocal: Real-clock bounds and FP parity cover zero, owner-only and
// joined work; the counted variant proves unmeasured calls read no clock.
static void Timing(bool counted) {
    using namespace Common::Uberhar;
    const auto original = ParallelFloatEnvironment::Capture();
    unsigned checks{};
    for (unsigned processors : {1U, 2U}) {
        ParallelWork pool{processors};
        for (unsigned count : {0U, 1U, 128U}) {
            ParallelFloatEnvironment::Status reference{};
            for (bool measured : {false, true}) {
                original.Apply();
                std::fesetround(FE_DOWNWARD);
                std::feclearexcept(FE_ALL_EXCEPT);
                std::feraiseexcept(FE_INEXACT);
                std::barrier meeting{processors == 2 && count == 128 ? 2 : 1};
                std::atomic<unsigned> visits{};
                FutexTest::clock_reads = 0;
                const auto begin = std::chrono::steady_clock::now();
                const auto result = pool.Run(count, 64, [&](unsigned first, unsigned last) {
                    Require(std::fegetround() == FE_DOWNWARD, "rounding changed by timing");
                    if (count == 128) meeting.arrive_and_wait();
                    std::feraiseexcept(FE_DIVBYZERO);
                    visits += last-first;
                }, measured);
                const auto end = std::chrono::steady_clock::now();
                if (counted) {
                    const bool multi = processors == 2 && count == 128;
                    const unsigned expected = measured ? (multi ? 3U : 2U) : 0U;
                    Require(FutexTest::clock_reads == expected &&
                            result.owner_process_ns == (measured ? 10U : 0U) &&
                            result.join_ns == (measured && multi ? 10U : 0U),
                            "clock branch contract failed");
                }
                const auto flags = ParallelFloatEnvironment::Flags();
                Require(visits == count && result.owner_items + result.worker_items == count,
                        "timing changed completed population");
                Require(std::fegetround() == FE_DOWNWARD, "timing changed owner control");
                if (!measured) {
                    Require(result.owner_process_ns == 0 && result.join_ns == 0,
                            "unmeasured timing fields not zero");
                    reference = flags;
                } else {
                    Require(flags == reference, "measured/unmeasured FP status differs");
                    const auto outer = std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();
                    Require(result.owner_process_ns + result.join_ns <= static_cast<std::uint64_t>(outer),
                            "timing escaped outer pool bracket");
                    if (processors == 1 || count < 128)
                        Require(result.join_ns == 0, "owner-only join invented");
                }
                ++checks;
            }
        }
    }
    original.Apply();
    std::cout << "timing controls PASS: " << checks << " measured/unmeasured cases\n";
}

static int Execute(int argc, char** argv) {
    using namespace Common::Uberhar;
    const std::string_view argument = argc > 1 ? argv[1] : "normal";
    if (argument == "timing" || argument == "counted-timing") {
        Timing(argument == "counted-timing");
        return 0;
    }
    if (argument == "permanent-wait" || argument == "permanent-wake") {
        // CodexAstraLocal: A permanent transport error has a terminal contract;
        // returning or retrying forever is a test failure, not a graceful drain.
        std::set_terminate(+[] { std::_Exit(77); });
        FutexTest::mode = 2;
        ParallelFutexWord word;
        if (argument == "permanent-wait") word.Wait(0); else word.Wake();
        return 78;
    }
    if (argument == "denied-probe") {
        FutexTest::mode = 1;
        {
            ParallelWork pool{2};
            Require(std::string_view{pool.WaitTransport()} == "condition_variable",
                    "denied probe transport identity");
            std::barrier meet{2};
            const auto result = pool.Run(2, 1, [&](auto, auto) { meet.arrive_and_wait(); });
            Require(result.owner_items == 1 && result.worker_items == 1,
                    "denied startup did not use complete CV fallback");
        }
        Require(FutexTest::calls == 2, "fallback continued to use denied futex adapter");
        std::cout << "denied probe PASS " << checks << '\n';
        return 0;
    }
    Require(ParallelFutexWord::Available(), "real futex unavailable in local fixture");
    { ParallelWork identity{1};
      Require(std::string_view{identity.WaitTransport()} == "futex", "actual futex transport identity"); }
    // CodexAstraLocal: Real mismatch must return immediately and preserve errno;
    // a stale notification cannot satisfy an unchanged user-space predicate.
    ParallelFutexWord word;
    errno = EDOM;
    word.Wait(1);
    Require(errno == EDOM, "wait changed caller errno");
    word.Wake();
    Require(errno == EDOM && word.Load() == 0, "wake changed predicate or errno");
    word.Store(0xfffffffeU);
    word.Publish();
    Require(word.Load() == 0xffffffffU, "generation before wrap");
    word.Publish();
    Require(word.Load() == 0, "generation wrap");
    // CodexAstraLocal: EINTR/EAGAIN controls execute the exact Wait and owner
    // predicate loop; after three synthetic returns the real syscall blocks
    // until the worker releases a valid result. No policy change is involved.
    {
        ParallelWork pool{2};
        std::barrier entered{2};
        const auto owner = std::this_thread::get_id();
        FutexTest::retry_index = 0;
        FutexTest::mode = 3;
        const auto result = pool.Run(2, 1, [&](auto, auto) {
            entered.arrive_and_wait();
            if (std::this_thread::get_id() != owner)
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
        });
        FutexTest::mode = 0;
        Require(result.owner_items == 1 && result.worker_items == 1,
                "interrupt/mismatch lost completion");
        Require(FutexTest::retry_index >= 3, "retry seam not exercised");
    }
    // CodexAstraLocal: Force the final worker to pause AFTER releasing count0
    // but BEFORE notifying. Owner observes completion, destroys the old borrow,
    // publishes the next job, then releases the delayed notification. The old
    // Wake must be harmless and the worker must acquire the new generation.
    {
        ParallelWork pool{2};
        const auto owner = std::this_thread::get_id();
        std::barrier entered{2};
        std::atomic<bool> old_live{true};
        FutexTest::delay = true;
        const auto first = pool.Run(2, 1, [&](auto, auto) {
            Require(old_live, "old callback after borrow ended");
            entered.arrive_and_wait();
            if (std::this_thread::get_id() == owner) FutexTest::delayed.acquire();
        });
        old_live = false;
        Require(first.owner_items == 1 && first.worker_items == 1,
                "delayed-notify first population");
        std::barrier next_entered{2};
        std::atomic<unsigned> new_visits{};
        const auto second = pool.Run(2, 1, [&](auto, auto) {
            ++new_visits;
            if (std::this_thread::get_id() == owner) FutexTest::resume.release();
            next_entered.arrive_and_wait();
        });
        Require(new_visits == 2 && second.owner_items == 1 && second.worker_items == 1,
                "delayed old notification corrupted new job");
    }
    // CodexAstraLocal: Repeated short-lived owners cover sleeping-stop and heap
    // address reuse; the underlying regression separately checks callback drain.
    for (unsigned i = 0; i < 16; ++i) {
        ParallelWork pool{2};
        std::barrier meet{2};
        const auto result = pool.Run(2, 1, [&](auto, auto) { meet.arrive_and_wait(); });
        Require(result.owner_items + result.worker_items == 2, "stop lifetime lost work");
    }
    std::cout << "transport controls PASS " << checks << '\n';
    return 0;
}

// CodexAstraLocal: Assertion failures are explicit finite child outcomes, never
// confused with successful mutation rejection by an unrelated crash/timeout.
int main(int argc, char** argv) {
    try { return Execute(argc, argv); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility> // CodexAstraLocal: Forward optional owner hooks without copies.
#include <vector>
#include "common/uberhar_cpu_work_budget.h"
#if defined(__linux__)
#include <sched.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace Common::Uberhar {

// CodexAstraLocal: Workers must execute with the owner's rounding/flush controls,
// not their last job's environment. ARM64 captures the complete FPCR/FPSR so
// sticky status outside the portable FE mask is retained too. The portable path
// uses the C floating environment and merges its standard exception flags.
struct ParallelFloatEnvironment {
    // CodexAstraLocal: Sticky flags can be merged after a joined job, but enabled
    // synchronous traps cannot preserve the serial fault site/order on workers.
    // Unknown control-register contracts remain serial rather than guessing.
    static bool AllowsParallel() {
#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
        std::uint64_t control;
        asm volatile("mrs %0, fpcr" : "=r"(control));
        return (control & 0x9f00U) == 0;
#elif defined(__i386__) || defined(__x86_64__)
        unsigned short control;
        asm volatile("fnstcw %0" : "=m"(control));
        return (control & 0x3fU) == 0x3fU && (_mm_getcsr() & 0x1f80U) == 0x1f80U;
#else
        // CodexAstraLocal: Other platforms need an executed full sticky-status
        // adapter before parallel admission, including MSVC's distinct x87 ABI.
        return false;
#endif
    }
#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    using Status = std::uint64_t;
    std::uint64_t control{}, status{};
    static ParallelFloatEnvironment Capture() {
        ParallelFloatEnvironment result;
        asm volatile("mrs %0, fpcr" : "=r"(result.control));
        asm volatile("mrs %0, fpsr" : "=r"(result.status));
        return result;
    }
    void Apply() const {
        asm volatile("msr fpcr, %0" : : "r"(control) : "memory");
        asm volatile("msr fpsr, %0" : : "r"(status) : "memory");
    }
    static Status Flags() {
        Status result;
        asm volatile("mrs %0, fpsr" : "=r"(result));
        return result;
    }
    static void Merge(Status extra) {
        const Status combined = Flags() | extra;
        asm volatile("msr fpsr, %0" : : "r"(combined) : "memory");
    }
#elif defined(__i386__) || defined(__x86_64__)
    // CodexAstraLocal: Standard FE_ALL_EXCEPT omits x86 denormal-operand status.
    // Carry all six MXCSR exception bits and x87 exception/stack-fault bits.
    // x87 TOP, condition codes and controls are not sticky flags to OR together.
    using Status = std::uint32_t;
    std::fenv_t environment{};
    std::uint32_t mxcsr{};
    static ParallelFloatEnvironment Capture() {
        ParallelFloatEnvironment result;
        std::fegetenv(&result.environment);
        result.mxcsr = _mm_getcsr();
        return result;
    }
    void Apply() const {
        std::fesetenv(&environment);
        _mm_setcsr(mxcsr);
    }
    static Status Flags() {
        unsigned short status;
        asm volatile("fnstsw %0" : "=am"(status));
        return (_mm_getcsr() & 0x3fU) | (static_cast<Status>(status & 0x7fU) << 16);
    }
    static void Merge(Status extra) {
        // CodexAstraLocal: The default 32-bit operand form has seven 32-bit
        // environment slots on both supported GNU x86 targets; status is slot1.
        // Restore the owner's entire image after inserting only sticky bits.
        // This avoids feraiseexcept arithmetic that can add unrelated flags.
        std::uint32_t x87[7];
        asm volatile("fnstenv %0" : "=m"(x87));
        x87[1] |= (extra >> 16) & 0x7fU;
        asm volatile("fldenv %0" : : "m"(x87));
        _mm_setcsr(_mm_getcsr() | (extra & 0x3fU));
    }
#else
    using Status = int;
    std::fenv_t environment{};
    static ParallelFloatEnvironment Capture() {
        ParallelFloatEnvironment result;
        std::fegetenv(&result.environment);
        return result;
    }
    void Apply() const { std::fesetenv(&environment); }
    static Status Flags() { return std::fetestexcept(FE_ALL_EXCEPT); }
    static void Merge(Status extra) {
        if (extra != 0)
            std::feraiseexcept(extra);
    }
#endif
};

// CodexAstraLocal: One synchronous owner lends immutable job data until all
// participating workers finish. Persistent sleeping workers avoid thread creation
// per draw; selective wakeups and grain stealing let heterogeneous CPUs share
// real work without busy polling. The owner also processes grains. Callbacks
// must touch only disjoint output ranges and immutable input, never renderer state.
// CodexAstraLocal: Linux/Android sleeping uses aligned 32-bit words with the
// compiler's lock-free atomic operations; no std::atomic object
// representation or unavailable NDK atomic_ref representation is assumed.
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
struct alignas(4) ParallelFutexWord {
    std::uint32_t value{};
    std::uint32_t Load() const { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }
    void Store(std::uint32_t next) { __atomic_store_n(&value, next, __ATOMIC_RELAXED); }
    void Publish() { __atomic_fetch_add(&value, 1U, __ATOMIC_RELEASE); }
    std::uint32_t Complete() { return __atomic_fetch_sub(&value, 1U, __ATOMIC_ACQ_REL); }

    // CodexAstraLocal: The syscall compares before sleeping, preventing a wake
    // between the user predicate and WAIT from being lost. Every return goes
    // through an acquire predicate reload in the caller, including EINTR,
    // EAGAIN and spurious wakes. There is no timed polling, yield or backoff.
    void Wait(std::uint32_t expected) {
        const int saved = errno;
        const long result = ::syscall(SYS_futex, &value, FUTEX_WAIT_PRIVATE, expected,
                                      nullptr, nullptr, 0);
        const int failure = result < 0 ? errno : 0;
        errno = saved;
        if (failure != 0 && failure != EINTR && failure != EAGAIN)
            std::terminate();
    }
    void Wake() {
        const int saved = errno;
        const long result = ::syscall(SYS_futex, &value, FUTEX_WAKE_PRIVATE, 1,
                                      nullptr, nullptr, 0);
        errno = saved;
        if (result < 0)
            std::terminate();
    }
    // CodexAstraLocal: A mismatch makes this startup probe nonblocking. A denied
    // or unsupported initial syscall selects the original CV transport before
    // any worker exists. This does not prove future per-thread syscall policy.
    static bool Available() {
        ParallelFutexWord probe;
        const int saved = errno;
        const long waited = ::syscall(SYS_futex, &probe.value, FUTEX_WAIT_PRIVATE, 1,
                                      nullptr, nullptr, 0);
        const int failure = errno;
        const long woke = ::syscall(SYS_futex, &probe.value, FUTEX_WAKE_PRIVATE, 1,
                                    nullptr, nullptr, 0);
        errno = saved;
        return waited == -1 && failure == EAGAIN && woke >= 0;
    }
};
static_assert(sizeof(ParallelFutexWord) == 4 && alignof(ParallelFutexWord) == 4);
static_assert(__atomic_always_lock_free(4, nullptr));
#endif

class ParallelWork {
public:
    struct Result {
        std::uint32_t owner_items{}, worker_items{};
        unsigned working_threads{};
        // CodexAstraLocal: Optional owner-side wall fields have exactly the
        // timing-only pool scope. Internal adaptive samples do not populate
        // these caller-requested fields; ordinary unsampled jobs read no clocks.
        std::uint64_t owner_process_ns{}, join_ns{};
        // CodexAstraLocal: Speculative CPU append is measured apart from the
        // subsequent join so overlap is never mislabeled as sleeping time.
        std::uint64_t owner_hook_ns{};
    };
    explicit ParallelWork(unsigned processors = AvailableProcessors(),
                          void (*worker_start)() = nullptr,
                          AuxiliaryBudget& shared_budget = AuxiliaryBudget::Global(),
                          AuxiliaryBudget::ProgressLane* progress = nullptr,
                          bool adaptive = true, bool diagnostics = true)
        : available{std::max(1U, processors)}, worker_limit{available - 1},
          on_worker_start{worker_start}, budget{shared_budget}, progress_lane{progress},
          // CodexAstraLocal: Owner-snapshotted controls never read Settings inside jobs.
          participation{available - 1, adaptive}, policy_diagnostics{diagnostics} {}
    ParallelWork(const ParallelWork&) = delete;
    ParallelWork& operator=(const ParallelWork&) = delete;

    ~ParallelWork() {
        // CodexAstraLocal: No Run may race destruction. Stop wakes each idle
        // generation and joins every thread before freeing either its word or
        // the completion word, including a delayed final-worker notification.
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
        if (use_futex) {
            futex_stopping.store(true, std::memory_order_release);
            for (auto& slot : workers) {
                slot->generation.Publish();
                slot->generation.Wake();
            }
        } else
#endif
        {
            {
                std::lock_guard lock{mutex};
                stopping = true;
            }
            for (auto& slot : workers)
                slot->wake.notify_one();
        }
        for (auto& slot : workers)
            slot->thread.join();
    }
    unsigned Available() const { return available; }
    unsigned CreatedWorkers() const { return static_cast<unsigned>(workers.size()); }
    unsigned StartupFailures() const { return startup_failures; }
    // CodexAstraLocal: Owner-only reporting exposes whether sampled policy
    // actually changed participation, without logging per draw or per worker.
    AdaptiveParticipation::Statistics Scheduling() const {
        auto result = participation.GetStatistics();
        result.prepare_samples = policy_prepare_samples; result.prepare_ns = policy_prepare_ns;
        result.observe_samples = policy_observe_samples; result.observe_ns = policy_observe_ns;
        return result;
    }
    // CodexAstraLocal: The owner identifies exact executable work generations;
    // zero opts mixed/unidentified jobs out of adaptation, not out of execution.
    void SetWorkIdentity(std::uint64_t identity) { work_identity = identity; }
    // CodexAstraLocal: Report the immutable runtime choice at the existing
    // cadence; a compiled Linux target can still select CV after a denied probe.
    const char* WaitTransport() const {
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
        if (use_futex) return "futex";
#endif
        return "condition_variable";
    }

    // CodexAstraLocal: The ordinary API compiles out every hook branch. Software
    // and other callers keep the same publication, owner-only and join contract.
    template <typename Function>
    Result Run(std::uint32_t count, std::uint32_t grain, Function&& function,
               bool measure = false) {
        return RunImpl<false>(count, grain, std::forward<Function>(function),
                              [] { return false; }, [] {}, measure);
    }

    // CodexAstraLocal: Prepare only after actual workers exist and before any
    // borrowed job is published. A false result disables speculative append.
    template <typename Function, typename Prepare, typename Hook>
    Result RunWithOwnerHook(std::uint32_t count, std::uint32_t grain, Function&& function,
                           Prepare&& prepare, Hook&& hook, bool measure = false) {
        return RunImpl<true>(count, grain, std::forward<Function>(function),
                             std::forward<Prepare>(prepare), std::forward<Hook>(hook), measure);
    }

private:
    // CodexAstraLocal TEST-ONLY LOG: Sparse observation cost excludes shader
    // execution/waits and is never added to overlapping pool wall time. Remove
    // these measurements after adaptive maintenance is hardware-qualified.
    void ObserveParticipation(const AdaptiveParticipation::Sample& sample) {
        const bool measure = policy_diagnostics && (++policy_observations % 64 == 0);
        const auto start = measure ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        participation.Observe(sample);
        if (measure) {
            ++policy_observe_samples;
            policy_observe_ns += Elapsed(start, std::chrono::steady_clock::now());
        }
    }
    template <bool HasHook, typename Function, typename Prepare, typename Hook>
    Result RunImpl(std::uint32_t count, std::uint32_t grain, Function&& function,
                   Prepare&& prepare, Hook&& hook, bool measure) {
        // CodexAstraLocal: A helper's nested job stays within its current token.
        // Owners never wait for capacity; denied auxiliaries leave ready grains
        // for the owner. Persistent slots remain asleep when admission shrinks.
        grain = std::max(1U, grain);
        const bool nested = AuxiliaryBudget::HasPermit();
        const unsigned helper_capacity = budget.HelperCapacity();
        // CodexAstraLocal TEST-ONLY LOG: Price controller preparation separately
        // from dispatched work, one in1024jobs; remove after .50 qualification.
        const bool price_policy = policy_diagnostics && (++policy_calls % 1024 == 0);
        const auto policy_start = price_policy ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point{};
        participation.Limit(std::min(available - 1, helper_capacity));
        participation.Prepare(count, grain, HasHook, work_identity);
        const unsigned requested = !nested && ParallelFloatEnvironment::AllowsParallel()
            ? std::min(available, std::max(1U, count / grain)) : 1U;
        // CodexAstraLocal: Sampled expiry must restore the static cap before
        // this job selects helpers, never halfway through an admitted trial.
        const bool sample = requested > 1 && participation.SelectSample(count, grain);
        const unsigned wanted = std::min({requested - 1, participation.Desired(), helper_capacity});
        if (price_policy) {
            ++policy_prepare_samples;
            policy_prepare_ns += Elapsed(policy_start, std::chrono::steady_clock::now());
        }
        const bool timed = measure || sample;
        EnsureWorkers(wanted);
        const unsigned active = std::min(wanted, CreatedWorkers());
        if (active == 0) {
            const auto start = timed ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
            // CodexAstraLocal: A reserved coordinator lane counts only actual
            // packet execution. Its RAII token unwinds before error delivery.
            try {
                auto permit = progress_lane && !nested ? progress_lane->Enter() : AuxiliaryBudget::Permit{};
                if (count != 0) function(0, count);
            } catch (...) {
                if (sample) ObserveParticipation({.failed = true});
                throw;
            }
            const auto elapsed = timed ? Elapsed(start, std::chrono::steady_clock::now()) : 0;
            if (sample) ObserveParticipation({count, grain, count, 0, 0, elapsed, elapsed, 0, 0,
                wanted != participation.Desired(), false});
            return {count, 0, count != 0 ? 1U : 0U, measure ? elapsed : 0, 0};
        }

        // CodexAstraLocal: Small jobs and failed worker startup returned above;
        // they take no transaction snapshot or readiness-publication overhead.
        bool prepared = false;
        if constexpr (HasHook) prepared = prepare();
        // CodexAstraLocal: Sample dispatch through completed work, excluding
        // thread creation/transaction preparation and all caller pacing. Normal
        // explicit timing retains its original three endpoints and public scope.
        const auto dispatch_start = sample ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};

        // CodexAstraLocal: Publication is unchanged except for its synchronizer:
        // release generations for futex workers or the original shared mutex
        // for portable workers. All payload writes precede the first wake.
        auto publish = [&] {
            task = const_cast<void*>(static_cast<const void*>(std::addressof(function)));
            invoke = [](void* opaque, std::uint32_t first, std::uint32_t last) {
                (*static_cast<std::remove_reference_t<Function>*>(opaque))(first, last);
            };
            item_count = count;
            grain_size = grain;
            next.store(0, std::memory_order_relaxed);
            failed.store(false, std::memory_order_relaxed);
            error = {};
            environment = ParallelFloatEnvironment::Capture();
        };
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
        if (use_futex) {
            publish();
            unfinished.Store(active);
            for (unsigned i = 0; i < active; ++i) {
                workers[i]->generation.Publish();
                workers[i]->generation.Wake();
            }
        } else
#endif
        {
            {
                std::lock_guard lock{mutex};
                publish();
                remaining = active;
                for (unsigned i = 0; i < active; ++i) {
                    workers[i]->items = 0;
                    workers[i]->flags = {};
                    workers[i]->ready = true;
                }
            }
            for (unsigned i = 0; i < active; ++i)
                workers[i]->wake.notify_one();
        }
        const auto owner_start = timed ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
        Result result{};
        {
            auto permit = progress_lane ? progress_lane->Enter() : AuxiliaryBudget::Permit{};
            result.owner_items = Process();
        }
        const auto owner_end = timed ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
        result.working_threads = result.owner_items != 0 ? 1U : 0U;
        std::exception_ptr hook_error;
        ParallelFloatEnvironment::Status hook_flags{};
        bool hook_ran{};
        if constexpr (HasHook) {
            // CodexAstraLocal: One finite attempt while borrowers remain. Ready
            // result bytes do not release worker, FP or resource ownership.
            const auto pending = [&] {
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
                if (use_futex) return unfinished.Load();
#endif
                std::lock_guard guard{mutex};
                return remaining;
            };
            if (prepared && pending() != 0 && !failed.load(std::memory_order_acquire)) {
                hook_ran = true;
                const auto owner_fp = ParallelFloatEnvironment::Capture();
                try { hook(); } catch (...) { hook_error = std::current_exception(); }
                hook_flags = ParallelFloatEnvironment::Flags();
                // CodexAstraLocal: A later shader error precedes an append that
                // must roll back, so do not merge its speculative flags yet.
                owner_fp.Apply();
            }
        }
        const auto join_start = timed && HasHook ? std::chrono::steady_clock::now() : owner_end;
        if constexpr (HasHook) {
            if (measure) result.owner_hook_ns = Elapsed(owner_end, join_start);
        }
        ParallelFloatEnvironment::Status extra{};
        std::exception_ptr exception;
        auto collect = [&] {
            const auto end = timed ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
            if (measure) {
                result.owner_process_ns = Elapsed(owner_start, owner_end);
                result.join_ns = Elapsed(join_start, end);
            }
            bool denied{};
            for (unsigned i = 0; i < active; ++i) {
                result.worker_items += workers[i]->items;
                result.working_threads += workers[i]->items != 0;
                extra |= workers[i]->flags;
                denied |= workers[i]->denied;
            }
            exception = error;
            task = nullptr;
            invoke = nullptr;
            // CodexAstraLocal: Never learn from failed or capacity-contended
            // work. Hook work is separately compared so a different amount of
            // useful prefix append cannot masquerade as a faster shader batch.
            if (sample) ObserveParticipation({count, grain, result.owner_items,
                result.worker_items, active, Elapsed(dispatch_start, end),
                Elapsed(owner_start, owner_end), Elapsed(join_start, end),
                hook_ran ? Elapsed(owner_end, join_start) : 0,
                denied || active != std::min(requested - 1, participation.Desired()),
                bool(exception) || bool(hook_error)});
        };
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
        if (use_futex) {
            for (auto pending = unfinished.Load(); pending != 0; pending = unfinished.Load())
                unfinished.Wait(pending);
            collect();
        } else
#endif
        {
            std::unique_lock lock{mutex};
            complete.wait(lock, [&] { return remaining == 0; });
            collect();
        }
        // CodexAstraLocal: Callback exceptions are recorded, all borrowers drain,
        // and only then does the owner merge sticky flags and rethrow. Fatal
        // transport errors are terminal; they never turn
        // into polling, worker disappearance, or an early borrowed-data return.
        ParallelFloatEnvironment::Merge(extra);
        if (exception)
            std::rethrow_exception(exception);
        // CodexAstraLocal: Shader failure wins after full drainage. Successful
        // shading retains append flags, including an append-only failure.
        if constexpr (HasHook) {
            if (prepared) ParallelFloatEnvironment::Merge(hook_flags);
            if (hook_error) std::rethrow_exception(hook_error);
        }
        return result;
    }

private:
    // CodexAstraLocal: Convert only selected steady-clock brackets; a
    // nonpositive duration reports zero without altering the work or FP state.
    static std::uint64_t Elapsed(std::chrono::steady_clock::time_point first,
                                 std::chrono::steady_clock::time_point last) {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(last - first).count();
        return ns > 0 ? static_cast<std::uint64_t>(ns) : 0;
    }
    struct Worker {
        std::condition_variable wake;
        std::thread thread;
        bool ready{};
        bool denied{};
        std::uint32_t items{};
        ParallelFloatEnvironment::Status flags{};
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
        ParallelFutexWord generation;
#endif
    };
    void EnsureWorkers(unsigned wanted) {
        // CodexAstraLocal: Preserve one-time allocation/thread failure fallback;
        // heap-owned slots and their futex addresses never move after publication.
        wanted = std::min(wanted, worker_limit);
        while (workers.size() < wanted) {
            try {
                auto slot = std::make_unique<Worker>();
                Worker* pointer = slot.get();
                workers.push_back(std::move(slot));
                try {
                    pointer->thread = std::thread([this, pointer] { WorkerLoop(*pointer); });
                } catch (...) {
                    workers.pop_back();
                    throw;
                }
            } catch (const std::system_error&) {
                worker_limit = CreatedWorkers();
                ++startup_failures;
                break;
            } catch (const std::bad_alloc&) {
                worker_limit = CreatedWorkers();
                ++startup_failures;
                break;
            }
        }
    }
    void WorkerLoop(Worker& slot) {
        // CodexAstraLocal: Thread naming is optional diagnostic work; a failure
        // must not terminate a worker and leave the owner waiting on its job.
        try {
            if (on_worker_start)
                on_worker_start();
        } catch (...) {
        }
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
        std::uint32_t observed = 0;
#endif
        for (;;) {
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
            if (use_futex) {
                // CodexAstraLocal: Acquire a new generation before reading the
                // borrowed payload; an unchanged generation blocks immediately.
                auto current = slot.generation.Load();
                while (current == observed) {
                    slot.generation.Wait(observed);
                    current = slot.generation.Load();
                }
                observed = current;
                if (futex_stopping.load(std::memory_order_acquire))
                    return;
            } else
#endif
            {
                std::unique_lock lock{mutex};
                slot.wake.wait(lock, [&] { return stopping || slot.ready; });
                if (stopping)
                    return;
                slot.ready = false;
            }
            std::uint32_t done{};
            ParallelFloatEnvironment::Status flags{};
            {
                // CodexAstraLocal: Acquire only after waking to executable work.
                // A denied helper retires its participation without claiming a
                // grain, and releases any token before completion/next sleep.
                const bool work_ready = next.load(std::memory_order_relaxed) < item_count &&
                                        !failed.load(std::memory_order_relaxed);
                auto permit = work_ready ? budget.TryAcquire() : AuxiliaryBudget::Permit{};
                slot.denied = work_ready && !permit;
                if (permit) {
                    const auto previous = ParallelFloatEnvironment::Capture();
                    environment.Apply();
                    done = Process();
                    flags = ParallelFloatEnvironment::Flags();
                    previous.Apply();
                }
            }
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
            if (use_futex) {
                slot.items = done;
                slot.flags = flags;
                // CodexAstraLocal: The acquire/release countdown joins every
                // worker's result and restored FP environment. After decrement
                // this worker touches no borrowed payload; a late Wake may
                // spuriously notify a newer job but cannot complete its count.
                if (unfinished.Complete() == 1)
                    unfinished.Wake();
            } else
#endif
            {
                std::lock_guard lock{mutex};
                slot.items = done;
                slot.flags = flags;
                if (--remaining == 0)
                    complete.notify_one();
            }
        }
    }
    std::uint32_t Process() {
        std::uint32_t processed = 0;
        try {
            while (!failed.load(std::memory_order_relaxed)) {
                // CodexAstraLocal: Widen the claim counter so count+one-grain
                // cannot wrap at UINT32_MAX and execute an old output range.
                const std::uint64_t first = next.fetch_add(grain_size, std::memory_order_relaxed);
                if (first >= item_count)
                    break;
                const auto last = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(item_count, first + grain_size));
                invoke(task, static_cast<std::uint32_t>(first), last);
                processed += last - static_cast<std::uint32_t>(first);
            }
        } catch (...) {
            failed.store(true, std::memory_order_relaxed);
            std::lock_guard lock{mutex};
            if (!error)
                error = std::current_exception();
        }
        return processed;
    }
    const unsigned available;
    unsigned worker_limit;
    void (*on_worker_start)();
    AuxiliaryBudget& budget;
    AuxiliaryBudget::ProgressLane* progress_lane;
    AdaptiveParticipation participation;
    // CodexAstraLocal: Immutable diagnostic policy, owner-only integer counters.
    const bool policy_diagnostics;
    std::uint64_t policy_calls{}, policy_observations{}, policy_prepare_samples{}, policy_prepare_ns{};
    std::uint64_t policy_observe_samples{}, policy_observe_ns{};
    // CodexAstraLocal: Fixed-purpose pools retain their own domain; shader pools
    // replace this identity with an exact retained proof generation per draw.
    std::uint64_t work_identity{1};
    unsigned startup_failures{};
    std::vector<std::unique_ptr<Worker>> workers;
    std::mutex mutex;
    std::condition_variable complete;
    bool stopping{};
    unsigned remaining{};
    std::atomic<std::uint64_t> next{};
    std::atomic<bool> failed{};
    std::uint32_t item_count{}, grain_size{};
    void* task{};
    void (*invoke)(void*, std::uint32_t, std::uint32_t){};
    ParallelFloatEnvironment environment{};
    std::exception_ptr error;
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && defined(SYS_futex)
    const bool use_futex{ParallelFutexWord::Available()};
    ParallelFutexWord unfinished;
    std::atomic<bool> futex_stopping{};
#endif
};
} // namespace Common::Uberhar

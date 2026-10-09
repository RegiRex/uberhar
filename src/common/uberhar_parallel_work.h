// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace Common::Uberhar {

// CodexAstraLocal: Size useful CPU work from the caller's allowed processors on
// Linux/Android, including cpuset restrictions. Other hosts use their standard
// concurrency query as fallback; a zero answer means owner-only execution.
inline unsigned AvailableProcessors() {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
        const int count = CPU_COUNT(&allowed);
        if (count > 0)
            return static_cast<unsigned>(count);
    }
#endif
    return std::max(1U, std::thread::hardware_concurrency());
}

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
class ParallelWork {
public:
    struct Result {
        std::uint32_t owner_items{}, worker_items{};
        unsigned working_threads{};
    };

    explicit ParallelWork(unsigned processors = AvailableProcessors(),
                          void (*worker_start)() = nullptr)
        : available{std::max(1U, processors)}, worker_limit{available - 1},
          on_worker_start{worker_start} {}
    ParallelWork(const ParallelWork&) = delete;
    ParallelWork& operator=(const ParallelWork&) = delete;

    ~ParallelWork() {
        // CodexAstraLocal: Run has already joined every borrowed job before it
        // returns. Wake idle workers for teardown before destroying any slots.
        {
            std::lock_guard lock{mutex};
            stopping = true;
        }
        for (auto& slot : workers)
            slot->wake.notify_one();
        for (auto& slot : workers)
            slot->thread.join();
    }

    unsigned Available() const { return available; }
    unsigned CreatedWorkers() const { return static_cast<unsigned>(workers.size()); }
    unsigned StartupFailures() const { return startup_failures; }

    template <typename Function>
    Result Run(std::uint32_t count, std::uint32_t grain, Function&& function) {
        // CodexAstraLocal: Small jobs remain on the owner. Worker count grows
        // only when enough independent items exist; no fixed six/eight-core cap.
        grain = std::max(1U, grain);
        const unsigned requested = ParallelFloatEnvironment::AllowsParallel()
                                       ? std::min(available, std::max(1U, count / grain))
                                       : 1U;
        EnsureWorkers(requested - 1);
        const unsigned active = std::min(requested - 1, CreatedWorkers());
        if (active == 0) {
            if (count != 0)
                function(0, count);
            return {count, 0, count != 0 ? 1U : 0U};
        }

        // CodexAstraLocal: Publish all job fields under the same mutex workers
        // acquire. A single atomic grain claim is the only shared hot-loop write.
        {
            std::lock_guard lock{mutex};
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
            remaining = active;
            for (unsigned i = 0; i < active; ++i) {
                workers[i]->items = 0;
                workers[i]->flags = {};
                workers[i]->ready = true;
            }
        }
        for (unsigned i = 0; i < active; ++i)
            workers[i]->wake.notify_one();
        Result result{Process(), 0, 0};
        result.working_threads = result.owner_items != 0 ? 1U : 0U;
        ParallelFloatEnvironment::Status extra{};
        std::exception_ptr exception;
        {
            std::unique_lock lock{mutex};
            complete.wait(lock, [&] { return remaining == 0; });
            for (unsigned i = 0; i < active; ++i) {
                result.worker_items += workers[i]->items;
                result.working_threads += workers[i]->items != 0;
                extra |= workers[i]->flags;
            }
            exception = error;
            task = nullptr;
            invoke = nullptr;
        }
        // CodexAstraLocal: Preserve sticky floating flags from completed worker
        // arithmetic on the owner too. Exceptions drain all jobs before escape,
        // so borrowed shader code, uniforms and buffers can then be released.
        ParallelFloatEnvironment::Merge(extra);
        if (exception)
            std::rethrow_exception(exception);
        return result;
    }

private:
    struct Worker {
        std::condition_variable wake;
        std::thread thread;
        bool ready{};
        std::uint32_t items{};
        ParallelFloatEnvironment::Status flags{};
    };

    void EnsureWorkers(unsigned wanted) {
        // CodexAstraLocal: Creation failure reduces available parallelism for
        // this owner; it never drops a job or repeatedly retries every draw.
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
        for (;;) {
            {
                std::unique_lock lock{mutex};
                slot.wake.wait(lock, [&] { return stopping || slot.ready; });
                if (stopping)
                    return;
                slot.ready = false;
            }
            const auto previous = ParallelFloatEnvironment::Capture();
            environment.Apply();
            const auto done = Process();
            const auto flags = ParallelFloatEnvironment::Flags();
            previous.Apply();
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
};
} // namespace Common::Uberhar

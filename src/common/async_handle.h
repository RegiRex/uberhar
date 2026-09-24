// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Completion publication and an event-driven wait for either compatible pipeline.

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace Common {

class AsyncHandle;

class AsyncCompletion {
public:
    // AstraEH: Both handles must use this completion signal and remain alive while waiting.
    // Prefer the specialized handle when both are ready; no polling or extra worker is used.
    bool WaitAny(AsyncHandle& preferred, AsyncHandle& alternative);

    void Notify() {
        // AstraEH: Serialize with predicate evaluation/sleep to prevent a lost wake-up.
        std::scoped_lock lock{mutex};
        condition.notify_all();
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
};

struct AsyncHandle {
public:
    explicit AsyncHandle(bool done = false, AsyncCompletion* completion_ = nullptr)
        : is_done{done}, completion{completion_} {}

    [[nodiscard]] bool IsDone() const noexcept {
        // AstraEH: A completed flag publishes the handle and its build diagnostics.
        return is_done.load(std::memory_order::acquire);
    }

    void WaitDone() noexcept {
        std::unique_lock lock{mutex};
        condition.wait(lock, [this] { return IsDone(); });
    }

    void MarkDone(bool done = true) noexcept {
        {
            std::scoped_lock lock{mutex};
            is_done.store(done, std::memory_order::release);
            condition.notify_all();
        }
        // AstraEH: Release the per-handle lock before taking the shared signal's lock.
        if (done && completion) {
            completion->Notify();
        }
    }

private:
    std::condition_variable condition;
    std::mutex mutex;
    std::atomic_bool is_done;
    AsyncCompletion* completion;
};

inline bool AsyncCompletion::WaitAny(AsyncHandle& preferred, AsyncHandle& alternative) {
    std::unique_lock lock{mutex};
    condition.wait(lock, [&] { return preferred.IsDone() || alternative.IsDone(); });
    return preferred.IsDone();
}

} // namespace Common

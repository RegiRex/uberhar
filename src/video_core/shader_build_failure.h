// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional> // CodexAstraLocal: Nonblocking opt-in submission observation.
#include <utility>
#include "video_core/shader_recovery_error.h"

namespace VideoCore {

// CodexAstraUlt-2: Every compiler outcome publishes completion, including a null
// module and exceptions. Failure diagnostics cannot escape this boundary either.
template <typename Shader, typename Build, typename Failure>
bool CompleteShaderBuild(Shader& shader, Build&& build, Failure&& failure) noexcept {
    const auto fail = [&](const char* reason) {
        shader.MarkFailed();
        try {
            failure(reason);
        } catch (...) {
            // CodexAstraUlt-2: Diagnostic allocation failure cannot strand a waiter.
        }
        return false;
    };
    try {
        std::forward<Build>(build)();
        if (shader.Handle()) {
            shader.MarkDone();
            return true;
        }
        return fail("null shader module");
    } catch (const std::exception& error) {
        return fail(error.what());
    } catch (...) {
        return fail("unknown compiler exception");
    }
}

template <typename Shader, typename Build>
bool CompleteShaderBuild(Shader& shader, Build&& build) noexcept {
    return CompleteShaderBuild(shader, std::forward<Build>(build), [](const char*) {});
}

// CodexAstraUlt-2: A failed command stream remains terminal. Wait only for ticks
// actually submitted, so cancellation cannot strand the producer on a GPU fence.
class ShaderFailureState {
public:
    void Fail() {
        std::scoped_lock lock{mutex};
        failed.store(true, std::memory_order_release);
        condition.notify_all();
    }

    bool Failed() const noexcept {
        return failed.load(std::memory_order_acquire);
    }

    void Check() const {
        if (Failed() && !shutdown)
            throw ShaderRecoveryError{};
    }

    void BeginShutdown() noexcept {
        shutdown = true;
    }

    void Submitted(std::uint64_t tick) {
        std::scoped_lock lock{mutex};
        submitted = tick;
        condition.notify_all();
    }

    // CodexAstraLocal: Observe only the existing post-SubmitWork publication.
    // Contention/error is unknown, never a wait or an invented submitted tick.
    std::optional<std::uint64_t> TrySubmittedTick() noexcept {
        try {
            std::unique_lock lock{mutex, std::try_to_lock};
            if (lock.owns_lock())
                return submitted;
        } catch (...) {
        }
        return std::nullopt;
    }

    bool WaitSubmitted(std::uint64_t tick) {
        std::unique_lock lock{mutex};
        condition.wait(lock, [&] { return submitted >= tick || Failed(); });
        Check();
        return submitted >= tick;
    }

private:
    std::atomic_bool failed{};
    bool shutdown{}; // CodexAstraUlt-2: Read and written only by the producer.
    std::uint64_t submitted{};
    std::mutex mutex;
    std::condition_variable condition;
};

// CodexAstraUlt-2: Catch only the renderer's terminal shader error on command
// workers. Discard the remainder; unrelated runtime errors are not recovered.
template <typename Execute, typename Discard>
void ExecuteShaderCommands(ShaderFailureState& state, Execute&& execute, Discard&& discard) {
    try {
        if (state.Failed())
            std::forward<Discard>(discard)();
        else
            std::forward<Execute>(execute)();
    } catch (const ShaderRecoveryError&) {
        state.Fail();
        std::forward<Discard>(discard)();
    }
}

} // namespace VideoCore

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraUlt-2: Fault-inject production publication and scheduler handoff.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include "common/async_handle.h"
#include "video_core/shader_build_failure.h"

namespace {
struct Shader : Common::AsyncHandle {
    int module{};
    std::atomic_bool failed{};
    int Handle() const { return module; }
    void MarkFailed() {
        failed.store(true, std::memory_order_release);
        MarkDone();
    }
};
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error{message};
}
template <typename Function>
bool ThrowsShaderError(Function&& function) {
    try {
        function();
        return false;
    } catch (const VideoCore::ShaderRecoveryError&) {
        return true;
    }
}
} // namespace

int main() {
    using namespace std::chrono_literals;
    for (int fault : {0, 1, 2, 3}) {
        Shader shader;
        auto dependency = std::async(std::launch::async, [&] {
            shader.WaitDone();
            return shader.failed.load(std::memory_order_acquire);
        });
        std::jthread compiler{[&] {
            const bool result = VideoCore::CompleteShaderBuild(shader, [&] {
                if (fault == 1)
                    throw std::runtime_error{"injected compiler error"};
                if (fault == 2)
                    throw 42;
                shader.module = fault == 0 ? 7 : 0;
            });
            Check(result == (fault == 0), "Incorrect build result");
        }};
        Check(dependency.wait_for(2s) == std::future_status::ready,
              "A shader failure stranded its pipeline dependency");
        Check(dependency.get() == (fault != 0), "Failure publication was lost");
        if (fault == 0)
            Check(shader.module == 7, "Module writes were not published");
    }

    Shader diagnostic_failure;
    std::string reason;
    Check(!VideoCore::CompleteShaderBuild(diagnostic_failure, [] {
        throw std::runtime_error{"preserved driver cause"};
    }, [&](const char* error) {
        reason = error;
        throw std::bad_alloc{};
    }), "A failed diagnostic callback escaped the compiler boundary");
    Check(diagnostic_failure.IsDone() && diagnostic_failure.failed &&
              reason == "preserved driver cause",
          "Failure cause or completion was lost when reporting failed");

    VideoCore::ShaderFailureState state;
    bool executed_after_failure = false;
    bool submitted_failed_stream = false;
    bool discarded = false;
    auto owner = std::make_shared<int>(17);
    std::weak_ptr<int> lifetime = owner;
    auto producer = std::async(std::launch::async, [&] {
        return ThrowsShaderError([&] { state.WaitSubmitted(2); });
    });
    Check(producer.wait_for(2ms) == std::future_status::timeout,
          "Unsubmitted work was advertised as complete");
    std::jthread worker{[&, capture = std::move(owner)]() mutable {
        state.Submitted(1);
        VideoCore::ExecuteShaderCommands(state, [&] {
            throw VideoCore::ShaderRecoveryError{};
            executed_after_failure = true;
        }, [&] {
            capture.reset();
            discarded = true;
        });
        VideoCore::ExecuteShaderCommands(state, [&] {
            submitted_failed_stream = true;
            state.Submitted(2);
        }, [] {});
    }};
    Check(producer.wait_for(2s) == std::future_status::ready,
          "Canceled GPU tick stranded the producer");
    Check(producer.get(), "Terminal error did not reach the producer");
    worker.join();
    Check(discarded && lifetime.expired(), "Canceled capture retained its resource");
    Check(!executed_after_failure && !submitted_failed_stream,
          "Commands executed after a terminal shader failure");
    Check(ThrowsShaderError([&] { state.Check(); }), "Failure was not permanent");
    state.BeginShutdown();
    state.Check();
    Check(state.WaitSubmitted(1), "Shutdown lost already-submitted GPU work");
    Check(!state.WaitSubmitted(2), "Shutdown waited for an unsignaled GPU tick");

    VideoCore::ShaderFailureState healthy;
    auto submission = std::async(std::launch::async, [&] { return healthy.WaitSubmitted(9); });
    healthy.Submitted(8);
    Check(submission.wait_for(2ms) == std::future_status::timeout,
          "An earlier submission satisfied a later tick");
    healthy.Submitted(9);
    Check(submission.get(), "Successful submission did not wake the producer");
    Check(!healthy.Failed(), "Successful submission marked failure");
    std::puts("PASS: shader null/error publication, command cancellation, producer handoff and drain");
}

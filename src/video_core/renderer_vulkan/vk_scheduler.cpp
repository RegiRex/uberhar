// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <mutex>
#include <utility>
#include "common/microprofile.h"
#include "common/thread.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#ifdef HAVE_LIBRETRO
#include "citra_libretro/libretro_vk.h"
#endif

MICROPROFILE_DEFINE(Vulkan_WaitForWorker, "Vulkan", "Wait for worker", MP_RGB(255, 192, 192));
MICROPROFILE_DEFINE(Vulkan_Submit, "Vulkan", "Submit Exectution", MP_RGB(255, 192, 255));

namespace Vulkan {

namespace {

std::unique_ptr<MasterSemaphore> MakeMasterSemaphore(const Instance& instance) {
#ifdef HAVE_LIBRETRO
    return CreateLibRetroMasterSemaphore(instance);
#else
    if (instance.IsTimelineSemaphoreSupported()) {
        return std::make_unique<MasterSemaphoreTimeline>(instance);
    } else {
        return std::make_unique<MasterSemaphoreFence>(instance);
    }
#endif
}

} // Anonymous namespace

void Scheduler::CommandChunk::ExecuteAll(vk::CommandBuffer cmdbuf) {
    while (first != nullptr) {
        auto* command = first;
        // CodexAstraUlt-2: Leave the throwing command linked for cancellation.
        command->Execute(cmdbuf);
        first = command->GetNext();
        command->~Command();
    }
    Discard();
}

void Scheduler::CommandChunk::Discard() noexcept {
    while (first != nullptr) {
        auto* command = first;
        first = command->GetNext();
        command->~Command();
    }
    submit = false;
    recorded_counts = 0;
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Instance& instance)
    : master_semaphore{MakeMasterSemaphore(instance)},
      command_pool{instance, master_semaphore.get()}, work_profile{instance}, use_worker_thread{true} {
    AllocateWorkerCommandBuffers();
    if (use_worker_thread) {
        AcquireNewChunk();
        worker_thread = std::jthread([this](std::stop_token token) { WorkerThread(token); });
    }
}

Scheduler::~Scheduler() = default;

void Scheduler::FinishWorkProfile() noexcept {
    try {
        std::scoped_lock lock{execution_mutex};
        work_profile.Poll(~u64{});
        work_profile.Report(true);
    } catch (...) { /* CodexAstraLocal: Optional final diagnostics cannot fail teardown. */ }
}

// CodexAstraLocal: Independent renderer packet tracking is retired; shared terminal state remains.
void Scheduler::Flush(vk::Semaphore signal, vk::Semaphore wait) {
    // When flushing, we only send data to the worker thread; no waiting is necessary.
    SubmitExecution(signal, wait);
}

void Scheduler::Finish(vk::Semaphore signal, vk::Semaphore wait) {
    if (shader_failure.Failed()) {
        WaitWorker();
        return;
    }
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitExecution(signal, wait);
    Wait(presubmit_tick);
}

void Scheduler::WaitWorker() {
    if (!use_worker_thread) {
        return;
    }

    MICROPROFILE_SCOPE(Vulkan_WaitForWorker);
    // CodexAstraLocal TEST-ONLY LOG: Measure actual owner recorder drainage,
    // separately from GPU execution. Remove with the .49 work profiler.
    const auto wait_start = work_profile.Enabled() ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
    // CodexAstraUlt-2: Drain captures even after failure; report only after the
    // worker no longer references them. Shutdown leaves the latch terminal.
    if (shader_failure.Failed())
        chunk->Discard();
    else
        DispatchWork();

    // Ensure the queue is drained.
    {
        std::unique_lock ql{queue_mutex};
        event_cv.wait(ql, [this] { return work_queue.empty(); });
    }

    // Now wait for execution to finish.
    // This needs to be done in the same order as WorkerThread.
    std::scoped_lock el{execution_mutex};
    if (work_profile.Enabled()) work_profile.Wait(HostWait::Recorder, std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - wait_start).count());
    shader_failure.Check();
}

void Scheduler::Wait(u64 tick) {
    // CodexAstraLocal: Retain submission cancellation and ordinary wait accounting.
    shader_failure.Check();
    if (master_semaphore->IsFree(tick)) {
        return;
    }
    const auto wait_start = work_profile.Enabled() ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
    if (tick >= master_semaphore->CurrentTick()) {
        // Make sure we are not waiting for the current tick without signalling
        Flush();
    }
    // CodexAstraUlt-2: A canceled submission never signals its Vulkan tick.
    const bool submitted = shader_failure.WaitSubmitted(tick);
    // CodexAstraLocal TEST-ONLY LOG: Split readiness from the existing device wait;
    // no extra submit/fence is introduced. Remove after dependency qualification.
    const auto ready_time = work_profile.Enabled() ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
    if (work_profile.Enabled()) work_profile.Wait(HostWait::SubmitReadiness,
        std::chrono::duration_cast<std::chrono::nanoseconds>(ready_time - wait_start).count());
    if (submitted) {
        // CodexAstraLocal TEST-ONLY LOG: Count only an actual device wait, excluding
        // canceled readiness and the diagnostic update above. This is host wall time.
        const auto fence_start = work_profile.Enabled() ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
        master_semaphore->Wait(tick);
        if (work_profile.Enabled()) work_profile.Wait(HostWait::GpuCompletion,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - fence_start).count());
    }
}

void Scheduler::DispatchWork() {
    // CodexAstraUlt-2: During shutdown, discard instead of submitting a partial draw.
    if (shader_failure.Failed()) {
        chunk->Discard();
        return;
    }
    if (!use_worker_thread || chunk->Empty()) {
        return;
    }

    on_dispatch();

    {
        std::scoped_lock ql{queue_mutex};
        work_queue.push(std::move(chunk));
    }

    event_cv.notify_all();
    AcquireNewChunk();
}

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("VulkanWorker");

    const auto TryPopQueue{[this](auto& work) -> bool {
        if (work_queue.empty()) {
            return false;
        }

        work = std::move(work_queue.front());
        work_queue.pop();
        event_cv.notify_all();
        return true;
    }};

    while (!stop_token.stop_requested()) {
        std::unique_ptr<CommandChunk> work;

        {
            std::unique_lock lk{queue_mutex};

            // Wait for work.
            Common::CondvarWait(event_cv, lk, stop_token, [&] { return TryPopQueue(work); });

            // If we've been asked to stop, we're done.
            if (stop_token.stop_requested()) {
                return;
            }

            // Exchange lock ownership so that we take the execution lock before
            // the queue lock goes out of scope. This allows us to force execution
            // to complete in the next step.
            std::exchange(lk, std::unique_lock{execution_mutex});

            // Perform the work, tracking whether the chunk was a submission
            // before executing.
            const bool has_submit = work->HasSubmit();
            // CodexAstraUlt-2: Stop the entire stream; the producer reports this
            // typed failure. No draw or submission after it may execute.
            VideoCore::ExecuteShaderCommands(shader_failure,
                [&] {
                    work->ExecuteAll(current_cmdbuf);
                    // CodexAstraLocal: The next command-buffer allocation is
                    // still worker-owned work. Capture its typed failure before
                    // it can escape the thread; already submitted ticks remain
                    // drainable and subsequent chunks are canceled by the latch.
                    if (has_submit) {
                        AllocateWorkerCommandBuffers();
                    }
                }, [&] { work->Discard(); });
        }

        {
            std::scoped_lock rl{reserve_mutex};

            // Recycle the chunk back to the reserve.
            chunk_reserve.emplace_back(std::move(work));
        }
    }
}

void Scheduler::AllocateWorkerCommandBuffers() {
    const vk::CommandBufferBeginInfo begin_info = {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };

    current_cmdbuf = command_pool.Commit();
    current_cmdbuf.begin(begin_info);
}

void Scheduler::SubmitExecution(vk::Semaphore signal_semaphore, vk::Semaphore wait_semaphore) {
    if (shader_failure.Failed()) {
        shader_failure.Check();
        return;
    }
    state = StateFlags::AllDirty;
    const u64 signal_value = master_semaphore->NextTick();

    on_submit();

    // CodexAstraLocal: Submit supported work without a retired per-draw packet observer.
    Record([signal_semaphore, wait_semaphore, signal_value, this](vk::CommandBuffer cmdbuf) {
        MICROPROFILE_SCOPE(Vulkan_Submit);
        const auto submit_start = work_profile.Enabled() ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
        {
        std::scoped_lock lock{submit_mutex};
        master_semaphore->SubmitWork(cmdbuf, wait_semaphore, signal_semaphore, signal_value);
        shader_failure.Submitted(signal_value);
        }
        if (work_profile.Enabled()) work_profile.Wait(HostWait::Submit, std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - submit_start).count());
        // CodexAstraLocal: Queries are read only after their original successful
        // tick is complete, and availability still guards driver readiness.
        work_profile.Submitted(signal_value, master_semaphore->KnownGpuTick());
    });

    master_semaphore->Refresh();

    if (!use_worker_thread) {
        AllocateWorkerCommandBuffers();
    } else {
        chunk->MarkSubmit();
        DispatchWork();
    }
}

void Scheduler::AcquireNewChunk() {
    std::scoped_lock lock{reserve_mutex};
    if (chunk_reserve.empty()) {
        chunk = std::make_unique<CommandChunk>();
        return;
    }

    chunk = std::move(chunk_reserve.back());
    chunk_reserve.pop_back();
}

} // namespace Vulkan

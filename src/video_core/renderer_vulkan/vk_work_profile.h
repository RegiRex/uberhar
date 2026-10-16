// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;

// CodexAstraLocal: Categories describe complete command spans, never exclusive
// hardware stages. Overlapping GPU durations must not be summed as frame time.
enum class GpuWork : u32 {
    Graphics, Utility, Upload, Copy, Blit, Readback, Feedback, Presentation, Count
};
enum class HostWait : u32 { Recorder, GpuCompletion, Submit, Count };

// CodexAstraLocal TEST-ONLY LOG: Bounded .49 cost attribution. Remove this
// profiler after Native/Combo work placement is hardware-qualified. All query
// state is command-worker-owned; only rare owner wait totals cross threads.
class WorkProfile {
public:
    explicit WorkProfile(const Instance& instance);
    ~WorkProfile();
    int Begin(vk::CommandBuffer command, GpuWork kind, u64 logical_bytes = 0);
    void End(vk::CommandBuffer command, int slot);
    void BeginPass(vk::CommandBuffer command, GpuWork kind);
    void EndPass(vk::CommandBuffer command);
    void Route(u32 route);
    void Submitted(u64 tick, u64 completed_tick) noexcept;
    void Poll(u64 completed_tick) noexcept;
    void Wait(HostWait reason, u64 nanoseconds);
    void Report(bool final = false) noexcept;

private:
    struct Sample {
        u64 tick{}, serial{}, mask{}, host_start{};
        GpuWork kind{};
        // 0=free, 1=recording, 2=closed/unsubmitted, 3=submitted.
        u32 state{};
    };
    struct Totals {
        u64 calls{}, bytes{}, samples{}, ns{}, maximum_ns{}, dropped{}, mixed{}, host_span_ns{};
        u64 first_tick{}, last_tick{}, first_serial{}, last_serial{};
    };
    const Instance& instance;
    vk::UniqueQueryPool pool;
    std::array<Sample, 128> samples{};
    std::array<Totals, static_cast<u32>(GpuWork::Count)> totals{};
    std::array<u64, 5> routes{};
    std::array<std::atomic<u64>, static_cast<u32>(HostWait::Count)> waits{};
    std::array<std::atomic<u64>, static_cast<u32>(HostWait::Count)> wait_calls{};
    std::chrono::steady_clock::time_point report_time{std::chrono::steady_clock::now()};
    u64 timestamp_mask{}, serial{}, query_errors{}, unavailable{}, profile_cpu_ns{};
    double period{};
    int pass_slot{-1};
    bool enabled{}, final_reported{}, pass_active{};
};
} // namespace Vulkan

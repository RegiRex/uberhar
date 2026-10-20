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
    // CodexAstraLocal: Retain normal category indices after retiring the
    // independent Calculated renderer and its unused trailing categories.
    Graphics, Utility, Upload, Copy, Blit, Readback, Feedback, Presentation, Count
};
// CodexAstraLocal: Submission readiness and actual fence waiting are different dependencies.
enum class HostWait : u32 { Recorder, GpuCompletion, Submit, SubmitReadiness, Count };

// CodexAstraLocal TEST-ONLY LOG: Semantic hashes contain no host handles or
// pointers. They correlate sampled work only and never admit shader execution.
// Remove after costly-state attribution is qualified; uniforms/samplers remain
// outside this state identity and scissor area is only a workload proxy.
struct GpuDrawIdentity {
    u64 vertex{}, fragment{}, geometry{}, state{}, transport{};
    bool operator==(const GpuDrawIdentity&) const = default;
};

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
    // CodexAstraLocal: Hash draw state only inside an already sampled graphics
    // pass. Disabled, count-only and unsampled paths do no identity preparation.
    bool NeedsIdentity() const noexcept {
        return pass_slot >= 0 && samples[pass_slot].kind == GpuWork::Graphics;
    }
    void Identity(const GpuDrawIdentity& identity, u64 scissor_area);
    void Submitted(u64 tick, u64 completed_tick) noexcept;
    void Poll(u64 completed_tick) noexcept;
    // CodexAstraLocal: Callers avoid clock reads entirely when diagnostic collection is off.
    bool Enabled() const noexcept { return enabled; }
    void Wait(HostWait reason, u64 nanoseconds);
    void Report(bool final = false) noexcept;

private:
    struct Sample {
        u64 tick{}, serial{}, mask{}, host_start{};
        GpuWork kind{};
        // 0=free, 1=recording, 2=closed/unsubmitted, 3=submitted.
        u32 state{};
        // CodexAstraLocal TEST-ONLY LOG: Fixed per-query metadata follows its
        // original submission tick, independent of later draw preparation.
        GpuDrawIdentity first{}, last{};
        u64 draw_bindings{}, scissor_area_sum{};
        // CodexAstraLocal TEST-ONLY LOG: Retain the existing end clock beside
        // host_start so repeated slowest samples can be aligned to scene windows.
        u64 host_end{};
        bool mixed_identity{};
    };
    // CodexAstraLocal TEST-ONLY LOG: Preserve only sixteen slowest resolved
    // graphics samples. No per-sample log, allocation, extra query or pass split.
    // Remove after identity/cost qualification; full totals remain independent.
    struct Detail { Sample sample{}; u64 ns{}; };
    struct Totals {
        u64 calls{}, bytes{}, samples{}, ns{}, maximum_ns{}, dropped{}, mixed{}, host_span_ns{};
        u64 first_tick{}, last_tick{}, first_serial{}, last_serial{};
        // CodexAstraLocal TEST-ONLY LOG: Coverage distinguishes missing bind
        // identity from a single-state pass; no absent metadata becomes a zero
        // shader cost. Remove with the bounded identity ledger after qualification.
        u64 identified_samples{}, mixed_identity_samples{}, draw_bindings{}, scissor_area_sum{};
    };
    const Instance& instance;
    vk::UniqueQueryPool pool;
    std::array<Sample, 128> samples{};
    std::array<Detail, 16> slow_graphics{};
    u32 slow_graphics_count{};
    std::array<Totals, static_cast<u32>(GpuWork::Count)> totals{};
    std::array<u64, 5> routes{};
    std::array<std::atomic<u64>, static_cast<u32>(HostWait::Count)> waits{};
    std::array<std::atomic<u64>, static_cast<u32>(HostWait::Count)> wait_calls{};
    std::chrono::steady_clock::time_point report_time{std::chrono::steady_clock::now()};
    u64 timestamp_mask{}, serial{}, query_errors{}, unavailable{}, profile_cpu_ns{};
    double period{};
    int pass_slot{-1};
    bool enabled{}, timestamps_requested{}, final_reported{}, pass_active{};
    u32 report_prescale{};
    u64 report_sequence{};
};
} // namespace Vulkan

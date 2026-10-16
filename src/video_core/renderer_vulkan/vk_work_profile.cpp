// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <bit>
#include "common/logging/log.h"
#include "common/uberhar_test_profile.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_work_profile.h"

namespace Vulkan {
namespace {
constexpr std::array names{"graphics", "utility", "upload", "copy", "blit", "readback",
                           "feedback", "presentation"};
constexpr std::array wait_names{"cpu_recorder", "gpu_completion", "queue_submit"};
}

WorkProfile::WorkProfile(const Instance& instance_) : instance{instance_} {
    try {
    const auto mode = Settings::values.uberhar_test_mode.GetValue();
    enabled = mode == Settings::UberharTestMode::Native || Settings::UsesReadyGpuVertices(mode);
    if (!enabled) return;
    const auto physical = instance.GetPhysicalDevice();
    const auto family = physical.getQueueFamilyProperties()[instance.GetGraphicsQueueFamilyIndex()];
    const auto limits = physical.getProperties().limits;
    if (family.timestampValidBits && limits.timestampComputeAndGraphics && limits.timestampPeriod > 0) {
        period = limits.timestampPeriod;
        timestamp_mask = family.timestampValidBits >= 64 ? ~u64{} :
                         (u64{1} << family.timestampValidBits) - 1;
        try {
            pool = instance.GetDevice().createQueryPoolUnique(
                {.queryType = vk::QueryType::eTimestamp, .queryCount = 256});
        } catch (const std::exception& error) {
            ++query_errors;
            LOG_WARNING(Render_Vulkan, "Uberhar GPU cost queries unavailable: {}", error.what());
        }
    }
    LOG_INFO(Render_Vulkan, "Uberhar GPU cost profile: enabled=true timestamps={} mode={} "
             "sample_stride=64 slots=128 waits=false pass_splits=false logical_bytes_not_bandwidth=true",
             bool(pool), static_cast<u32>(mode));
    } catch (...) { ++query_errors; } // CodexAstraLocal: Optional diagnostics cannot prevent startup.
}

WorkProfile::~WorkProfile() { Report(true); }

int WorkProfile::Begin(vk::CommandBuffer command, GpuWork kind, u64 logical_bytes) {
    if (!enabled) return -1;
    auto& stat = totals[static_cast<u32>(kind)];
    ++stat.calls;
    stat.bytes += logical_bytes;
    // CodexAstraLocal: Sparse per-category sampling avoids a timestamp/barrier on
    // every small operation. No unavailable slot can delay command recording.
    if (pass_active) { ++stat.dropped; return -1; } // CodexAstraLocal: Never reset queries inside any active pass.
    if (!pool || (stat.calls & 63) != 1) return -1;
    for (u32 i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        if (sample.state) continue;
        sample = {0, ++serial, 0, static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()), kind, 1};
        command.resetQueryPool(*pool, i * 2, 2);
        command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *pool, i * 2);
        return static_cast<int>(i);
    }
    ++stat.dropped;
    return -1;
}

void WorkProfile::End(vk::CommandBuffer command, int slot) {
    if (slot < 0) return;
    command.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *pool, slot * 2 + 1);
    auto& sample = samples[slot];
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    totals[static_cast<u32>(sample.kind)].host_span_ns += static_cast<u64>(now) - sample.host_start;
    sample.state = 2;
}

void WorkProfile::BeginPass(vk::CommandBuffer command, GpuWork kind) {
    pass_slot = Begin(command, kind);
    pass_active = true;
}
void WorkProfile::EndPass(vk::CommandBuffer command) {
    End(command, pass_slot);
    pass_slot = -1;
    pass_active = false;
}
void WorkProfile::Route(u32 route) {
    if (!enabled || route >= routes.size()) return;
    ++routes[route];
    if (pass_slot >= 0) samples[pass_slot].mask |= u64{1} << route;
}

void WorkProfile::Submitted(u64 tick, u64 completed_tick) noexcept {
    if (!enabled) return;
    // CodexAstraLocal: Stamp only completed measurement pairs after successful submit.
    for (auto& sample : samples)
        if (sample.state == 2) { sample.tick = tick; sample.state = 3; }
    Poll(completed_tick);
}

void WorkProfile::Poll(u64 completed_tick) noexcept {
    try {
    if (!enabled) return;
    const auto start = std::chrono::steady_clock::now();
    for (u32 i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        if (sample.state != 3 || sample.tick > completed_tick || !pool) continue;
        std::array<u64, 4> values{};
        try {
            const auto result = instance.GetDevice().getQueryPoolResults(
                *pool, i * 2, 2, sizeof(values), values.data(), 2 * sizeof(u64),
                vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
            if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
                ++query_errors;
                sample.state = 0;
                continue;
            }
            if (result == vk::Result::eNotReady || !values[1] || !values[3]) {
                ++unavailable;
                continue;
            }
            auto& stat = totals[static_cast<u32>(sample.kind)];
            const auto ns = static_cast<u64>(((values[2] - values[0]) & timestamp_mask) * period);
            if (!stat.samples) { stat.first_tick = sample.tick; stat.first_serial = sample.serial; }
            ++stat.samples;
            stat.ns += ns;
            stat.maximum_ns = std::max(stat.maximum_ns, ns);
            stat.last_tick = sample.tick;
            stat.last_serial = sample.serial;
            stat.mixed += std::popcount(sample.mask) > 1;
            sample.state = 0;
        } catch (const std::exception& error) {
            ++query_errors;
            sample.state = 0;
            if (query_errors <= 3)
                LOG_INFO_DIAGNOSTIC(Render_Vulkan, "Uberhar GPU cost query failed: {}", error.what());
        }
    }
    profile_cpu_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    Report();
    } catch (...) { ++query_errors; } // CodexAstraLocal: Logging/poll failure cannot fail rendering.
}

void WorkProfile::Wait(HostWait reason, u64 nanoseconds) {
    if (!enabled) return;
    const auto index = static_cast<u32>(reason);
    waits[index].fetch_add(nanoseconds, std::memory_order_relaxed);
    wait_calls[index].fetch_add(1, std::memory_order_relaxed);
}

void WorkProfile::Report(bool final) noexcept {
    try {
    if (!enabled || (final && final_reported)) return;
    const auto now = std::chrono::steady_clock::now();
    if (!final && now - report_time < std::chrono::seconds(10)) return;
    report_time = now;
    final_reported |= final;
    for (u32 i = 0; i < totals.size(); ++i) {
        const auto& s = totals[i];
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic, "Uberhar GPU cost {}: final={} calls={} logical_bytes={} samples={} "
                 "sample_ms={:.6f} max_sample_ms={:.6f} dropped={} mixed={} tick_first={} tick_last={} "
                 "sample_first={} sample_last={} host_record_span_ms={:.6f} total_is_cumulative=true unsampled_cost=unknown",
                 names[i], final, s.calls, s.bytes, s.samples, s.ns / 1e6, s.maximum_ns / 1e6,
                 s.dropped, s.mixed, s.first_tick, s.last_tick, s.first_serial, s.last_serial, s.host_span_ns / 1e6);
    }
    for (u32 i = 0; i < waits.size(); ++i)
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic, "Uberhar host cost {}: final={} calls={} wall_ms={:.6f}",
                 wait_names[i], final, wait_calls[i].load(std::memory_order_relaxed),
                 waits[i].load(std::memory_order_relaxed) / 1e6);
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic, "Uberhar GPU cost coverage: final={} generic={} static={} full={} "
             "recovery={} gpu_optimized={} query_errors={} unavailable={} poll_cpu_ms={:.6f} pending={}",
             final, routes[0], routes[1], routes[2], routes[3], routes[4], query_errors,
             unavailable, profile_cpu_ns / 1e6,
             std::count_if(samples.begin(), samples.end(), [](const auto& sample) { return sample.state != 0; }));
    } catch (...) { ++query_errors; }
}
} // namespace Vulkan

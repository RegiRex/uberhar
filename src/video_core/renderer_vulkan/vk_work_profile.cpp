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
// CodexAstraLocal: Only the remaining renderer's categories can have producers.
constexpr std::array names{"graphics", "utility", "upload", "copy", "blit", "readback",
                           "feedback", "presentation"};
// CodexAstraLocal: Every actual stage must have exactly one retained category name.
static_assert(names.size()==static_cast<u32>(GpuWork::Count));
constexpr std::array wait_names{"cpu_recorder", "gpu_completion", "queue_submit", "submit_readiness"};
}

WorkProfile::WorkProfile(const Instance& instance_) : instance{instance_} {
    try {
    const auto mode = Settings::values.uberhar_test_mode.GetValue();
    // CodexAstraLocal: Keep operation counts independent of GPU timestamp commands.
    // The owner can measure observer cost without disabling actual rendering work.
    enabled = Settings::values.uberhar_feature_diagnostics.GetValue() &&
              // CodexAstraLocal: The retired independent Compute mode has no
              // timestamp producer; supported Native/Combo controls are retained.
              (mode == Settings::UberharTestMode::Native || Settings::UsesReadyGpuVertices(mode));
    timestamps_requested = Settings::values.uberhar_gpu_timestamps.GetValue();
    if (!enabled) return;
    const auto physical = instance.GetPhysicalDevice();
    const auto family = physical.getQueueFamilyProperties()[instance.GetGraphicsQueueFamilyIndex()];
    const auto limits = physical.getProperties().limits;
    if (timestamps_requested && family.timestampValidBits && limits.timestampComputeAndGraphics && limits.timestampPeriod > 0) {
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
    LOG_INFO(Render_Vulkan, "Uberhar GPU cost profile: feature=gpu_work.v2 enabled=true timestamps={} mode={} "
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
    // CodexAstraLocal TEST-ONLY LOG: Reuse the already measured endpoint;
    // scene attribution adds no per-draw or per-span clock read.
    sample.host_end = static_cast<u64>(now);
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

// CodexAstraLocal TEST-ONLY LOG: Called on the selecting worker after any late
// fallback decision, using immutable selected shader IDs and current draw state.
// First/last equality alone cannot hide a different shader in the middle.
// Remove after attribution qualification; no clocks occur on this path.
void WorkProfile::Identity(const GpuDrawIdentity& identity, u64 scissor_area) {
    if (!NeedsIdentity()) return;
    auto& sample = samples[pass_slot];
    if (!sample.draw_bindings) sample.first = identity;
    else sample.mixed_identity |= !(sample.first == identity);
    sample.last = identity;
    ++sample.draw_bindings;
    sample.scissor_area_sum += scissor_area;
}

void WorkProfile::Submitted(u64 tick, u64 completed_tick) noexcept {
    if (!enabled) return;
    // CodexAstraLocal: Stamp only completed measurement pairs after successful submit.
    if (pool) for (auto& sample : samples)
        if (sample.state == 2) { sample.tick = tick; sample.state = 3; }
    Poll(completed_tick);
}

void WorkProfile::Poll(u64 completed_tick) noexcept {
    try {
    if (!enabled) return;
    // CodexAstraLocal: Counts-only runs do not scan query slots or time polling.
    if (!pool) { Report(); return; }
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
            // CodexAstraLocal TEST-ONLY LOG: Keep complete metadata-coverage
            // totals independently of which sixteen slow spans remain retained.
            stat.identified_samples += sample.draw_bindings != 0;
            stat.mixed_identity_samples += sample.mixed_identity;
            stat.draw_bindings += sample.draw_bindings;
            stat.scissor_area_sum += sample.scissor_area_sum;
            // CodexAstraLocal TEST-ONLY LOG: A fixed-size slowest-span ledger
            // retains actual selected identities through optional progress loss.
            // Whole mixed-pass durations never become a single shader's cost.
            if (sample.kind == GpuWork::Graphics && sample.draw_bindings) {
                auto* target = &slow_graphics[0];
                if (slow_graphics_count < slow_graphics.size()) {
                    target = &slow_graphics[slow_graphics_count++];
                } else {
                    target = &*std::min_element(slow_graphics.begin(), slow_graphics.end(),
                        [](const auto& a, const auto& b) { return a.ns < b.ns; });
                }
                if (!target->sample.draw_bindings || ns > target->ns)
                    *target = Detail{sample, ns};
            }
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
    // CodexAstraLocal TEST-ONLY LOG: Periodic cumulative evidence survives dropped
    // records; prescale avoids a reporting clock read on every submission.
    if (!final && (++report_prescale & 63U) != 0) return;
    const auto now = std::chrono::steady_clock::now();
    if (!final && now - report_time < std::chrono::seconds(10)) return;
    report_time = now;
    final_reported |= final;
    ++report_sequence;
    for (u32 i = 0; i < totals.size(); ++i) {
        const auto& s = totals[i];
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic, "Uberhar GPU cost {}: feature=gpu_work.v2 sequence={} final={} calls={} logical_bytes={} samples={} "
                 "sample_ms={:.6f} max_sample_ms={:.6f} dropped={} mixed={} tick_first={} tick_last={} "
                 "sample_first={} sample_last={} host_record_span_ms={:.6f} "
                 "identified_samples={} mixed_identity_samples={} sampled_draw_bindings={} sampled_scissor_area_sum={} "
                 "total_is_cumulative=true unsampled_cost=unknown",
                 names[i], report_sequence, final, s.calls, s.bytes, s.samples, s.ns / 1e6, s.maximum_ns / 1e6,
                 s.dropped, s.mixed, s.first_tick, s.last_tick, s.first_serial, s.last_serial, s.host_span_ns / 1e6,
                 s.identified_samples, s.mixed_identity_samples, s.draw_bindings, s.scissor_area_sum);
    }
    for (u32 i = 0; i < waits.size(); ++i)
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic, "Uberhar host cost {}: feature=host_wait.v2 sequence={} final={} calls={} wall_ms={:.6f}",
                 wait_names[i], report_sequence, final, wait_calls[i].load(std::memory_order_relaxed),
                 waits[i].load(std::memory_order_relaxed) / 1e6);
    // CodexAstraLocal TEST-ONLY LOG: Repeat the bounded ledger at the existing
    // cadence and reliable final. Sequence+sample serial allow deduplication;
    // unknown uniforms/samplers prevent treating identity as complete draw input.
    // Remove after graphics-cost attribution is sufficient for optimization.
    for (u32 i = 0; i < slow_graphics_count; ++i) {
        const auto& detail = slow_graphics[i];
        const auto& s = detail.sample;
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic,
            "Uberhar GPU graphics detail: feature=gpu_identity.v1 sequence={} final={} slot={} limit=16 "
            "sample={} tick={} sample_ns={} route_mask={} draw_bindings={} scissor_area_sum={} "
            "host_start_ns={} host_end_ns={} report_host_ns={} host_clock=steady_clock "
            "mixed_identity={} first_vs={:016X} first_fs={:016X} first_gs={:016X} first_state={:016X} first_transport={:016X} "
            "last_vs={:016X} last_fs={:016X} last_gs={:016X} last_state={:016X} last_transport={:016X} "
            "selection=actual_worker identity_scope=source_profile semantic_hashes=true binary_hashes=false uniforms_samplers=untracked "
            "area=per_binding_scissor_proxy coverage=unknown retained=slowest_sampled_spans",
            report_sequence, final, i, s.serial, s.tick, detail.ns, s.mask, s.draw_bindings,
            s.scissor_area_sum, s.host_start, s.host_end,
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),
            s.mixed_identity, s.first.vertex, s.first.fragment, s.first.geometry,
            s.first.state, s.first.transport, s.last.vertex, s.last.fragment, s.last.geometry,
            s.last.state, s.last.transport);
    }
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic, "Uberhar GPU cost coverage: feature=gpu_work.v2 sequence={} final={} timestamps_requested={} timestamps_available={} generic={} static={} full={} "
             "recovery={} gpu_optimized={} query_errors={} unavailable={} poll_cpu_ms={:.6f} pending={}",
             report_sequence, final, timestamps_requested, bool(pool), routes[0], routes[1], routes[2], routes[3], routes[4], query_errors,
             unavailable, profile_cpu_ns / 1e6,
             std::count_if(samples.begin(), samples.end(), [](const auto& sample) { return sample.state != 0; }));
    } catch (...) { ++query_errors; }
}
} // namespace Vulkan

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include "common/logging/log.h"
#include "common/uberhar_test_profile.h" // CodexAstraUlt: Keep both Combo compute routes equal.
#include "video_core/renderer_vulkan/uberhar_compute_rect_shader.h"
#include "video_core/renderer_vulkan/vk_compute_rect.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Vulkan {
ComputeRectRenderer::ComputeRectRenderer(const Instance& instance_, Scheduler& scheduler_,
                                         DescriptorUpdateQueue& updates_)
    : instance{instance_}, scheduler{scheduler_}, updates{updates_},
      mode{Settings::values.uberhar_test_mode.GetValue()} {}

void ComputeRectRenderer::Initialize(vk::PipelineCache cache) {
    // AstraEH: Compile once before gameplay, using the application's persisted driver cache.
    // CodexAstraUlt: Replace AstraEH's Native-only exclusion with explicit compute
    // capability; existing presets and both Combo comparison routes keep their behavior.
    if (!Settings::AllowsComputeRendering(mode) || pipeline)
        return;
    const auto device = instance.GetDevice();
    const auto physical = instance.GetPhysicalDevice();
    const auto family = physical.getQueueFamilyProperties()[instance.GetGraphicsQueueFamilyIndex()];
    if (!(family.queueFlags & vk::QueueFlagBits::eCompute)) {
        // AstraEH Log Line: One capability rejection; all draws retain the native route.
        LOG_WARNING(Render_Vulkan, "Uberhar compute unavailable: graphics queue lacks compute");
        return;
    }
    try {
        constexpr std::array bindings{vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute}};
        heap = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(), bindings,
                                                32);
        const vk::PushConstantRange range{vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(ComputeRectPacket)};
        layout = device.createPipelineLayoutUnique({.setLayoutCount = 1,
                                                    .pSetLayouts = &heap->Layout(),
                                                    .pushConstantRangeCount = 1,
                                                    .pPushConstantRanges = &range});
        const auto code = CompileGLSL(ComputeRectShader, vk::ShaderStageFlagBits::eCompute);
        module = device.createShaderModuleUnique(
            {.codeSize = code.size() * sizeof(u32), .pCode = code.data()});
        pipeline = device
                       .createComputePipelineUnique(
                           cache, {.stage{.stage = vk::ShaderStageFlagBits::eCompute,
                                          .module = *module,
                                          .pName = "main"},
                                   .layout = *layout})
                       .value;
    } catch (const std::exception& error) {
        pipeline.reset();
        // AstraEH Log Line: Never advertise a compute draw after startup compilation failed.
        LOG_ERROR(Render_Vulkan, "Uberhar compute preparation failed: {}", error.what());
        return;
    }
    // AstraEH: Timestamps are optional. Their absence selects native in automatic mode,
    // rather than claiming a faster route without a measurement. No WAIT_BIT is used.
    const auto limits = physical.getProperties().limits;
    if (family.timestampValidBits && limits.timestampComputeAndGraphics) {
        timestamp_period = limits.timestampPeriod;
        timestamp_mask =
            family.timestampValidBits >= 64 ? ~u64{} : (u64{1} << family.timestampValidBits) - 1;
        try {
            queries = device.createQueryPoolUnique(
                {.queryType = vk::QueryType::eTimestamp, .queryCount = 64});
        } catch (const std::exception& error) {
            // AstraEH Log Line: A measurement failure must not disable valid rendering.
            LOG_WARNING(Render_Vulkan, "Uberhar compute timing unavailable: {}", error.what());
        }
    }
    // AstraEH Log Line: Startup reports the true subset and whether automatic selection can learn.
    LOG_INFO(Render_Vulkan,
             "Uberhar compute prepared: coverage=solid_replace_rectangles pipeline_count=1 "
             "runtime_compilation=false timestamps={} mode={}",
             bool(queries), static_cast<u32>(mode));
}

bool ComputeRectRenderer::Choose(const ComputeRectPacket& packet) {
    ++eligible;
    if (!Ready())
        return false;
    if (mode == Settings::UberharTestMode::Compute)
        return true;
    // CodexAstraUlt: Both Combo presets use the same measured rectangle selection.
    return Settings::UsesAutomaticCompute(mode) && queries &&
           selector.Select(ComputeRectSelector::Bucket(packet.PixelCount()));
}

void ComputeRectRenderer::Draw(Surface& surface, const ComputeRectPacket& packet) {
    const auto set = heap->Commit();
    updates.AddStorageImage(set, 0, surface.StorageView());
    // AstraEH: Copy all handles and packet data into the command. The image remains
    // owned by the existing fenced surface cache; descriptors use fenced heap slots.
    scheduler.Record([image = surface.Image(), view_layout = *layout, target = *pipeline, set,
                      packet](vk::CommandBuffer cmdbuf) {
        vk::ImageMemoryBarrier barrier{
            .srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                               vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, barrier);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, target);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, view_layout, 0, set, {});
        cmdbuf.pushConstants(view_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(packet),
                             &packet);
        cmdbuf.dispatch((packet.rect[2] + 7) / 8, (packet.rect[3] + 7) / 8, 1);
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, barrier);
    });
    scheduler.MakeDirty(StateFlags::Pipeline | StateFlags::DescriptorSets);
    ++compute_draws;
    compute_pixels += packet.PixelCount();
}

int ComputeRectRenderer::ReserveSample(bool compute, u64 pixels) {
    if (!queries)
        return -1;
    // AstraEH: Bound measurement overhead after initial samples; never wait for a free slot.
    const auto bucket = ComputeRectSelector::Bucket(pixels);
    // CodexAstraUlt: Fragment isolation must not change compute warm-up measurements.
    const bool exploring = Settings::UsesAutomaticCompute(mode) && selector.NeedsSample(bucket);
    if (++measurement_attempts > 64 && !exploring && (measurement_attempts & 31) != 0)
        return -1;
    for (unsigned i = 0; i < samples.size(); ++i) {
        if (samples[i].pending)
            continue;
        samples[i] = {0, bucket, compute, true};
        return static_cast<int>(i);
    }
    return -1;
}

void ComputeRectRenderer::BeginSample(int slot) {
    if (slot < 0)
        return;
    // AstraEH: Reserve before splitting the render pass, but reset only after it
    // ends. Unmeasured native draws therefore retain normal render-pass batching.
    samples[slot].tick = scheduler.CurrentTick();
    scheduler.Record([pool = *queries, slot](vk::CommandBuffer cmdbuf) {
        cmdbuf.resetQueryPool(pool, slot * 2, 2);
        cmdbuf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, pool, slot * 2);
    });
}

void ComputeRectRenderer::EndSample(int slot) {
    if (slot < 0)
        return;
    scheduler.Record([pool = *queries, slot](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, pool, slot * 2 + 1);
    });
}

void ComputeRectRenderer::Poll() {
    if (!queries)
        return;
    for (unsigned i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        if (!sample.pending || !scheduler.IsFree(sample.tick))
            continue;
        std::array<u64, 4> result{};
        const auto status = instance.GetDevice().getQueryPoolResults(
            *queries, i * 2, 2, sizeof(result), result.data(), sizeof(u64) * 2,
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
        if (status != vk::Result::eSuccess || !result[1] || !result[3])
            continue;
        const double ns = ((result[2] - result[0]) & timestamp_mask) * timestamp_period;
        selector.Record(sample.bucket, sample.compute, ns);
        ++measured_draws[sample.compute];
        measured_ns[sample.compute] += ns;
        sample.pending = false;
    }
}

// CodexAstraLocal: Consume/reset before optional output so a dropped or throwing
// log cannot double-count a later interval. Top rows are censored; unranked draws
// and fixed mask-family upper bounds are computed from the complete bank.
// Ranked counts mean selected for emission; optional logging may lose rows.
void ComputeRectRenderer::ReportCensus(std::chrono::steady_clock::time_point now,
                                      bool final) noexcept {
    constexpr auto bit = [](ComputeRectReject reason) { return 1U << static_cast<unsigned>(reason); };
    constexpr u32 scissor_cull = bit(ComputeRectReject::Scissor) | bit(ComputeRectReject::Cull);
    constexpr u32 color_alpha_scissor_cull = scissor_cull | bit(ComputeRectReject::ColorWrite) |
                                            bit(ComputeRectReject::Alpha);
    const auto snapshot = state_census.Consume({scissor_cull, color_alpha_scissor_cull});
    census_overflow |= snapshot.overflow;
    for (unsigned i = 0; i < rejected_state.size(); ++i)
        ComputeStateCensus::Add(rejected_state[i], snapshot.marginal[i], census_overflow);
    const bool conservation = !census_overflow && !snapshot.invalid.six && !snapshot.invalid.other &&
        considered >= census_considered && unsupported >= census_unsupported &&
        snapshot.draws == considered - census_considered &&
        snapshot.rejected == unsupported - census_unsupported;
    const bool clock_valid = now >= census_start;
    const double window_ms = clock_valid
        ? std::chrono::duration<double, std::milli>(now - census_start).count() : 0.0;
    census_start = now;
    census_considered = considered;
    census_unsupported = unsupported;
    ++census_sequence;
    try {
        const auto delivery = final ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic;
        // CodexAstraLocal Log Line: At most one summary plus four groups per
        // existing 30s boundary and final flush; only counts and state masks.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
            "Uberhar compute state census: schema=1 mode={} window={} final={} "
            "scope=cpu_draw_attempts window_ms={:.3f} clock_valid={} considered={} six={} other={} "
            "state_admitted={} admitted_six={} unsupported={} considered_total={} unsupported_total={} "
            "distinct_masks={} ranked_groups={} ranked_draws={} ranked_six={} "
            "unranked_draws={} unranked_six={} invalid_mask_draws={} overflow={} conservation={} "
            "state_six_scissor_cull={} state_six_color_alpha_scissor_cull={} "
            "prior_log_failures={} six_scope=two_triangles_not_rectangle_proof",
            static_cast<u32>(mode), census_sequence, final, window_ms, clock_valid,
            snapshot.draws, snapshot.total.six, snapshot.total.other,
            snapshot.admitted_draws, snapshot.admitted.six,
            snapshot.rejected, considered, unsupported, snapshot.distinct, snapshot.groups,
            snapshot.ranked_draws, snapshot.ranked.six,
            snapshot.unranked_draws, snapshot.unranked.six,
            snapshot.invalid_draws, census_overflow, conservation,
            snapshot.state_only_six[0], snapshot.state_only_six[1], census_log_failures);
        for (unsigned i = 0; i < snapshot.groups; ++i) {
            const auto& group = snapshot.top[i];
            // CodexAstraLocal Log Line: Same interval identity binds each
            // optional top row. Consumers must first partition the exact process/
            // renderer lifecycle; a missing row is not an absent mask.
            LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                "Uberhar compute state group: schema=1 mode={} window={} final={} rank={} "
                "mask={} draws={} six={} other={} max_groups=4 scope=interval",
                static_cast<u32>(mode), census_sequence, final, i + 1,
                group.mask, group.draws, group.counts.six, group.counts.other);
        }
    } catch (...) {
        // CodexAstraLocal: Optional diagnostics cannot interrupt rendering or
        // retry a consumed bank after allocation/formatting failure.
        ++census_log_failures;
    }
}

void ComputeRectRenderer::Report() {
    // CodexAstraLocal: Flush every remaining draw before lifetime marginals;
    // teardown duration is part of this last interval, not active-render time.
    ReportCensus(std::chrono::steady_clock::now(), true);
    // AstraEH Log Line: One non-exclusive rejection summary; totals can exceed rejected draws.
    LOG_INFO(Render_Vulkan,
             "Uberhar compute blockers: shadow={} color_write={} depth_test={} depth_write={} "
             "stencil={} alpha={} clip={} scissor={} cull={} fog={} blend={}",
             rejected_state[0], rejected_state[1], rejected_state[2], rejected_state[3],
             rejected_state[4], rejected_state[5], rejected_state[6], rejected_state[7],
             rejected_state[8], rejected_state[9], rejected_state[10]);
    // AstraEH Log Line: One final coverage/timing report; sampled GPU times are not frame times.
    LOG_INFO(
        Render_Vulkan,
        "Uberhar virtual routes totals: mode={} considered={} unsupported_state={} "
        "rejected_geometry={} "
        "rejected_format={} eligible_rectangles={} native_draws={} compute_draws={} "
        "compute_pixels={} "
        "native_gpu_samples={} native_gpu_ms={:.3f} compute_gpu_samples={} compute_gpu_ms={:.3f}",
        static_cast<u32>(mode), considered, unsupported, geometry_rejected, format_rejected,
        eligible, native_draws, compute_draws, compute_pixels, measured_draws[0],
        measured_ns[0] / 1e6, measured_draws[1], measured_ns[1] / 1e6);
}
} // namespace Vulkan

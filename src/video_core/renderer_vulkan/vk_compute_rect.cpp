// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include "common/logging/log.h"
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
    if (mode == Settings::UberharTestMode::Native || pipeline)
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
    return mode == Settings::UberharTestMode::Automatic && queries &&
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
    const bool exploring =
        mode == Settings::UberharTestMode::Automatic && selector.NeedsSample(bucket);
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

void ComputeRectRenderer::Report() const {
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

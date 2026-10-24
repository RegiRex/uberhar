// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/uberhar_test_profile.h" // CodexAstraLocal: Bound diagnostics to Native/Combo.
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Vulkan {

constexpr u32 MinDrawsToFlush = 20;

using VideoCore::PixelFormat;
using VideoCore::SurfaceType;

RenderManager::RenderManager(const Instance& instance, Scheduler& scheduler)
    : instance{instance}, scheduler{scheduler} {
    // CodexAstraLocal TEST-ONLY LOG: Query capability is explicit; no timing
    // failure may disable valid graphics. Remove after pass costs are qualified.
    diagnostics_enabled = Settings::IsUberharTestProfile(Settings::values.uberhar_test_mode.GetValue());
    if (!diagnostics_enabled) return;
    try {
    diagnostic_next_report = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    const auto physical = instance.GetPhysicalDevice();
    const auto family = physical.getQueueFamilyProperties()[instance.GetGraphicsQueueFamilyIndex()];
    const auto limits = physical.getProperties().limits;
    if (family.timestampValidBits && limits.timestampComputeAndGraphics) {
        diagnostic_period = limits.timestampPeriod;
        diagnostic_mask = family.timestampValidBits >= 64 ? ~u64{}
            : (u64{1} << family.timestampValidBits) - 1;
        try {
            diagnostic_queries = instance.GetDevice().createQueryPoolUnique(
                {.queryType = vk::QueryType::eTimestamp, .queryCount = 32});
        } catch (const std::exception& error) {
            ++diagnostic_errors;
            LOG_WARNING(Render_Vulkan, "Uberhar pass timing unavailable: {}", error.what());
        }
    }
    LOG_INFO(Render_Vulkan,
        "Uberhar GPU pass timing: schema=1 enabled={} slots=16 stride=257 max_samples_per_frame=4 "
        "wait=false turnip_flush_requested={} effective_pass_flush={}",
        bool(diagnostic_queries), Settings::values.turnip_pass_end_flush.GetValue(), ShouldFlushPass());
    } catch (...) {
        // CodexAstraLocal: Capability lookup, allocation and optional logging
        // are all nonfatal observers. No commands use the pool at construction.
        diagnostic_queries.reset();
        ++diagnostic_errors;
    }
}

// CodexAstraLocal: RendererVulkan drains commands/device before member teardown;
// queries remain alive through the final poll and add no destruction-time wait.
RenderManager::~RenderManager() { PollDiagnostics(true); }

bool RenderManager::ShouldFlushPass() const {
    // CodexAstraLocal: Isolate only the existing pass-end policy on Mesa Turnip.
    // Keep proprietary-driver defaults, threshold and asynchronous Flush intact.
    return instance.ShouldFlush() ||
        (instance.GetDriverID() == vk::DriverId::eMesaTurnip &&
         Settings::values.turnip_pass_end_flush.GetValue());
}

void RenderManager::BeginDiagnosticPass(const RenderPass& info, bool framebuffer_pass) {
    if (!diagnostics_enabled) return;
    ++diagnostic_passes;
    diagnostic_clears += info.do_clear != 0;
    if (!diagnostic_queries || diagnostic_errors || diagnostic_frame_samples >= 4 ||
        diagnostic_passes % 257 != 1) return;
    for (u32 i = 0; i < diagnostic_samples.size(); ++i) {
        auto& sample = diagnostic_samples[i];
        if (sample.pending) continue;
        sample = {0, u64{info.render_area.extent.width} * info.render_area.extent.height,
                  true, false, info.do_clear != 0, framebuffer_pass};
        diagnostic_active = static_cast<int>(i);
        ++diagnostic_frame_samples;
        // CodexAstraLocal: EndRendering already closed the previous pass. Query
        // reset/start precede the new pass, without changing render boundaries.
        scheduler.Record([pool = *diagnostic_queries, i](vk::CommandBuffer cmdbuf) {
            cmdbuf.resetQueryPool(pool, i * 2, 2);
            cmdbuf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, pool, i * 2);
        });
        return;
    }
    ++diagnostic_full;
}

void RenderManager::EndDiagnosticPass() {
    if (diagnostic_active < 0) return;
    const auto i = static_cast<u32>(diagnostic_active);
    scheduler.Record([pool = *diagnostic_queries, i](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, pool, i * 2 + 1);
    });
    // CodexAstraLocal: Stamp the final recording tick, after the pass/barrier;
    // only a completed pair can be polled or reused across command buffers.
    diagnostic_samples[i].tick = scheduler.CurrentTick();
    diagnostic_samples[i].ended = true;
    diagnostic_active = -1;
}

void RenderManager::PollDiagnostics(bool final) noexcept {
    if (!diagnostics_enabled) return;
    diagnostic_frame_samples = 0;
    try {
        if (diagnostic_queries && !diagnostic_errors) {
            for (u32 i = 0; i < diagnostic_samples.size(); ++i) {
                auto& sample = diagnostic_samples[i];
                if (!sample.pending || !sample.ended || !scheduler.IsFree(sample.tick)) continue;
                std::array<u64, 4> values{};
                const auto result = instance.GetDevice().getQueryPoolResults(
                    *diagnostic_queries, i * 2, 2, sizeof(values), values.data(), sizeof(u64) * 2,
                    vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
                if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
                    ++diagnostic_errors;
                    break;
                }
                if (result == vk::Result::eNotReady || !values[1] || !values[3]) {
                    ++diagnostic_unavailable;
                    continue;
                }
                const double ns = ((values[2] - values[0]) & diagnostic_mask) * diagnostic_period;
                diagnostic_ns += ns;
                diagnostic_kind_ns[sample.framebuffer_pass] += ns;
                ++diagnostic_kind_samples[sample.framebuffer_pass];
                diagnostic_max_ns = std::max(diagnostic_max_ns, static_cast<u64>(ns));
                diagnostic_pixels += sample.pixels;
                diagnostic_clear_samples += sample.clear;
                ++diagnostic_sampled;
                sample.pending = false;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (!final && now < diagnostic_next_report) return;
        diagnostic_next_report = now + std::chrono::seconds{5};
        scheduler.ReportDiagnostics(final);
        const auto pending = std::count_if(diagnostic_samples.begin(), diagnostic_samples.end(),
            [](const auto& sample) { return sample.pending; });
        // CodexAstraLocal TEST-ONLY LOG: Cumulative values permit interval deltas;
        // sampled pass+dependency time is not pure shader time or a pixel count.
        // Remove after driver policy/admission experiments are qualified.
        LOG_INFO(Render_Vulkan,
            "Uberhar GPU passes {}: schema=1 passes={} reused={} clears={} policy_flushes={} "
            "samples={} gpu_ms={:.3f} max_pass_ms={:.3f} sampled_target_pixels={} clear_samples={} "
            "pending={} ring_full={} unavailable={} errors={} current_tick={} completed_tick={} "
            "framebuffer_samples={} framebuffer_ms={:.3f} utility_samples={} utility_ms={:.3f} "
            "scope=sampled_pass_plus_dependencies presentation_time=unmeasured bandwidth=unavailable",
            final ? "final" : "progress", diagnostic_passes, diagnostic_reuses, diagnostic_clears,
            diagnostic_flushes, diagnostic_sampled, diagnostic_ns / 1e6, diagnostic_max_ns / 1e6,
            diagnostic_pixels, diagnostic_clear_samples, pending, diagnostic_full,
            diagnostic_unavailable, diagnostic_errors, scheduler.CurrentTick(),
            scheduler.GetMasterSemaphore()->KnownGpuTick(), diagnostic_kind_samples[1],
            diagnostic_kind_ns[1] / 1e6, diagnostic_kind_samples[0], diagnostic_kind_ns[0] / 1e6);
    } catch (...) {
        // CodexAstraLocal: Optional observer failure stops further query work,
        // retaining the pool until ordinary GPU teardown instead of risking reuse.
        ++diagnostic_errors;
    }
}

void RenderManager::BeginRendering(const Framebuffer* framebuffer,
                                   Common::Rectangle<u32> draw_rect) {
    const vk::Rect2D render_area = {
        .offset{
            .x = static_cast<s32>(draw_rect.left),
            .y = static_cast<s32>(draw_rect.bottom),
        },
        .extent{
            .width = draw_rect.GetWidth(),
            .height = draw_rect.GetHeight(),
        },
    };
    const RenderPass new_pass = {
        .framebuffer = framebuffer->Handle(),
        .render_pass = framebuffer->RenderPass(),
        .render_area = render_area,
        .clear = {},
        .do_clear = false,
    };
    // CodexAstraLocal: End the previous pass using its own attachment metadata before replacement.
    BeginRendering(new_pass, true);
    images = framebuffer->Images();
    aspects = framebuffer->Aspects();
    shadow_rendering = framebuffer->shadow_rendering;
}

void RenderManager::BeginRendering(const RenderPass& new_pass, bool framebuffer_pass) {
    if (pass == new_pass) [[likely]] {
        num_draws++;
        if (diagnostics_enabled) ++diagnostic_reuses;
        return;
    }

    EndRendering();
    BeginDiagnosticPass(new_pass, framebuffer_pass);
    scheduler.Record([info = new_pass](vk::CommandBuffer cmdbuf) {
        const vk::RenderPassBeginInfo renderpass_begin_info = {
            .renderPass = info.render_pass,
            .framebuffer = info.framebuffer,
            .renderArea = info.render_area,
            .clearValueCount = info.do_clear ? 1u : 0u,
            .pClearValues = &info.clear,
        };
        cmdbuf.beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
    });

    pass = new_pass;
}

void RenderManager::EndRendering() {
    if (!pass.render_pass) {
        return;
    }

    scheduler.Record([images = images, aspects = aspects,
                      shadow_rendering = shadow_rendering](vk::CommandBuffer cmdbuf) {
        u32 num_barriers = 0;
        vk::PipelineStageFlags pipeline_flags{};
        vk::AccessFlags src_access_flags{};
        std::array<vk::ImageMemoryBarrier, 2> barriers;
        for (u32 i = 0; i < images.size(); i++) {
            if (!images[i]) {
                continue;
            }
            const bool is_color = static_cast<bool>(aspects[i] & vk::ImageAspectFlagBits::eColor);
            if (is_color) {
                pipeline_flags |= shadow_rendering
                                      ? vk::PipelineStageFlagBits::eFragmentShader
                                      : vk::PipelineStageFlagBits::eColorAttachmentOutput;
                src_access_flags = shadow_rendering ? vk::AccessFlagBits::eShaderWrite
                                                    : vk::AccessFlagBits::eColorAttachmentWrite;
            } else {
                pipeline_flags |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits::eLateFragmentTests;
                src_access_flags = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            }
            barriers[num_barriers++] = vk::ImageMemoryBarrier{
                .srcAccessMask = src_access_flags,
                .dstAccessMask =
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i],
                .subresourceRange{
                    .aspectMask = aspects[i],
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            };
        }
        cmdbuf.endRenderPass();
        if (num_barriers == 0) {
            return;
        }
        cmdbuf.pipelineBarrier(pipeline_flags,
                               vk::PipelineStageFlagBits::eFragmentShader |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr,
                               num_barriers, barriers.data());
    });

    EndDiagnosticPass();

    // Reset state.
    pass.render_pass = VK_NULL_HANDLE;
    images = {};
    aspects = {};
    shadow_rendering = false;

    // The Mali guide recommends flushing at the end of each major renderpass
    // Testing has shown this has a significant effect on rendering performance
    if (num_draws > MinDrawsToFlush && ShouldFlushPass()) {
        scheduler.Flush();
        if (diagnostics_enabled) ++diagnostic_flushes;
        num_draws = 0;
    }
}

vk::RenderPass RenderManager::GetRenderpass(VideoCore::PixelFormat color,
                                            VideoCore::PixelFormat depth, bool is_clear) {
    std::scoped_lock lock{cache_mutex};

    const u32 color_index =
        color == VideoCore::PixelFormat::Invalid ? NumColorFormats : static_cast<u32>(color);
    const u32 depth_index =
        depth == VideoCore::PixelFormat::Invalid
            ? NumDepthFormats
            : (static_cast<u32>(depth - VideoCore::PixelFormat::NumColorFormat));

    ASSERT_MSG(color_index <= NumColorFormats && depth_index <= NumDepthFormats,
               "Invalid color index {} and/or depth_index {}", color_index, depth_index);

    vk::UniqueRenderPass& renderpass = cached_renderpasses[color_index][depth_index][is_clear];
    if (!renderpass) {
        const vk::Format color_format = instance.GetTraits(color).native;
        const vk::Format depth_format = instance.GetTraits(depth).native;
        const vk::AttachmentLoadOp load_op =
            is_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
        renderpass = CreateRenderPass(color_format, depth_format, load_op);
    }

    return *renderpass;
}

vk::UniqueRenderPass RenderManager::CreateRenderPass(vk::Format color, vk::Format depth,
                                                     vk::AttachmentLoadOp load_op) const {
    u32 attachment_count = 0;
    std::array<vk::AttachmentDescription, 2> attachments;

    bool use_color = false;
    vk::AttachmentReference color_attachment_ref{};
    bool use_depth = false;
    vk::AttachmentReference depth_attachment_ref{};

    if (color != vk::Format::eUndefined) {
        attachments[attachment_count] = vk::AttachmentDescription{
            .format = color,
            .loadOp = load_op,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral,
        };

        color_attachment_ref = vk::AttachmentReference{
            .attachment = attachment_count++,
            .layout = vk::ImageLayout::eGeneral,
        };

        use_color = true;
    }

    if (depth != vk::Format::eUndefined) {
        attachments[attachment_count] = vk::AttachmentDescription{
            .format = depth,
            .loadOp = load_op,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = load_op,
            .stencilStoreOp = vk::AttachmentStoreOp::eStore,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral,
        };

        depth_attachment_ref = vk::AttachmentReference{
            .attachment = attachment_count++,
            .layout = vk::ImageLayout::eGeneral,
        };

        use_depth = true;
    }

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = use_color ? 1u : 0u,
        .pColorAttachments = &color_attachment_ref,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = use_depth ? &depth_attachment_ref : nullptr,
    };

    const vk::RenderPassCreateInfo renderpass_info = {
        .attachmentCount = attachment_count,
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = nullptr,
    };

    return instance.GetDevice().createRenderPassUnique(renderpass_info);
}

} // namespace Vulkan

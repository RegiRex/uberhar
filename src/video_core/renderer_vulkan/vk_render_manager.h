// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <chrono> // CodexAstraLocal: Bounded owner-cadence dependency diagnostics.
#include <mutex>

#include "common/math_util.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_work_profile.h" // CodexAstraLocal: Natural-pass attribution.

namespace VideoCore {
enum class PixelFormat : u32;
}

namespace Vulkan {

class Instance;
class Scheduler;
class Framebuffer;

struct RenderPass {
    vk::Framebuffer framebuffer;
    vk::RenderPass render_pass;
    vk::Rect2D render_area;
    vk::ClearValue clear;
    u32 do_clear;

    bool operator==(const RenderPass& other) const noexcept {
        return std::tie(framebuffer, render_pass, render_area, do_clear) ==
                   std::tie(other.framebuffer, other.render_pass, other.render_area,
                            other.do_clear) &&
               std::memcmp(&clear, &other.clear, sizeof(vk::ClearValue)) == 0;
    }
};

class RenderManager {
    static constexpr u32 NumColorFormats = static_cast<u32>(VideoCore::PixelFormat::NumColorFormat);
    static constexpr u32 NumDepthFormats = static_cast<u32>(VideoCore::PixelFormat::NumDepthFormat);

public:
    explicit RenderManager(const Instance& instance, Scheduler& scheduler);
    ~RenderManager();

    /// Begins a new renderpass with the provided framebuffer as render target.
    void BeginRendering(const Framebuffer* framebuffer, Common::Rectangle<u32> draw_rect);

    /// Begins a new renderpass with the provided render state.
    void BeginRendering(const RenderPass& new_pass, GpuWork kind = GpuWork::Utility);

    // CodexAstraLocal: Record the known next owner rather than infer a GPU
    // bottleneck from a broad attachment barrier. Unknown callers stay Other.
    enum class EndReason : u32 { Other, PassChange, Copy, Blit, Feedback, Count };
    void EndRendering(EndReason reason = EndReason::Other);
    void ReportDependencies(bool final = false) noexcept;

    /// Returns the renderpass associated with the color-depth format pair
    vk::RenderPass GetRenderpass(VideoCore::PixelFormat color, VideoCore::PixelFormat depth,
                                 bool is_clear);

private:
    /// Creates a renderpass configured appropriately and stores it in cached_renderpasses
    vk::UniqueRenderPass CreateRenderPass(vk::Format color, vk::Format depth,
                                          vk::AttachmentLoadOp load_op) const;

private:
    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueRenderPass cached_renderpasses[NumColorFormats + 1][NumDepthFormats + 1][2];
    std::mutex cache_mutex;
    std::array<vk::Image, 2> images;
    std::array<vk::ImageAspectFlags, 2> aspects;
    bool shadow_rendering{};
    RenderPass pass{};
    // CodexAstraLocal: Utility/clear passes cannot inherit graphics-only reuse.
    GpuWork pass_kind{GpuWork::Utility};
    u32 num_draws{};
    // CodexAstraLocal TEST-ONLY LOG: Owner-only cumulative dependency reasons;
    // remove after precise barrier populations are qualified, never remove required visibility.
    const bool dependency_diagnostics;
    std::array<u64, static_cast<u32>(EndReason::Count)> dependency_ends{};
    u64 dependency_attachments{}, dependency_shadow{}, dependency_sequence{};
    // CodexAstraLocal TEST-ONLY LOG: Remove after contained-area reuse is qualified.
    u64 contained_area_reuses{};
    u32 dependency_prescale{};
    std::chrono::steady_clock::time_point dependency_report_time{std::chrono::steady_clock::now()};
};

} // namespace Vulkan

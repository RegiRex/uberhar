// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <atomic> // CodexAstraLocal: Publish sampled worker metadata without a new wait.
#include <chrono> // CodexAstraLocal: Owner-frame diagnostic cadence, never a per-draw clock.

#include "common/math_util.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace VideoCore {
enum class PixelFormat : u32;
}

namespace Vulkan {

class Instance;
class Scheduler;
class Framebuffer;
class PipelineCache;

// CodexAstraLocal TEST-ONLY LOG: Count existing boundaries without changing them;
// remove after attachment/pass-cost qualification. Feedback counts requests too.
enum class PassEndReason : u32 { Explicit, PassChange, Submission, Feedback, Count };

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
    void BeginRendering(const RenderPass& new_pass, bool framebuffer_pass = false);

    /// Exits from any currently active renderpass instance
    void EndRendering(PassEndReason reason = PassEndReason::Explicit);

    // CodexAstraLocal TEST-ONLY LOG: Call only after an actual graphics draw enqueue.
    // The sampled observer reads the worker's fulfilled binding, never a requested
    // shader that may lose fallback arbitration. Remove with pass-cost diagnostics.
    void RecordDiagnosticDraw(const PipelineCache& pipelines, u32 color_addr, u32 depth_addr);

    // CodexAstraLocal TEST-ONLY LOG: Resolve sparse GPU samples at the existing
    // frame/teardown boundary; remove after pass/submission costs are qualified.
    void PollDiagnostics(bool final = false) noexcept;

    /// Returns the renderpass associated with the color-depth format pair
    vk::RenderPass GetRenderpass(VideoCore::PixelFormat color, VideoCore::PixelFormat depth,
                                 bool is_clear);

private:
    /// Creates a renderpass configured appropriately and stores it in cached_renderpasses
    vk::UniqueRenderPass CreateRenderPass(vk::Format color, vk::Format depth,
                                          vk::AttachmentLoadOp load_op) const;

    // CodexAstraLocal: Query reset/start is outside the natural pass; its end
    // includes the existing attachment dependency and never adds a pass split.
    void BeginDiagnosticPass(const RenderPass& info, bool framebuffer_pass);
    void EndDiagnosticPass();
    bool ShouldFlushPass() const;

private:
    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueRenderPass cached_renderpasses[NumColorFormats + 1][NumDepthFormats + 1][2];
    std::mutex cache_mutex;
    std::array<vk::Image, 2> images;
    std::array<vk::ImageAspectFlags, 2> aspects;
    bool shadow_rendering{};
    RenderPass pass{};
    u32 num_draws{};
    // CodexAstraLocal TEST-ONLY LOG: Fixed residency and per-frame sample budget
    // bound observer cost. A pending slot is reused only after GPU completion
    // AND query availability; remove with the pass-cost experiment.
    struct PassSample {
        u64 tick{}, pixels{};
        bool pending{}, ended{}, clear{}, framebuffer_pass{};
        // CodexAstraLocal: Owner metadata is fixed before queueing; attribution
        // is worker-only until the end command publishes and the GPU tick retires.
        u64 pass_id{}, frame_begin{}, frame_end{};
        PassEndReason end_reason{};
        struct Attribution {
            u64 draws{}, unattributed{}, pipeline_changes{}, shader_changes{}, target_changes{};
            u64 first_frame{}, last_frame{}, first_pipeline{}, last_pipeline{};
            std::array<u64, 3> first_shaders{}, last_shaders{};
            std::array<u32, 2> first_target{}, last_target{};
        } attribution;
    };
    vk::UniqueQueryPool diagnostic_queries;
    std::array<PassSample, 16> diagnostic_samples{};
    // CodexAstraLocal: Acquire/release covers host metadata explicitly; query
    // availability alone is not C++ synchronization. Slots retain GPU gating too.
    std::array<std::atomic_bool, 16> diagnostic_published{};
    int diagnostic_active{-1};
    bool diagnostics_enabled{};
    u64 diagnostic_mask{};
    double diagnostic_period{};
    u32 diagnostic_frame_samples{};
    u64 diagnostic_passes{}, diagnostic_clears{}, diagnostic_reuses{}, diagnostic_flushes{};
    u64 diagnostic_sampled{}, diagnostic_full{}, diagnostic_unavailable{}, diagnostic_errors{};
    u64 diagnostic_pixels{}, diagnostic_max_ns{}, diagnostic_clear_samples{};
    // CodexAstraLocal TEST-ONLY LOG: Fixed counters and one representative for
    // each report interval, with no shader map or per-sample logging.
    u64 diagnostic_frame{1}, diagnostic_draws{}, diagnostic_unattributed{};
    u64 diagnostic_mixed_shader_samples{}, diagnostic_mixed_pipeline_samples{};
    u64 diagnostic_feedback_requests{};
    std::array<u64, static_cast<std::size_t>(PassEndReason::Count)> diagnostic_end_reasons{};
    PassSample diagnostic_slowest{};
    double diagnostic_slowest_ns{};
    double diagnostic_ns{};
    // CodexAstraLocal TEST-ONLY LOG: Separate sampled framebuffer graphics from
    // explicit clear/utility passes; neither includes unmeasured presentation.
    std::array<double, 2> diagnostic_kind_ns{};
    std::array<u64, 2> diagnostic_kind_samples{};
    std::chrono::steady_clock::time_point diagnostic_next_report{};
};

} // namespace Vulkan

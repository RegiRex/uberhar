// Copyright 2022-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono> // AstraEH: CPU bridge batch timing.

#include "video_core/rasterizer_accelerated.h"
// CodexAstraLocal: Keep the recovered optional output guard owned by the renderer.
#include "video_core/renderer_vulkan/uberhar_gpu_output_policy.h"
#include "video_core/renderer_vulkan/vk_compute_rect.h" // AstraEH: Bounded compute test path.
// CodexAstraLocal: Explicit scratch diagnostics have separate ownership/counters.
#include "video_core/renderer_vulkan/vk_compute_benchmark.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Frontend {
class EmuWindow;
}

namespace VideoCore {
class CustomTexManager;
class RendererBase;
} // namespace VideoCore

namespace Pica {
struct DisplayTransferConfig;
struct MemoryFillConfig;
struct FramebufferConfig;
} // namespace Pica

namespace Vulkan {

struct ScreenInfo;

class Instance;
class Scheduler;
class RenderManager;

class RasterizerVulkan : public VideoCore::RasterizerAccelerated {
public:
    explicit RasterizerVulkan(Memory::MemorySystem& memory, Pica::PicaCore& pica,
                              VideoCore::CustomTexManager& custom_tex_manager,
                              VideoCore::RendererBase& renderer, Frontend::EmuWindow& emu_window,
                              const Instance& instance, Scheduler& scheduler,
                              RenderManager& renderpass_cache, DescriptorUpdateQueue& update_queue,
                              u32 image_count);
    ~RasterizerVulkan() override;

    void TickFrame();
    void LoadDefaultDiskResources(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) override;

    void DrawTriangles() override;
    // CodexAstraLocal: Ordered state preparation and independently owned CPU
    // vertices meet only at the final coherent upload command.
    DeferredHardwareWriter PrepareDeferredVertices(u32 count) const override;
    bool DrawDeferredVertices(const std::shared_ptr<Pica::CpuDrawPacket>& packet) override;
    void DrainDeferredCommands() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;
    bool AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateFill(const Pica::MemoryFillConfig& config) override;
    bool AccelerateDisplay(const Pica::FramebufferConfig& config, PAddr framebuffer_addr,
                           u32 pixel_stride, ScreenInfo& screen_info);
    bool AccelerateDrawBatch(bool is_indexed) override;
    // AstraPro: Opt-in Combo route; no partial draws on a false return.
    bool AccelerateDrawBatchReady(bool is_indexed) override;

    // AstraEH: PicaCore uses this only immediately after the unsubmitted GPU attempt.
    bool HasPreparedCpuVertexBridge() const override {
        return cpu_bridge.ready != nullptr;
    }

    /// Switches the disk resources to the specified title
    void SwitchDiskResources(u64 title_id) override;

private:
    /// Syncs pipeline state from PICA registers
    void SyncDrawState();

    /// Syncs and uploads the lighting, fog and proctex LUTs
    void SyncAndUploadLUTs();
    void SyncAndUploadLUTsLF();

    /// Syncs all enabled PICA texture units
    void SyncTextureUnits(const Framebuffer* framebuffer);

    /// Syncs all utility textures in the fragment shader.
    void SyncUtilityTextures(const Framebuffer* framebuffer);

    /// Binds the PICA shadow cube required for shadow mapping
    void BindShadowCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                        vk::DescriptorSet texture_set);

    /// Binds a texture cube to texture unit 0
    void BindTextureCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                         vk::DescriptorSet texture_set);

    /// Upload the uniform blocks to the uniform buffer object
    void UploadUniforms(bool accelerate_draw);

    /// Generic draw function for DrawTriangles and AccelerateDrawBatch
    // CodexAstraLocal: Deferred CPU output changes only geometry readiness;
    // live draw state and every resource decision stay on the current owner.
    bool Draw(bool accelerate, bool is_indexed,
              const std::shared_ptr<Pica::CpuDrawPacket>& deferred = {});

    // CodexAstraLocal: Report explicit isolation omissions independently of the
    // optional compute owner and its valid-framebuffer census denominator.
    void ReportStrictCompute() const;

    /// Internal implementation for AccelerateDrawBatch
    bool AccelerateDrawBatchInternal(bool is_indexed);

    /// Setup index array for AccelerateDrawBatch
    void SetupIndexArray();

    /// Setup vertex array for AccelerateDrawBatch
    void SetupVertexArray();

    /// Setup the fixed attribute emulation in vulkan
    void SetupFixedAttribs();

    // CodexAstraLocal: Both cold loading and rights switches use this one-shot
    // diagnostic initializer; Native/Custom never read the optional sidecar.
    void InitializeVertexCapture(u64 title) noexcept;
    // CodexAstraLocal: Attempt the one-shot scratch request only at cold startup;
    // rights switches cannot rearm an ID or turn it into a guest fallback.
    void InitializeComputeBenchmark(u64 title);

    /// Setup vertex shader for AccelerateDrawBatch
    bool SetupVertexShader();

    /// Setup geometry shader for AccelerateDrawBatch
    bool SetupGeometryShader();

    /// Creates the vertex layout struct used for software shader pipelines
    void MakeSoftwareVertexLayout();

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;
    PipelineCache pipeline_cache;
    TextureRuntime runtime;
    RasterizerCache res_cache;

    // AstraEH: Created only for a selected test profile; custom rendering allocates nothing.
    std::unique_ptr<ComputeRectRenderer> compute_rect;
    // CodexAstraLocal: Default-off, bounded, private resources remain alive until
    // the same scheduler/GPU drain that already protects compute ownership.
    std::unique_ptr<ComputeBenchmark> compute_benchmark;
    bool compute_benchmark_attempted{};
    // CodexAstraLocal: Freeze isolation for this renderer lifetime. Counters
    // include no-target/no-owner exits; empty batches are not attempted draws.
    const bool strict_compute;
    struct StrictComputeStats {
        u64 attempts{}, computed{}, empty_batches{}, no_target{}, no_renderer{},
            state{}, format{}, geometry{}, not_ready{};
        u64 Omitted() const {
            return no_target + no_renderer + state + format + geometry + not_ready;
        }
    } strict_compute_stats;
    VertexLayout software_layout;
    std::array<u32, 16> binding_offsets{};
    std::array<bool, 16> enable_attributes{};
    std::array<vk::Buffer, 16> vertex_buffers;
    VertexArrayInfo vertex_info;
    PipelineInfo pipeline_info{};
    // AstraEH: One draw's pre-submission CPU routing decision, consumed by DrawTriangles.
    PipelineCache::CpuBridgePreparation cpu_bridge{};
    // AstraPro: Scoped to one call; no ready handle survives a draw or title switch.
    // AstraPro: Bounded host-state evidence shared by Native and Combo.
    u32 fixed_attribute_max_bytes{};
    u64 fixed_attribute_over_legacy{}, diagnostic_draws{};
    // CodexAstraUlt-2: Count input fallbacks; only the first four emit a record.
    u64 ready_vertex_zero_stride_rejections{};
    // CodexAstraUlt: Count each additional input-parity fallback separately, with
    // four records per reason and one reliable summary at rasterizer teardown.
    std::array<u64, static_cast<std::size_t>(ReadyVertexPolicy::InputLayoutIssue::Count)>
        ready_vertex_layout_rejections{};
    // CodexAstraUlt: Count lit draws lacking GPU quaternion correction; only four
    // detail records per rasterizer, followed by one existing teardown summary.
    u64 ready_vertex_quaternion_rejections{};
    // CodexAstraUlt: Optional-only output-default containment; no shader/register
    // mutation. Exact program memoization is lazy and bounded to 128 entries.
    // CodexAstraLocal: This memo and its counters die with the rasterizer; no
    // app cache files, Native shader state or cross-title verdicts are persisted.
    ReadyVertexPolicy::OutputWriteMemo<> ready_vertex_output_writes;
    u64 ready_vertex_output_checks{}, ready_vertex_output_w_checks{},
        ready_vertex_output_rejections{};
    std::chrono::steady_clock::time_point next_draw_snapshot{};
    // CodexAstraUlt: Existing TickFrame owner samples memory at most every 30 seconds;
    // the clock is checked once per 64 completed frames, never once per draw.
    u64 memory_diagnostic_frames{};
    std::chrono::steady_clock::time_point next_memory_snapshot{};
    bool ready_vertex_attempt{};
    GraphicsPipeline* ready_vertex_pipeline{};
    // CodexAstraLocal: No capture allocation exists without a valid opt-in file.
    std::unique_ptr<VertexCapture::Session> vertex_capture;
    bool vertex_capture_initialized{};
    std::chrono::steady_clock::time_point cpu_bridge_start{};

    StreamBuffer stream_buffer;     ///< Vertex+Index buffer
    StreamBuffer uniform_buffer;    ///< Uniform buffer
    StreamBuffer texture_buffer;    ///< Texture buffer
    StreamBuffer texture_lf_buffer; ///< Texture Light-Fog buffer
    vk::UniqueBufferView texture_lf_view;
    vk::UniqueBufferView texture_rg_view;
    vk::UniqueBufferView texture_rgba_view;
    vk::DeviceSize uniform_buffer_alignment;
    u32 uniform_size_aligned_vs_pica;
    u32 uniform_size_aligned_vs;
    u32 uniform_size_aligned_fs;
    bool async_shaders{false};
};

} // namespace Vulkan

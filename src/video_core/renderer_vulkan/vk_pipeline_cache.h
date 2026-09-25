// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <bitset>
#include <unordered_map> // AstraEH: Separate in-memory caches for experimental TEV fallbacks.
#include <unordered_set> // AstraEH: Bounded census of candidate fallback state dimensions.

#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/uberhar_pipeline_policy.h" // AstraEH: Admission reason counters.
#include "video_core/renderer_vulkan/uberhar_wait_diagnostics.h" // AstraEH: Bounded worst waits.
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h" // AstraEH: Shared fallback ABI.
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Pica {
struct RegsInternal;
struct ShaderSetup;
} // namespace Pica

namespace Vulkan {

class Instance;
class Scheduler;
class RenderManager;
class DescriptorUpdateQueue;

enum class DescriptorHeapType : u32 {
    Buffer,
    Texture,
    Utility,
};

/**
 * Stores a collection of rasterizer pipelines used during rendering.
 */
class PipelineCache {
    static constexpr u32 NumRasterizerSets = 3;
    static constexpr u32 NumDescriptorHeaps = 3;
    static constexpr u32 NumDynamicOffsets = 3;

public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           RenderManager& renderpass_cache, DescriptorUpdateQueue& update_queue);
    ~PipelineCache();

    // AstraEH: Startup compute preparation shares persisted driver data, not guest shader records.
    vk::PipelineCache DriverCache() const {
        return driver_pipeline_cache.get();
    }

    /// Acquires and binds a free descriptor set from the appropriate heap.
    vk::DescriptorSet Acquire(DescriptorHeapType type) {
        const u32 index = static_cast<u32>(type);
        const auto descriptor_set = descriptor_heaps[index].Commit();
        bound_descriptor_sets[index] = descriptor_set;
        return descriptor_set;
    }

    /// Sets the dynamic offset for the uniform buffer at binding
    void UpdateRange(u8 binding, u32 offset) {
        offsets[binding] = offset;
    }

    /// Loads the driver pipeline cache and the disk shader cache
    void LoadCache(const std::atomic_bool& stop_loading = std::atomic_bool{false},
                   const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Switches the driver pipeline cache and the shader disk cache to the specified title
    void SwitchCache(u64 title_id, const std::atomic_bool& stop_loading = std::atomic_bool{false},
                     const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Binds a pipeline using the provided information
    // AstraEH: A ready CPU bridge may bind its generic pipeline directly; admitting
    // another GPU-vertex fallback is optional while the shared CPU route warms.
    bool BindPipeline(PipelineInfo& info, bool wait_built = false,
                      GraphicsPipeline* ready_cpu_fallback = nullptr, bool allow_tev_build = true);

    struct CpuBridgePreparation {
        GraphicsPipeline* ready{};
        bool preferred{};
    };
    // AstraEH: Called before any draw submission. A non-null result authorizes
    // the existing PICA CPU vertex path, with a compatible completed GPU pipeline.
    CpuBridgePreparation PrepareCpuFallback(const PipelineInfo& info,
                                            const VertexLayout& software_layout, u32 vertices);
    void RecordCpuBridgeVertices(u64 elapsed_ns, u32 vertices);

    Pica::Shader::Generator::ExtraVSConfig CalcExtraConfig(
        const Pica::Shader::Generator::PicaVSConfig& config);

    /// Binds a PICA decompiled vertex shader
    bool UseProgrammableVertexShader(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup,
                                     const VertexLayout& layout);

    /// Binds a passthrough vertex shader
    void UseTrivialVertexShader();

    /// Binds a PICA decompiled geometry shader
    bool UseFixedGeometryShader(const Pica::RegsInternal& regs);

    /// Binds a passthrough geometry shader
    void UseTrivialGeometryShader();

    /// Binds a fragment shader generated from PICA state
    void UseFragmentShader(const Pica::RegsInternal& regs, const Pica::Shader::UserConfig& user);

    /// Gets the current program ID
    u64 GetProgramID() const {
        return current_program_id;
    }

    void SetProgramID(u64 program_id) {
        current_program_id = program_id;
    }

    void SetAccurateMul(bool _accurate_mul) {
        profile.enable_accurate_mul = _accurate_mul;
    }

private:
    // AstraEH: Only the serial TEV worker reads/writes generic modules; reports use atomics.
    std::vector<u32> LoadOrCompileTevModule(std::string_view source);
    std::atomic<u64> generic_module_hits{}, generic_module_misses{}, generic_module_rejected{},
        generic_module_write_failures{};
    friend ShaderDiskCache;

    /// Loads the driver pipeline cache
    void LoadDriverPipelineDiskCache(const std::atomic_bool& stop_loading = std::atomic_bool{false},
                                     const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Stores the generated pipeline cache
    void SaveDriverPipelineDiskCache();

    /// Loads the shader disk cache
    void LoadDiskCache(const std::atomic_bool& stop_loading = std::atomic_bool{false},
                       const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Switches the disk cache at runtime to use a different title ID
    void SwitchDiskCache(u64 title_id, const std::atomic_bool& stop_loading,
                         const VideoCore::DiskResourceLoadCallback& callback);

    /// Builds the rasterizer pipeline layout
    void BuildLayout();

    /// Returns true when the disk data can be used by the current driver
    bool IsCacheValid(std::span<const u8> cache_data) const;

    /// Create pipeline cache directories. Returns true on success.
    bool EnsureDirectories() const;

    /// Returns the Vulkan shader directory
    std::string GetVulkanDir() const;

    /// Returns the pipeline cache storage dir
    std::string GetPipelineCacheDir() const;

    /// Returns the transferable shader dir
    std::string GetTransferableDir() const;

    // AstraEH: Fallback cache lifecycle and diagnostics; see the implementation for support caps.
    GraphicsPipeline* GetTevFallback(const PipelineInfo& info, bool cpu_vertex = false);
    void ClearTevFallbacks();
    void ReportUberharStats(const char* kind = "totals");
    PipelineBuildOptions SpecializedBuildOptions() {
        return hybrid_tev
                   ? PipelineBuildOptions{&pipeline_completion, &specialized_build_stats, false}
                   : PipelineBuildOptions{};
    }

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;

    Pica::Shader::Profile profile{};
    vk::UniquePipelineCache driver_pipeline_cache;
    vk::UniquePipelineLayout pipeline_layout;
    std::size_t num_worker_threads;
    Common::ThreadWorker pipeline_workers;
    Common::ThreadWorker shader_workers;
    // AstraEH: Optional fallback work must not occupy workers needed by specialized draws.
    // Created only in hybrid mode; shader and driver compilation run as one serial job.
    std::unique_ptr<Common::ThreadWorker> tev_worker;
    PipelineInfo current_info{};
    // AstraEH: Only the scheduler accesses the actual bound pipeline. A queued draw may
    // choose a different winner from the render thread's original specialization.
    GraphicsPipeline* bound_pipeline{};
    Common::AsyncCompletion pipeline_completion;
    PipelineBuildStats specialized_build_stats;
    PipelineBuildStats fallback_build_stats;
    std::array<DescriptorHeap, NumDescriptorHeaps> descriptor_heaps;
    std::array<vk::DescriptorSet, NumRasterizerSets> bound_descriptor_sets{};
    std::array<u32, NumDynamicOffsets> offsets{};

    std::array<u64, MAX_SHADER_STAGES> shader_hashes;
    std::array<Shader*, MAX_SHADER_STAGES> current_shaders;

    Shader trivial_vertex_shader;

    // AstraEH: Separate maps keep experimental shaders out of the transferable cache.
    // Limit growth; after the limit we wait for the accurate specialized path.
    // AstraEH: The generator owns the versioned 120-byte transport and layout assertions.
    using TevPushConstants = Pica::Shader::Generator::GLSL::DynamicTevState;
    TevPushConstants tev_constants{};
    std::optional<Pica::Shader::FSConfig> tev_family_config;
    Pica::Shader::UserConfig tev_user{};
    bool tev_supported{};
    std::unordered_map<u64, std::unique_ptr<Shader>> tev_shaders;
    std::unordered_map<u64, std::unique_ptr<GraphicsPipeline>> tev_pipelines;
    // AstraEH: Renderer-thread-only, per-title observations before admission limits.
    // Dimensions: raw/canonical FS, raw/canonical pipeline, VS, GS, vertex layout,
    // attachments, blending, rasterization, depth/stencil, pre-0.0.13 family,
    // lighting and procedural shapes. A saturated set gives a lower bound only.
    std::array<std::unordered_set<u64>, 14> tev_candidate_keys;
    bool tev_census_capped{};
    // AstraEH: Renderer-thread counters only; emit aggregates at the existing five-second cadence.
    u64 cpu_bridge_pending{};
    u64 cpu_bridge_selected{};
    u64 cpu_bridge_warming{};
    u64 cpu_bridge_limited{};
    u64 cpu_bridge_draws{};
    u64 cpu_bridge_mismatches{};
    u64 cpu_bridge_batches{};
    u64 cpu_bridge_vertices{};
    u64 cpu_bridge_cpu_ns{};
    u64 cpu_bridge_cpu_max_ns{};
    // AstraEH: Renderer-owned bounded reason/topology counts, never per-draw text.
    std::array<u64, static_cast<std::size_t>(CpuBridgeAdmission::Count)> cpu_bridge_admission{};
    std::array<u64, 5> cpu_bridge_topology{}; // list, strip, fan, shader-list, unknown.
    std::array<u64, 5> cpu_bridge_selected_topology{};
    // AstraEH: Normal hybrid mode admits one warm-up pipeline at a time. Ready entries
    // remain usable; force mode may queue more because it explicitly waits for comparison.
    GraphicsPipeline* warming_tev_pipeline{};
    // AstraEH: Profiles use generic fragments as the primary path, with explicit recovery.
    std::optional<Pica::Shader::FSConfig> virtual_fs_config;
    u64 virtual_generic_draws{}, virtual_recovery_draws{}, virtual_waits{}, virtual_wait_ns{},
        virtual_max_wait_ns{};
    const bool hybrid_tev;
    const bool force_tev;
    // AstraEH: A/B switch captured at startup, effective only in normal hybrid mode.
    const bool cpu_vertex_bridge;
    // AstraEH: Draw counters belong to the render thread; wait counters belong to the scheduler.
    u64 draw_requests{};
    u64 specialized_pending{};
    std::atomic<u64> fallback_draws{};
    u64 fallback_warming{};
    u64 fallback_unavailable{};
    u64 fallback_deferred{};
    u64 skipped_draws{};
    std::atomic<u64> pipeline_waits{};
    std::atomic<u64> pipeline_wait_ns{};
    // AstraEH: Scheduler-only updates; read after draining for shutdown diagnostics.
    std::atomic<u64> pipeline_wait_max_ns{};
    std::atomic<u64> fallback_wait_ns{};
    std::atomic<u64> slow_pipeline_waits{};
    // AstraEH: Histograms survive detail caps; final worst records are read after drain.
    PipelineWaitDiagnostics wait_diagnostics;
    const std::chrono::steady_clock::time_point diagnostics_start =
        std::chrono::steady_clock::now();
    // AstraEH: Compiler totals are atomic so progress logging never races with a build.
    std::atomic<u64> fallback_compile_jobs{};
    std::atomic<u64> fallback_shader_ns{};
    std::atomic<u64> fallback_driver_ns{};
    std::atomic<u64> fallback_job_queue_ns{};
    // AstraEH: Error output is capped independently of successful build diagnostics.
    std::atomic<u64> fallback_failures{};
    // AstraEH: Count decisions on the scheduler, including fallbacks that became ready late.
    std::atomic<u64> first_ready_waits{};
    std::atomic<u64> late_fallback_draws{};
    std::chrono::steady_clock::time_point next_progress =
        std::chrono::steady_clock::now() + std::chrono::seconds{5};

    u64 current_program_id{0};
    std::vector<std::shared_ptr<ShaderDiskCache>> disk_caches;
    std::shared_ptr<ShaderDiskCache> curr_disk_cache{};
};

} // namespace Vulkan

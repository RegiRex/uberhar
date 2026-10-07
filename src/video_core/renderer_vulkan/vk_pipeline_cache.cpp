// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <bit>       // AstraEH: Count per-draw active TEV stages without fragment-side decoding.
#include <chrono>    // AstraEH: Measure actual scheduler waits for pipeline compilation.
#include <cstring>   // AstraEH: Select the already-drained final statistics path.
#include <optional>  // AstraEH: Android path-query declarations use optional strings.
#include <stdexcept> // AstraEH: Recover experimental compilation failures through specialization.
#include <boost/container/static_vector.hpp>

#include "common/arch.h" // CodexAstraLocal: Match the architectures with a production PICA JIT.
#include "common/common_paths.h"
#ifdef ANDROID
#include "common/android_utils.h" // AstraEH: Match the platform's cache path translation.
#endif
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scm_rev.h" // AstraEH: Fingerprint the compiler/generator cache ABI.
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/uberhar_test_profile.h" // CodexAstraUlt: Shared diagnostic route capabilities.
#include "core/core.h"
#include "core/loader/loader.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/renderer_vulkan/pica_to_vk.h"
#include "video_core/renderer_vulkan/uberhar_pipeline_policy.h" // AstraEH: Tested routing/wait policy.
#include "video_core/renderer_vulkan/uberhar_spirv_cache.h" // AstraEH: Reuse generic SPIR-V on warm runs.
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/spv_fs_shader_gen.h"
#include "video_core/shader_recovery_error.h" // AstraEH: Terminal recovery has a typed frontend path.

using namespace Pica::Shader::Generator;
using Pica::Shader::FSConfig;

MICROPROFILE_DEFINE(Vulkan_Bind, "Vulkan", "Pipeline Bind", MP_RGB(192, 32, 32));

namespace Vulkan {

u32 AttribBytes(Pica::PipelineRegs::VertexAttributeFormat format, u32 size) {
    switch (format) {
    case Pica::PipelineRegs::VertexAttributeFormat::FLOAT:
        return sizeof(float) * size;
    case Pica::PipelineRegs::VertexAttributeFormat::SHORT:
        return sizeof(u16) * size;
    case Pica::PipelineRegs::VertexAttributeFormat::BYTE:
    case Pica::PipelineRegs::VertexAttributeFormat::UBYTE:
        return sizeof(u8) * size;
    }
    return 0;
}

AttribLoadFlags MakeAttribLoadFlag(Pica::PipelineRegs::VertexAttributeFormat format) {
    switch (format) {
    case Pica::PipelineRegs::VertexAttributeFormat::BYTE:
    case Pica::PipelineRegs::VertexAttributeFormat::SHORT:
        return AttribLoadFlags::Sint;
    case Pica::PipelineRegs::VertexAttributeFormat::UBYTE:
        return AttribLoadFlags::Uint;
    default:
        return AttribLoadFlags::Float;
    }
}

constexpr std::array<vk::DescriptorSetLayoutBinding, 6> BUFFER_BINDINGS = {{
    {0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eVertex},
    {1, vk::DescriptorType::eUniformBufferDynamic, 1,
     vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry},
    {2, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eFragment},
    {3, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
    {4, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
    {5, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
}};

template <u32 NumTex0>
constexpr std::array<vk::DescriptorSetLayoutBinding, 3> TEXTURE_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, NumTex0,
     vk::ShaderStageFlagBits::eFragment},                                                  // tex0
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}, // tex1
    {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}, // tex2
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 2> UTILITY_BINDINGS = {{
    {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment}, // shadow_buffer
    {1, vk::DescriptorType::eCombinedImageSampler, 1,
     vk::ShaderStageFlagBits::eFragment}, // tex_normal
}};

PipelineCache::PipelineCache(const Instance& instance_, Scheduler& scheduler_,
                             RenderManager& renderpass_cache_, DescriptorUpdateQueue& update_queue_)
    : instance{instance_}, scheduler{scheduler_}, renderpass_cache{renderpass_cache_},
      update_queue{update_queue_},
      num_worker_threads{std::max(std::thread::hardware_concurrency(), 2U) / 2},
      pipeline_workers{num_worker_threads, "Pipeline workers"},
      shader_workers{num_worker_threads, "Shader workers"},
      descriptor_heaps{
          DescriptorHeap{instance, scheduler.GetMasterSemaphore(), BUFFER_BINDINGS, 32},
          DescriptorHeap{instance, scheduler.GetMasterSemaphore(), TEXTURE_BINDINGS<1>},
          DescriptorHeap{instance, scheduler.GetMasterSemaphore(), UTILITY_BINDINGS, 32}},
      trivial_vertex_shader{
          instance, vk::ShaderStageFlagBits::eVertex,
          GLSL::GenerateTrivialVertexShader(instance.IsShaderClipDistanceSupported(), true)},
      // AstraEH: Capture per-game settings once; changing modes requires a restart.
      hybrid_tev{Settings::values.uberhar_hybrid_tev.GetValue()},
      force_tev{hybrid_tev && Settings::values.uberhar_force_tev.GetValue()},
      // CodexAstraUlt: Force-generic and ready-vertex admission are independent. The new
      // preset keeps the same worker/cache bounds while disabling optional fragment work.
      allow_specialized_fragments{
          Settings::AllowsSpecializedFragments(Settings::values.uberhar_test_mode.GetValue())},
      cpu_vertex_bridge{hybrid_tev && !force_tev &&
                        Settings::values.uberhar_cpu_vertex_bridge.GetValue()},
      // CodexAstraLocal: PicaCore selects its CPU engine at construction. Freeze
      // the same choice for this renderer; never change a shader-map policy live.
      shader_jit_enabled{(CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)) &&
                         Settings::values.use_shader_jit.GetValue()} {
    // AstraPro: Diagnostics 19 adds bounded GPU promotion and rescued-input observations.
    // AstraEH: Record effective settings so a device log identifies the tested path.
    // AstraEH Log Line: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
    LOG_INFO(
        Render_Vulkan,
        "Uberhar: hybrid_tev={} force_tev={} async_shaders={} spirv_generator={} "
        "diagnostics=22 first_ready=true compact_tev=true canonical_tev=true dynamic_fragment=true "
        "cpu_bridge={} bridge_policy=ready_only fallback_abi={} push_bytes={} "
        "runtime_lighting_luts=true runtime_lighting_enables=true runtime_light_loop=true "
        "runtime_tev_plan=true prepared_tev_operands=true phase_diagnostics=1 "
        "host_pipeline_identity=true "
        "bridge_assembly=isolated_lists_strips_fans "
        "compiler_workers={}",
        hybrid_tev, force_tev, Settings::values.async_shader_compilation.GetValue(),
        Settings::values.spirv_shader_gen.GetValue(), cpu_vertex_bridge, GLSL::DynamicTevAbiVersion,
        sizeof(TevPushConstants), num_worker_threads);
    // AstraEH: Allocate the isolated compiler only when the experiment is enabled.
    if (hybrid_tev) {
        tev_worker = std::make_unique<Common::ThreadWorker>(1, "Uberhar TEV");
        // CodexAstraUlt: Replace AstraPro's Automatic-only worker gate with shared vertex
        // eligibility, so forced-generic Combo still builds its existing bounded GPU pipelines.
        if (Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue()))
            ready_vertex_worker = std::make_unique<Common::ThreadWorker>(1, "Uberhar GPU vertex");
    }
    scheduler.RegisterOnDispatch([this] { update_queue.Flush(); });
    profile = Pica::Shader::Profile{
        .enable_accurate_mul = false,
        .has_separable_shaders = true,
        .has_clip_planes = instance.IsShaderClipDistanceSupported(),
        .has_geometry_shader = instance.UseGeometryShaders(),
        .has_custom_border_color = instance.IsCustomBorderColorSupported(),
        .has_fragment_shader_interlock = instance.IsFragmentShaderInterlockSupported(),
        .has_fragment_shader_barycentric = instance.IsFragmentShaderBarycentricSupported(),
        .has_blend_minmax_factor = false,
        .has_minus_one_to_one_range = false,
        .has_logic_op = !instance.NeedsLogicOpEmulation(),
        .vk_disable_spirv_optimizer = Settings::values.disable_spirv_optimizer.GetValue(),
        .vk_use_spirv_generator = Settings::values.spirv_shader_gen.GetValue(),
        .is_vulkan = true,
    };

    const auto& traits = instance.GetAllTraits();
    size_t i = 0;
    for (const auto& it : traits) {
        profile.vk_format_traits[i].transfer_support = it.transfer_support;
        profile.vk_format_traits[i].blit_support = it.blit_support;
        profile.vk_format_traits[i].attachment_support = it.attachment_support;
        profile.vk_format_traits[i].storage_support = it.storage_support;
        profile.vk_format_traits[i].needs_conversion = it.needs_conversion;
        profile.vk_format_traits[i].needs_emulation = it.needs_emulation;
        profile.vk_format_traits[i].usage_flags = static_cast<u32>(it.usage);
        profile.vk_format_traits[i].aspect_flags = static_cast<u32>(it.aspect);
        profile.vk_format_traits[i].native_format = static_cast<u32>(it.native);
        ++i;
    }

    // AstraEH: Freeze preparation inputs until the explicit profile setter changes them.
    tev_preparation.Configure(profile, instance.IsExtendedDynamicStateSupported());
    BuildLayout();
}

// AstraEH: Every mutable profile update also invalidates pure preparation when needed.
void PipelineCache::SetAccurateMul(bool accurate_mul) {
    profile.enable_accurate_mul = accurate_mul;
    tev_preparation.Configure(profile, instance.IsExtendedDynamicStateSupported());
}

void PipelineCache::BuildLayout() {
    std::array<vk::DescriptorSetLayout, NumRasterizerSets> descriptor_set_layouts;
    descriptor_set_layouts[0] = descriptor_heaps[0].Layout();
    descriptor_set_layouts[1] = descriptor_heaps[1].Layout();
    descriptor_set_layouts[2] = descriptor_heaps[2].Layout();

    // AstraEH: One shared layout lets specialized and interpreted pipelines alternate.
    // AstraEH: The 128-byte ABI includes runtime fragment and lighting controls.
    const vk::PushConstantRange tev_range{
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(TevPushConstants),
    };
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = NumRasterizerSets,
        .pSetLayouts = descriptor_set_layouts.data(),
        .pushConstantRangeCount = hybrid_tev ? 1U : 0U,
        .pPushConstantRanges = hybrid_tev ? &tev_range : nullptr,
    };
    pipeline_layout = instance.GetDevice().createPipelineLayoutUnique(layout_info);
}

PipelineCache::~PipelineCache() {
    scheduler.WaitWorker();
    pipeline_workers.WaitForRequests();
    shader_workers.WaitForRequests();
    // AstraEH: Fallback jobs also retain shader, layout and cache objects until completed.
    if (tev_worker) {
        tev_worker->WaitForRequests();
    }
    // AstraPro: Candidates refer to modules owned by both fragment and disk caches.
    if (ready_vertex_worker)
        ready_vertex_worker->WaitForRequests();
    // AstraEH: Compiler/scheduler workers are drained before reading final counters.
    ReportUberharStats();
    SaveDriverPipelineDiskCache();
}

void PipelineCache::LoadCache(const std::atomic_bool& stop_loading,
                              const VideoCore::DiskResourceLoadCallback& callback) {
    // AstraPro: A repeated frontend load may replace disk-cache module owners.
    // Drain optional users before either the driver cache or shader caches change.
    // AstraEH: Generic pipelines also borrow disk-cache modules in Native/Compute profiles.
    if (hybrid_tev && curr_disk_cache)
        ClearTevFallbacks();
    LoadDriverPipelineDiskCache(stop_loading, callback);
    LoadDiskCache(stop_loading, callback);
}

void PipelineCache::SwitchCache(u64 title_id, const std::atomic_bool& stop_loading,
                                const VideoCore::DiskResourceLoadCallback& callback) {
    if (GetProgramID() == title_id) {
        LOG_DEBUG(Render_Vulkan,
                  "Skipping pipeline cache switch - already using cache for title_id={:016X}",
                  title_id);
        return;
    }

    // Make sure we have a valid pipeline cache before switching
    if (!driver_pipeline_cache) {
        vk::PipelineCacheCreateInfo cache_info{};
        try {
            driver_pipeline_cache = instance.GetDevice().createPipelineCacheUnique(cache_info);
        } catch (const vk::SystemError& err) {
            LOG_ERROR(Render_Vulkan, "Failed to create pipeline cache: {}", err.what());
            return;
        }
    }

    LOG_INFO(Render_Vulkan, "Switching pipeline cache to title_id={:016X}", title_id);

    // AstraEH: Fallback pipelines reference the current title's VS/GS objects and driver
    // cache. Drain both GPU commands and compiler jobs before replacing either.
    if (hybrid_tev) {
        ClearTevFallbacks();
    }

    // Save current driver cache, update program ID and load the new driver cache
    SaveDriverPipelineDiskCache();
    SetProgramID(title_id);
    LoadDriverPipelineDiskCache(stop_loading, nullptr);

    // Switch the disk shader cache after driver cache is switched
    SwitchDiskCache(title_id, stop_loading, callback);
}

void PipelineCache::LoadDriverPipelineDiskCache(
    const std::atomic_bool& stop_loading, const VideoCore::DiskResourceLoadCallback& callback) {
    vk::PipelineCacheCreateInfo cache_info{};

    // AstraEH: Inventory the actual title namespaces before EnsureDirectories, loading
    // or generated-module writes. Non-Custom profiles bypass specialized disk records.
    const auto cache_dir = GetPipelineCacheDir();
    const u64 program_id = GetProgramID();
    const auto cache_file_path = fmt::format("{}{:016X}-{:X}{:X}.bin", cache_dir, program_id,
                                             instance.GetVendorID(), instance.GetDeviceID());
    const auto inventory_start = std::chrono::steady_clock::now();
    // AstraEH: Android's user-relative FileUtil paths require the same mapping as
    // actual cache I/O. Unsupported virtual filesystems stay Unknown, never empty.
    const auto inventory_path = [](const std::string& path) -> std::string {
#if defined(HAVE_LIBRETRO_VFS)
        return {};
#elif defined(ANDROID)
        return AndroidUtils::CanUseRawFS() ? AndroidUtils::TranslateFilePath(path) : "";
#else
        return path;
#endif
    };
    cache_start = UberharCacheDiagnostics::Inspect(
        inventory_path(cache_dir), inventory_path(cache_file_path),
        inventory_path(GetTransferableDir()), fmt::format("{:016X}", program_id),
        Settings::values.use_disk_shader_cache.GetValue(),
        Settings::values.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    driver_cache_load = "not_attempted";
    // AstraEH: Existing module totals are renderer-lifetime; reuse classification
    // uses a per-title-load baseline after the preceding compiler work is drained.
    cache_start_hits = generic_module_hits.load();
    cache_start_misses = generic_module_misses.load();
    cache_session = Common::UberharActivity::session.load();
    // AstraEH Log Line: One bounded, read-only inventory per load/title switch; no paths or data.
    LOG_INFO(Render_Vulkan,
             "Uberhar cache start: schema=1 session={} title={:016X} scope=application_vulkan "
             "state={} generic_files={} generic_bytes={} generic_complete={} generic_capped={} "
             "driver_files={} driver_bytes={} driver_complete={} specialized_files={} "
             "specialized_bytes={} specialized_complete={} specialized_active={} "
             "inventory_ms={:.3f} compatible_contents=unverified driver_internal=unknown",
             cache_session, program_id, cache_start.State(), cache_start.generic.files,
             cache_start.generic.bytes, cache_start.generic.complete, cache_start.generic.capped,
             cache_start.driver.files, cache_start.driver.bytes, cache_start.driver.complete,
             cache_start.specialized.files, cache_start.specialized.bytes,
             cache_start.specialized.complete, cache_start.specialized_active,
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                       inventory_start)
                 .count());

    if (callback) {
        callback(VideoCore::LoadCallbackStage::Build, 0, 1, "Driver Pipeline Cache");
    }

    auto load_cache = [this, &cache_info, &callback](bool allow_fallback) {
        const vk::Device device = instance.GetDevice();
        try {
            driver_pipeline_cache = device.createPipelineCacheUnique(cache_info);
        } catch (const vk::SystemError& err) {
            driver_cache_load = "create_failed"; // AstraEH: Preserve failure after fallback.
            LOG_ERROR(Render_Vulkan, "Failed to create pipeline cache: {}", err.what());
            if (allow_fallback) {
                // Fall back to empty cache
                cache_info.initialDataSize = 0;
                cache_info.pInitialData = nullptr;
                try {
                    driver_pipeline_cache = device.createPipelineCacheUnique(cache_info);
                    driver_cache_load = "rejected_empty_fallback"; // AstraEH: Not warm reuse.
                } catch (const vk::SystemError& err) {
                    LOG_ERROR(Render_Vulkan, "Failed to create fallback pipeline cache: {}",
                              err.what());
                }
            }
        }
        if (callback) {
            callback(VideoCore::LoadCallbackStage::Build, 1, 1, "Driver Pipeline Cache");
        }
    };

    // Try to load existing pipeline cache if disk cache is enabled and directories exist
    if (!Settings::values.use_disk_shader_cache) {
        driver_cache_load = "disabled"; // AstraEH: Deliberate bypass is not a cold-cache claim.
        load_cache(false);
        return;
    }
    if (!EnsureDirectories()) {
        driver_cache_load = "directory_error";
        load_cache(false);
        return;
    }

    // Try to load existing pipeline cache for this game/device combination
    std::vector<u8> cache_data;
    FileUtil::IOFile cache_file{cache_file_path, "rb"};

    if (!cache_file.IsOpen()) {
        driver_cache_load = cache_start.driver.complete && cache_start.driver.files == 0
                                ? "missing"
                                : "open_failed";
        LOG_INFO(Render_Vulkan, "No pipeline cache found for title_id={:016X}", program_id);
        load_cache(false);
        return;
    }

    const u64 cache_file_size = cache_file.GetSize();
    cache_data.resize(cache_file_size);

    if (cache_file.ReadBytes(cache_data.data(), cache_file_size) != cache_file_size) {
        driver_cache_load = "read_failed";
        LOG_ERROR(Render_Vulkan, "Error reading pipeline cache");
        load_cache(false);
        return;
    }

    if (!IsCacheValid(cache_data)) {
        driver_cache_load = "invalid";
        LOG_WARNING(Render_Vulkan, "Pipeline cache invalid, removing");
        cache_file.Close();
        FileUtil::Delete(cache_file_path);
        load_cache(false);
        return;
    }

    LOG_INFO(Render_Vulkan, "Loading pipeline cache for title_id={:016X} with size {} KB",
             program_id, cache_file_size / 1024);

    cache_info.initialDataSize = cache_file_size;
    cache_info.pInitialData = cache_data.data();
    driver_cache_load = "provided_to_driver"; // AstraEH: Vulkan exposes no cache-hit guarantee.
    load_cache(true);
}

void PipelineCache::SaveDriverPipelineDiskCache() {
    // Save Vulkan pipeline cache
    if (!Settings::values.use_disk_shader_cache || !driver_pipeline_cache) {
        return;
    }

    const auto cache_dir = GetPipelineCacheDir();
    const u32 vendor_id = instance.GetVendorID();
    const u32 device_id = instance.GetDeviceID();
    const u64 program_id = GetProgramID();
    // Include both device info and program id in cache path to handle both GPU changes and
    // different games
    const auto cache_file_path =
        fmt::format("{}{:016X}-{:X}{:X}.bin", cache_dir, program_id, vendor_id, device_id);

    FileUtil::IOFile cache_file{cache_file_path, "wb"};
    if (!cache_file.IsOpen()) {
        LOG_ERROR(Render_Vulkan, "Unable to open pipeline cache for writing");
        return;
    }

    const vk::Device device = instance.GetDevice();
    const auto cache_data = device.getPipelineCacheData(*driver_pipeline_cache);
    if (cache_file.WriteBytes(cache_data.data(), cache_data.size()) != cache_data.size()) {
        LOG_ERROR(Render_Vulkan, "Error during pipeline cache write");
        return;
    }
}

void PipelineCache::LoadDiskCache(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {

    disk_caches.clear();
    curr_disk_cache =
        disk_caches.emplace_back(std::make_shared<ShaderDiskCache>(*this, GetProgramID()));

    curr_disk_cache->Init(stop_loading, callback);
}

void PipelineCache::SwitchDiskCache(u64 title_id, const std::atomic_bool& stop_loading,
                                    const VideoCore::DiskResourceLoadCallback& callback) {
    // NOTE: curr_disk_cache can be null if emulation restarted without calling
    // LoadDefaultDiskResources

    // Check if the current cache is for the specified TID.
    if (curr_disk_cache && curr_disk_cache->GetProgramID() == title_id) {
        return;
    }

    // Search for an existing manager
    size_t new_pos = 0;
    for (new_pos = 0; new_pos < disk_caches.size(); new_pos++) {
        if (disk_caches[new_pos]->GetProgramID() == title_id) {
            break;
        }
    }
    // Manager does not exist, create it and append to the end
    if (new_pos >= disk_caches.size()) {
        new_pos = disk_caches.size();
        auto& new_manager =
            disk_caches.emplace_back(std::make_shared<ShaderDiskCache>(*this, title_id));

        new_manager->Init(stop_loading, callback);
    }

    auto is_applet = [](u64 tid) {
        constexpr u32 APPLET_TID_HIGH = 0x00040030;
        return static_cast<u32>(tid >> 32) == APPLET_TID_HIGH;
    };

    bool prev_applet = curr_disk_cache ? is_applet(curr_disk_cache->GetProgramID()) : false;
    bool new_applet = is_applet(disk_caches[new_pos]->GetProgramID());
    curr_disk_cache = disk_caches[new_pos];

    if (prev_applet) {
        // If we came from an applet, clean up all other applets
        for (auto it = disk_caches.begin(); it != disk_caches.end();) {
            if (it == disk_caches.begin() || *it == curr_disk_cache ||
                !is_applet((*it)->GetProgramID())) {
                it++;
                continue;
            }
            it = disk_caches.erase(it);
        }
    }
    if (!new_applet) {
        // If we are going into a non-applet, clean up everything
        for (auto it = disk_caches.begin(); it != disk_caches.end();) {
            if (it == disk_caches.begin() || *it == curr_disk_cache) {
                it++;
                continue;
            }
            it = disk_caches.erase(it);
        }
    }
}

bool PipelineCache::BindPipeline(PipelineInfo& info, bool wait_built,
                                 GraphicsPipeline* ready_cpu_fallback, bool allow_tev_build,
                                 GraphicsPipeline* ready_gpu_vertex) {
    MICROPROFILE_SCOPE(Vulkan_Bind);

    for (u32 i = 0; i < MAX_SHADER_STAGES; i++) {
        info.state.shader_ids[i] = shader_hashes[i];
    }

    // AstraPro: Validate the complete execution identity immediately before use.
    // On failure this call has queued no draw; its caller returns to CPU rendering.
    if (ready_gpu_vertex && (!ready_gpu_vertex->MatchesExecution(info, HostShaderIds(current_shaders)) ||
                            !ReadyVertexPolicy::CanSelect(
            ready_gpu_vertex->IsDone(), ready_gpu_vertex->HasFailed(),
            info.state.ExecutionHash(instance.IsExtendedDynamicStateSupported(),
                                     HostShaderIds(current_shaders)), ready_gpu_vertex->Key()))) {
        ++ready_vertex_key_mismatches;
        return false;
    }
    if (ready_gpu_vertex)
        ++ready_vertex_selected;
    // AstraEH: Count draw attempts and choose between specialization and a TEV fallback.
    ++draw_requests;
    // AstraEH: Validate the actual software draw's complete key before using the
    // prepared handle. On a mismatch, retain the accurate existing blocking path.
    GraphicsPipeline* pipeline = ready_gpu_vertex;
    if (ready_cpu_fallback && hybrid_tev && tev_supported && tev_family_config) {
        const auto family = GLSL::MakeDynamicTevFamilyConfig(*tev_family_config, profile).Hash();
        const auto shader = tev_shaders.find(family);
        auto stages = current_shaders;
        stages[ProgramType::FS] = shader == tev_shaders.end() ? nullptr : shader->second.get();
        stages[ProgramType::GS] = nullptr;
        const u64 expected_key = info.state.ExecutionHash(
            instance.IsExtendedDynamicStateSupported(), HostShaderIds(stages));
        if (shader != tev_shaders.end() && ready_cpu_fallback->IsDone() &&
            !ready_cpu_fallback->HasFailed() && ready_cpu_fallback->Key() == expected_key) {
            pipeline = ready_cpu_fallback;
            ++cpu_bridge_draws;
        } else if (++cpu_bridge_mismatches <= 4) {
            // AstraEH Log Line: bounded bridge-key mismatch; never bind incompatible state.
            LOG_WARNING(Render_Vulkan,
                        "Uberhar CPU bridge mismatch: expected={:016X} ready={:016X}", expected_key,
                        ready_cpu_fallback->Key());
        }
    }
    const bool cpu_bridge = pipeline != nullptr && !ready_gpu_vertex;
    bool virtual_generic = false;
    // AstraPro: Specialized promotion is an optimization, not unsupported-state
    // recovery and not a dynamic TEV draw. Do not upload generic push constants
    // or label it generic merely because the CPU fallback has a virtual config.
    const bool specialized_gpu = ready_gpu_vertex && virtual_fs_config &&
        PreferReadySpecializedFragment(tev_user);
    if (specialized_gpu) {
        ++virtual_specialized_gpu_draws;
    } else if (virtual_fs_config) {
        // AstraEH: Start only the requested generic pipeline, never a speculative rival.
        // Until the ready bank is complete this wait is real, separately measured work.
        // AstraPro: A promoted draw uses its hardware-VS pipeline, never a
        // trivial-VS CPU pipeline with the hardware vertex-buffer layout.
        auto* generic = ready_gpu_vertex ? ready_gpu_vertex : GetTevFallback(info, true);
        if (generic && !generic->IsDone()) {
            const auto activity_start = Common::UberharActivity::Capture();
            const auto start = std::chrono::steady_clock::now();
            generic->WaitDone();
            const auto ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                 std::chrono::steady_clock::now() - start)
                                                 .count());
            ++virtual_waits;
            virtual_wait_ns += ns;
            virtual_max_wait_ns = std::max(virtual_max_wait_ns, ns);
            const auto activity_end = Common::UberharActivity::Capture();
            const auto phase = Common::UberharActivity::Between(activity_start, activity_end);
            auto& bucket = phase_waits[static_cast<std::size_t>(phase)];
            ++bucket.count;
            bucket.ns += ns;
            bucket.maximum = std::max(bucket.maximum, ns);
            if (ns >= 16'666'667 && phase_wait_details < 64) {
                ++phase_wait_details;
                // AstraEH Log Line: First 64 significant waits; all waits retain phase totals.
                LOG_INFO(Render_Vulkan,
                         "Uberhar generic wait: session={} phase={} wait_ms={:.3f} "
                         "reads={} requested_bytes={} submissions={} limit=64",
                         activity_start.run, Common::UberharActivity::Name(phase), ns / 1e6,
                         activity_end.reads - activity_start.reads,
                         activity_end.bytes - activity_start.bytes,
                         activity_end.presents - activity_start.presents);
            }
        }
        if (generic && !generic->HasFailed()) {
            pipeline = generic;
            virtual_generic = true;
            ++virtual_generic_draws;
            // AstraEH: Draw-weighted coverage only, not fragment/pixel-weighted savings.
            ++tev_loop_histogram[(tev_constants.buffer_mask >> 8) & 7U];
            ++tev_active_histogram[std::popcount((tev_constants.buffer_mask >> 16) & 63U)];
        } else {
            // AstraEH: Recover before submission, so a failed/unsupported generic never drops a
            // draw.
            ++virtual_recovery_draws;
            ++virtual_recovery_reasons[generic ? 8 : 9];
            auto specialized = curr_disk_cache->UseFragmentShader(*virtual_fs_config, tev_user);
            if (!specialized)
                throw VideoCore::ShaderRecoveryError{};
            current_shaders[ProgramType::FS] = specialized->second;
            shader_hashes[ProgramType::FS] = specialized->first;
            info.state.shader_ids[ProgramType::FS] = specialized->first;
        }
    } else if (Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom) {
        ++virtual_recovery_draws;
        const auto reason = static_cast<std::size_t>(tev_support_reason);
        ++virtual_recovery_reasons[hybrid_tev && reason > 0 && reason < 8 ? reason : 10];
    }
    if (!pipeline) {
        pipeline = curr_disk_cache->GetPipeline(info);
    }
    const bool pending = !pipeline->IsDone();
    specialized_pending += pending;
    bool using_fallback = cpu_bridge || virtual_generic;
    GraphicsPipeline* alternative = nullptr;
    bool alternative_was_pending = false;
    if (hybrid_tev && !cpu_bridge && !virtual_generic && !ready_gpu_vertex) {
        // AstraEH: Admit a bounded fallback before queuing specialization. The scheduler
        // can use either completed result; neither path is allowed to omit this draw.
        if (force_tev || (pending && allow_tev_build)) {
            alternative = GetTevFallback(info);
            if (alternative) {
                alternative_was_pending = !alternative->IsDone();
                fallback_warming += alternative_was_pending;
            } else {
                ++fallback_unavailable;
            }
        }
        if (pending) {
            // AstraEH: Even cache-only driver calls can block on compiler locks. In hybrid
            // mode all pipeline creation belongs on workers, including these probes.
            pipeline->TryBuild(true, true);
        }
        // AstraEH: Keep specialization available even in forced mode so a failed
        // experimental build can recover instead of binding an invalid handle.
    } else if (!cpu_bridge && pending && !pipeline->TryBuild(wait_built)) {
        ++skipped_draws;
        return false;
    }

    // AstraEH: Bounded progress snapshots survive a copied/truncated log without requiring
    // an orderly shutdown. Check the clock once per 4096 draws to keep steady-state cost low.
    if (hybrid_tev && (draw_requests & 4095) == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_progress) {
            ReportUberharStats("progress");
            next_progress = now + std::chrono::seconds{5};
        }
    }

    const bool is_dirty = scheduler.IsStateDirty(StateFlags::Pipeline);
    // AstraEH: Capture invalidation in enqueue order; utility/presentation writes
    // may execute after this thread has already prepared later draw constants.
    const bool constants_dirty = is_dirty || scheduler.IsStateDirty(StateFlags::FragmentConstants);
    // AstraEH: Copy TEV registers into the queued command; later draws may change them.
    scheduler.Record([this, is_dirty, constants_dirty, pipeline, using_fallback, alternative,
                      draw_id = draw_requests, alternative_was_pending, constants = tev_constants,
                      current_dynamic = current_info.dynamic_info, dynamic = info.dynamic_info,
                      descriptor_sets = bound_descriptor_sets, offsets = offsets,
                      current_rasterization = current_info.state.rasterization,
                      current_depth_stencil = current_info.state.depth_stencil,
                      rasterization = info.state.rasterization,
                      depth_stencil = info.state.depth_stencil](vk::CommandBuffer cmdbuf) {
        if (dynamic.viewport != current_dynamic.viewport || is_dirty) {
            const vk::Viewport vk_viewport = {
                .x = static_cast<f32>(dynamic.viewport.left),
                .y = static_cast<f32>(dynamic.viewport.top),
                .width = static_cast<f32>(dynamic.viewport.GetWidth()),
                .height = static_cast<f32>(dynamic.viewport.GetHeight()),
                .minDepth = 0.f,
                .maxDepth = 1.f,
            };
            cmdbuf.setViewport(0, vk_viewport);
        }

        if (dynamic.scissor != current_dynamic.scissor || is_dirty) {
            const vk::Rect2D scissor = {
                .offset{
                    .x = static_cast<s32>(dynamic.scissor.left),
                    .y = static_cast<s32>(dynamic.scissor.bottom),
                },
                .extent{
                    .width = dynamic.scissor.GetWidth(),
                    .height = dynamic.scissor.GetHeight(),
                },
            };
            cmdbuf.setScissor(0, scissor);
        }

        if (dynamic.stencil_compare_mask != current_dynamic.stencil_compare_mask || is_dirty) {
            cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                         dynamic.stencil_compare_mask);
        }

        if (dynamic.stencil_write_mask != current_dynamic.stencil_write_mask || is_dirty) {
            cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                       dynamic.stencil_write_mask);
        }

        if (dynamic.stencil_reference != current_dynamic.stencil_reference || is_dirty) {
            cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                       dynamic.stencil_reference);
        }

        if (dynamic.blend_color != current_dynamic.blend_color || is_dirty) {
            const Common::Vec4f color = PicaToVK::ColorRGBA8(dynamic.blend_color);
            cmdbuf.setBlendConstants(color.AsArray());
        }

        if (instance.IsExtendedDynamicStateSupported()) {
            const bool needs_flip =
                rasterization.flip_viewport != current_rasterization.flip_viewport;
            if (rasterization.cull_mode != current_rasterization.cull_mode || needs_flip ||
                is_dirty) {
                cmdbuf.setCullModeEXT(
                    PicaToVK::CullMode(rasterization.cull_mode, rasterization.flip_viewport));
                cmdbuf.setFrontFaceEXT(PicaToVK::FrontFace(rasterization.cull_mode));
            }

            if (depth_stencil.depth_compare_op != current_depth_stencil.depth_compare_op ||
                is_dirty) {
                cmdbuf.setDepthCompareOpEXT(PicaToVK::CompareFunc(depth_stencil.depth_compare_op));
            }

            if (depth_stencil.depth_test_enable != current_depth_stencil.depth_test_enable ||
                is_dirty) {
                cmdbuf.setDepthTestEnableEXT(depth_stencil.depth_test_enable);
            }

            if (depth_stencil.depth_write_enable != current_depth_stencil.depth_write_enable ||
                is_dirty) {
                cmdbuf.setDepthWriteEnableEXT(depth_stencil.depth_write_enable);
            }

            if (rasterization.topology != current_rasterization.topology || is_dirty) {
                cmdbuf.setPrimitiveTopologyEXT(PicaToVK::PrimitiveTopology(rasterization.topology));
            }

            if (depth_stencil.stencil_test_enable != current_depth_stencil.stencil_test_enable ||
                is_dirty) {
                cmdbuf.setStencilTestEnableEXT(depth_stencil.stencil_test_enable);
            }

            if (depth_stencil.stencil_fail_op != current_depth_stencil.stencil_fail_op ||
                depth_stencil.stencil_pass_op != current_depth_stencil.stencil_pass_op ||
                depth_stencil.stencil_depth_fail_op !=
                    current_depth_stencil.stencil_depth_fail_op ||
                depth_stencil.stencil_compare_op != current_depth_stencil.stencil_compare_op ||
                is_dirty) {
                cmdbuf.setStencilOpEXT(vk::StencilFaceFlagBits::eFrontAndBack,
                                       PicaToVK::StencilOp(depth_stencil.stencil_fail_op),
                                       PicaToVK::StencilOp(depth_stencil.stencil_pass_op),
                                       PicaToVK::StencilOp(depth_stencil.stencil_depth_fail_op),
                                       PicaToVK::CompareFunc(depth_stencil.stencil_compare_op));
            }
        }

        // AstraEH: Choose on the command worker, where readiness actually matters.
        // Rechecking here also recovers fallbacks completed after the draw was enqueued.
        const bool prefer_fallback = force_tev && alternative;
        const bool must_wait = PipelineWaitRequired(*pipeline, alternative, prefer_fallback);
        const auto wait_start =
            must_wait ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const u32 pending_stages = must_wait ? pipeline->PendingShaderMask() : 0;
        const u32 initial_phase = must_wait ? pipeline->BuildPhase() : 3;
        const u32 alternative_phase = must_wait && alternative ? alternative->BuildPhase() : 3;
        const u32 alternative_stages =
            must_wait && alternative ? alternative->PendingShaderMask() : 0;
        if (alternative && must_wait && !prefer_fallback) {
            first_ready_waits.fetch_add(1, std::memory_order::relaxed);
        }
        auto* selected =
            SelectUsablePipeline(pipeline_completion, *pipeline, alternative, prefer_fallback);
        const bool selected_fallback = using_fallback || selected == alternative;
        if (selected == alternative && alternative_was_pending && !prefer_fallback) {
            late_fallback_draws.fetch_add(1, std::memory_order::relaxed);
        }
        if (must_wait) {
            const u64 elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - wait_start)
                                       .count();
            // AstraEH: Retain the worst events even when the early detail budget
            // is exhausted. Keys are process-local; timestamp is renderer-relative.
            wait_diagnostics.Record({
                .elapsed_ns = elapsed_ns,
                .start_ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                 wait_start - diagnostics_start)
                                                 .count()),
                .draw = draw_id,
                .wait_key = pipeline->Key(),
                .chosen_key = selected->Key(),
                .stages = pending_stages,
                .phase = initial_phase,
                .alternative_phase = alternative_phase,
                .alternative_stages = alternative_stages,
                .topology = static_cast<u32>(rasterization.topology.Value()),
                .alternative = alternative != nullptr,
                .fallback = selected_fallback,
            });
            pipeline_waits.fetch_add(1, std::memory_order::relaxed);
            pipeline_wait_ns.fetch_add(elapsed_ns, std::memory_order::relaxed);
            pipeline_wait_max_ns.store(
                std::max(pipeline_wait_max_ns.load(std::memory_order::relaxed), elapsed_ns),
                std::memory_order::relaxed);
            if (prefer_fallback) {
                fallback_wait_ns.fetch_add(elapsed_ns, std::memory_order::relaxed);
            }
            if (elapsed_ns >= 50000000 &&
                slow_pipeline_waits.fetch_add(1, std::memory_order::relaxed) < 20) {
                // AstraEH Log Line: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
                LOG_INFO(Render_Vulkan,
                         "Uberhar slow pipeline wait: path={} wait_ms={:.3f} wait_key={:016X} "
                         "chosen_key={:016X} pending_stages={} phase={} alternative={} "
                         "alternative_phase={} alternative_stages={}",
                         selected_fallback ? (prefer_fallback ? "forced_fallback" : "fallback")
                                           : "specialized",
                         elapsed_ns / 1000000.0, pipeline->Key(), selected->Key(), pending_stages,
                         initial_phase, alternative != nullptr, alternative_phase,
                         alternative_stages);
            }
        }
        if (selected_fallback) {
            fallback_draws.fetch_add(1, std::memory_order::relaxed);
            // AstraEH: Distinguish reusable fallback pipelines from expensive
            // speculative builds that never contribute to rendering.
            selected->RecordFallbackUse();
        }
        // AstraEH: Track the actual scheduler-selected handle, not the originally requested
        // handle. Otherwise a fallback win could leave the next specialized draw misbound.
        if (is_dirty || bound_pipeline != selected) {
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, selected->Handle());
            bound_pipeline = selected;
        }

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0,
                                  descriptor_sets, offsets);
        // AstraEH: Invalidate even when specialization wins this command. Otherwise
        // the following generic draw could reuse values overwritten by utility work.
        if (constants_dirty) {
            tev_push_constants.Invalidate();
        }
        // AstraEH: Compare after actual pipeline selection, against the last executed
        // generic upload. All rasterizer pipelines share this immutable layout/range.
        if (selected_fallback) {
            tev_push_constants.UploadIfChanged(constants, [&](const auto& value) {
                cmdbuf.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0,
                                     sizeof(value), &value);
            });
        }
    });

    current_info = info;
    scheduler.MarkStateNonDirty(StateFlags::Pipeline | StateFlags::DescriptorSets |
                                StateFlags::FragmentConstants);

    return true;
}

ExtraVSConfig PipelineCache::CalcExtraConfig(const PicaVSConfig& config) {
    auto res = ExtraVSConfig();

    // Enable the geometry-shader only if we are actually doing per-fragment lighting
    // and care about proper quaternions. Otherwise just use standard vertex+fragment shaders.
    // We also don't need the geometry shader if we have the barycentric extension.
    const bool use_geometry_shader = instance.UseGeometryShaders() &&
                                     !config.state.lighting_disable &&
                                     !instance.IsFragmentShaderBarycentricSupported();

    res.use_clip_planes = instance.IsShaderClipDistanceSupported();
    res.use_geometry_shader = use_geometry_shader;
    res.sanitize_mul = profile.enable_accurate_mul;
    res.separable_shader = true;
    // CodexAstraLocal: Only Combo's immutable ready-only owner mixes GPU shaders
    // with CPU JIT output. Custom/Native/OpenGL keep the prior dot generation.
    res.precise_jit_dot = ready_vertex_worker != nullptr && shader_jit_enabled;
    res.load_flags.fill(AttribLoadFlags::Float);

    for (u32 i = 0; i < config.state.used_input_vertex_attributes; i++) {
        const auto& attr = config.state.input_vertex_attributes[i];
        const u32 location = attr.location;
        const Pica::PipelineRegs::VertexAttributeFormat type =
            static_cast<Pica::PipelineRegs::VertexAttributeFormat>(attr.type);
        const FormatTraits& traits = instance.GetTraits(type, attr.size);
        AttribLoadFlags& flags = res.load_flags[location];

        if (traits.needs_conversion) {
            flags = MakeAttribLoadFlag(type);
        }
        if (traits.needs_emulation) {
            // CodexAstraUlt: The wider Vulkan format reads an extra component;
            // replace inherited zero padding with the missing PICA component W=1.
            flags |= AttribLoadFlags::PadWOne;
        }
    }

    return res;
}

std::optional<VertexCapture::BindingState> PipelineCache::CaptureVertexBinding(
    const Pica::RegsInternal& regs, Pica::ShaderSetup& setup, const PipelineInfo& info,
    u64 pipeline_key) noexcept {
    // CodexAstraLocal: Mirror the already selected VS input configuration using
    // the canonical layout; preserve actual final dynamic UBO offsets by value.
    const auto& layout = info.state.vertex_layout;
    // CodexAstraLocal: The existing selected path validated these invariants,
    // but diagnostics fail closed before any fixed-array copy or trait lookup.
    if (!curr_disk_cache || layout.attribute_count > layout.attributes.size() ||
        layout.binding_count > layout.bindings.size() ||
        regs.rasterizer.vs_output_total > 7 || (regs.vs.output_mask & ~0xffffU))
        return std::nullopt;
    for (u32 i = 0; i < layout.attribute_count; ++i) {
        const auto& attr = layout.attributes[i];
        if (attr.location.Value() >= 16 || static_cast<u32>(attr.type.Value()) >= 4 ||
            attr.size.Value() < 1 || attr.size.Value() > 4 ||
            attr.binding.Value() >= layout.binding_count)
            return std::nullopt;
    }
    // CodexAstraLocal: These constructors and lookups use fixed arrays/hash
    // arithmetic only; the current shader fixup function is empty. No compile,
    // allocation, Vulkan request or mutable descriptor selection occurs here.
    PicaVSConfig config{regs, setup};
    config.state.used_input_vertex_attributes = layout.attribute_count;
    for (u32 i = 0; i < layout.attribute_count; ++i) {
        const auto& source = layout.attributes[i];
        auto& target = config.state.input_vertex_attributes[i];
        target.location = source.location;
        target.type = static_cast<u8>(source.type.Value());
        target.size = source.size;
    }
    return VertexCapture::BindingState{info, CalcExtraConfig(config), profile, offsets, shader_hashes, pipeline_key,
        curr_disk_cache->CaptureVertexSourceHash(current_shaders[ProgramType::VS]),
        virtual_fs_config && PreferReadySpecializedFragment(tev_user)};
}

// AstraPro: Readiness checks are atomic acquire operations. Do not inspect module
// handles before IsDone has published construction; absent GS is a valid stage.
bool PipelineCache::ReadyVertexShaders() const {
    const auto* vertex = current_shaders[ProgramType::VS];
    const auto* geometry = current_shaders[ProgramType::GS];
    return vertex && vertex != &trivial_vertex_shader && vertex->IsDone() &&
           !vertex->HasFailed() && vertex->Handle() &&
           (!geometry || (geometry->IsDone() && !geometry->HasFailed() && geometry->Handle()));
}

// CodexAstraUlt: Replace repeated AstraPro fragment preferences with a single immutable
// preset gate. Generic-only Combo never asks the optional fragment cache to warm or select.
bool PipelineCache::PreferReadySpecializedFragment(const Pica::Shader::UserConfig& user) const {
    return allow_specialized_fragments &&
           ReadyFragmentPolicy::PreferSpecialized(force_tev, user.IsCacheable());
}

// AstraPro: Speculate only after every shader dependency is usable. One serial
// build may be in flight; it uses a distinct 256-entry cache and never consumes
// mandatory CPU-generic pipeline slots. Failure/pending/caps retain CPU rendering.
bool PipelineCache::ReadyGpuFragmentPreflight(const Pica::RegsInternal& regs,
                                               const Pica::Shader::UserConfig& user) {
    if (!ready_vertex_worker || !curr_disk_cache)
        return false;
    if (!PreferReadySpecializedFragment(user))
        return true;
    const FSConfig config{regs};
    // AstraPro: Preserve all generic support guards. Unsupported states already
    // use the mandatory specialized path; do not give them speculative behavior.
    if (GLSL::CheckDynamicTevSupport(config, user) != GLSL::DynamicTevSupport::Ready)
        return true;
    if (curr_disk_cache->UseReadyFragmentShader(config, user))
        return true;
    ++ready_fragment_preflight_deferred;
    return false; // No uploads or framebuffer ownership changes occurred.
}

GraphicsPipeline* PipelineCache::PrepareReadyGpuVertex(const PipelineInfo& info) {
    ++ready_vertex_requests;
    if (!ready_vertex_worker || !ReadyVertexShaders()) {
        ++ready_vertex_dependency_misses;
        return nullptr;
    }
    auto stages = current_shaders;
    u64 fragment_id = shader_hashes[ProgramType::FS];
    const bool specialize = virtual_fs_config && PreferReadySpecializedFragment(tev_user);
    if (specialize) {
        const auto fragment = curr_disk_cache->UseReadyFragmentShader(*virtual_fs_config, tev_user);
        if (!fragment) {
            ++ready_vertex_dependency_misses;
            return nullptr;
        }
        fragment_id = fragment->first;
        stages[ProgramType::FS] = fragment->second;
    } else if (virtual_fs_config) {
        fragment_id = GLSL::MakeDynamicTevFamilyConfig(*tev_family_config, profile).Hash();
        const auto shader = tev_shaders.find(fragment_id);
        if (shader == tev_shaders.end()) {
            ++ready_vertex_dependency_misses;
            return nullptr;
        }
        stages[ProgramType::FS] = shader->second.get();
    }
    auto* fragment = stages[ProgramType::FS];
    if (!fragment || !fragment->IsDone() || fragment->HasFailed() || !fragment->Handle()) {
        ++ready_vertex_dependency_misses;
        return nullptr;
    }
    PipelineInfo candidate = info;
    candidate.state.shader_ids = shader_hashes;
    candidate.state.shader_ids[ProgramType::FS] = fragment_id;
    const u64 key = candidate.state.ExecutionHash(instance.IsExtendedDynamicStateSupported(),
                                                  HostShaderIds(stages));
    if (auto it = ready_vertex_pipelines.find(key); it != ready_vertex_pipelines.end()) {
        auto* pipeline = it->second.get();
        if (ReadyVertexPolicy::CanSelect(pipeline->IsDone(), pipeline->HasFailed(), key,
                                         pipeline->Key()) &&
            pipeline->MatchesExecution(candidate, HostShaderIds(stages))) {
            current_shaders = stages;
            shader_hashes[ProgramType::FS] = fragment_id;
            return pipeline;
        }
        if (pipeline->HasFailed())
            ++ready_vertex_failed;
        else
            ++ready_vertex_deferred;
        return nullptr;
    }
    const bool pending = warming_ready_vertex && !warming_ready_vertex->IsDone();
    if (!ReadyVertexPolicy::CanQueue(ready_vertex_pipelines.size(), pending)) {
        if (pending)
            ++ready_vertex_deferred;
        else
            ++ready_vertex_capped;
        return nullptr;
    }
    auto pipeline = std::make_unique<GraphicsPipeline>(
        instance, renderpass_cache, candidate, *driver_pipeline_cache, *pipeline_layout,
        stages, ready_vertex_worker.get(),
        PipelineBuildOptions{&pipeline_completion, &ready_vertex_build_stats,
                             virtual_fs_config.has_value() && !specialize, true});
    auto* prepared = pipeline.get();
    ready_vertex_pipelines.emplace(key, std::move(pipeline));
    warming_ready_vertex = prepared;
    // AstraPro: Build errors publish failed completion. CPU never waits on this
    // worker; keep even failed objects stable until drained title reset/shutdown.
    // AstraPro: Capture immutable stage/state identities for this bounded optional
    // cache (at most 256 records per title). No worker reads mutable PICA state.
    const auto shader_ids = candidate.state.shader_ids;
    const auto raster = candidate.state.rasterization;
    const auto attachments = candidate.state.attachments;
    const auto layout = candidate.state.vertex_layout;
    const u64 title = current_program_id;
    const u64 ordinal = ready_vertex_pipelines.size();
    ready_vertex_worker->QueueWork([prepared, shader_ids, raster, attachments, layout,
                                    title, ordinal, specialize] {
        try {
            if (!prepared->Build())
                prepared->MarkFailed();
        } catch (const std::exception&) {
            if (!prepared->IsDone())
                prepared->MarkFailed();
        }
        // AstraPro Log Line: One completion per admitted pipeline, including
        // failures. This is driver creation time, NEVER GPU execution time.
        LOG_INFO(Render_Vulkan,
                 "Uberhar GPU pipeline detail: schema=1 title={:016X} ordinal={} "
                 "key={:016X} vs={:016X} fs={:016X} gs={:016X} stages={} "
                 "specialized_optional={} topology={} bindings={} attributes={} "
                 "color_format={} depth_format={} driver_ms={:.3f} failed={} limit=256",
                 title, ordinal, prepared->Key(), shader_ids[0], shader_ids[1], shader_ids[2],
                 prepared->ShaderStageMask(), specialize, static_cast<u32>(raster.topology.Value()),
                 layout.binding_count, layout.attribute_count,
                 static_cast<u32>(attachments.color), static_cast<u32>(attachments.depth),
                 prepared->DriverBuildNs() / 1e6, prepared->HasFailed());
    });
    ++ready_vertex_deferred;
    return nullptr;
}

bool PipelineCache::UseProgrammableVertexShader(const Pica::RegsInternal& regs,
                                                Pica::ShaderSetup& setup,
                                                const VertexLayout& layout) {

    // AstraPro: Optional GPU translation has separate admission/failure rules.
    const bool ready_only = ready_vertex_worker != nullptr;
    auto res = curr_disk_cache->UseProgrammableVertexShader(regs, setup, layout, ready_only);

    if (res.has_value()) {
        current_shaders[ProgramType::VS] = (*res).second;
        shader_hashes[ProgramType::VS] = (*res).first;
        return true;
    }

    return false;
}

void PipelineCache::UseTrivialVertexShader() {
    current_shaders[ProgramType::VS] = &trivial_vertex_shader;
    shader_hashes[ProgramType::VS] = 0;
}

bool PipelineCache::UseFixedGeometryShader(const Pica::RegsInternal& regs) {

    auto res = curr_disk_cache->UseFixedGeometryShader(regs, ready_vertex_worker != nullptr);

    if (res.has_value()) {
        current_shaders[ProgramType::GS] = (*res).second;
        shader_hashes[ProgramType::GS] = (*res).first;
        return true;
    }

    return false;
}

void PipelineCache::UseTrivialGeometryShader() {
    current_shaders[ProgramType::GS] = nullptr;
    shader_hashes[ProgramType::GS] = 0;
}

void PipelineCache::UseFragmentShader(const Pica::RegsInternal& regs,
                                      const Pica::Shader::UserConfig& user,
                                      bool preparing_ready_gpu) {

    // AstraEH: Capture runtime controls before canonicalization. Lighting/procedural
    // structure and typed cube resources remain specialized; LUT controls/source slots are runtime.
    if (hybrid_tev) {
        tev_family_config.emplace(regs);
        // AstraEH: Unsupported draws cannot select a generic/CPU-bridge pipeline.
        // Do not decode 128 bytes of runtime TEV state that their specialized shader
        // will never consume. Every supported draw still takes its own full snapshot.
        tev_support_reason = GLSL::CheckDynamicTevSupport(*tev_family_config, user);
        tev_supported = tev_support_reason == GLSL::DynamicTevSupport::Ready;
        const bool need_transport = ReadyFragmentPolicy::NeedsDynamicTransport(
            tev_supported, preparing_ready_gpu,
            PreferReadySpecializedFragment(user));
        if (need_transport) {
            tev_constants = GLSL::MakeDynamicTevState(*tev_family_config, profile);
            ++tev_transport_prepared;
        } else if (tev_supported) {
            // AstraPro: This attempt can use only a ready specialized GPU FS or
            // return for a complete CPU draw. No generic consumer sees old data;
            // the CPU retry calls this again with preparing_ready_gpu=false.
            ++tev_transport_bypassed_gpu;
        } else {
            ++tev_transport_skipped;
        }
        tev_family_config->texture.tev_stages = {};
        tev_family_config->texture.combiner_buffer_input.Assign(0);
        tev_user = user;
    }

    // AstraEH: Do not even enqueue a specialized FS for covered test-profile draws.
    // Keep the original config for accurate recovery if a generic build fails.
    virtual_fs_config.reset();
    if (Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom &&
        hybrid_tev && tev_supported) {
        virtual_fs_config.emplace(regs);
        current_shaders[ProgramType::FS] = nullptr;
        shader_hashes[ProgramType::FS] = 0;
        return;
    }
    auto res = curr_disk_cache->UseFragmentShader(FSConfig{regs}, user);

    if (res.has_value()) {
        current_shaders[ProgramType::FS] = (*res).second;
        shader_hashes[ProgramType::FS] = (*res).first;
    }
}

// AstraEH: Build bounded, per-title fallback caches without changing transferable disk entries.
GraphicsPipeline* PipelineCache::GetTevFallback(const PipelineInfo& info, bool cpu_vertex) {
    if (!tev_family_config || !tev_supported) {
        return nullptr;
    }
    constexpr std::size_t MaxFamilies = 128;
    constexpr std::size_t MaxPipelines = 1024;
    // AstraEH: Reuse pure preparation only after complete raw/native input equality.
    // A miss still observes every census dimension before pipeline admission.
    const bool sample = (tev_preparation.Stats().requests & 1023U) == 0;
    const auto sample_start =
        sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto prepared = tev_preparation.Get(*tev_family_config, info);
    const auto& family_config = prepared.prepared.family;
    const u64 family_hash = prepared.prepared.candidates[1];
    if (!prepared.reused) {
        tev_light_counts_mask |= prepared.prepared.light_counts;
        constexpr std::size_t MaxCensusKeys = 2048;
        for (std::size_t i = 0; i < prepared.prepared.candidates.size(); ++i) {
            auto& keys = tev_candidate_keys[i];
            const auto key = prepared.prepared.candidates[i];
            if (keys.size() < MaxCensusKeys) {
                keys.insert(key);
            } else if (!keys.contains(key)) {
                tev_census_capped = true;
            }
        }
    }
    // AstraEH: Two clock reads per 1024 requests measure preparation plus census,
    // excluding actual pipeline lookup/compilation. Separate hit/miss populations.
    if (sample) {
        const auto ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - sample_start)
                                            .count());
        auto& count = prepared.reused ? tev_prepare_hit_samples : tev_prepare_miss_samples;
        auto& total = prepared.reused ? tev_prepare_hit_ns : tev_prepare_miss_ns;
        ++count;
        total += ns;
    }
    PipelineInfo fallback_info = info;
    fallback_info.state.shader_ids[ProgramType::FS] = family_hash;
    // AstraEH: Look up runtime identity before admission. Configurations that
    // share host VS/GS modules should also share their ready generic pipelines.
    auto shader_it = tev_shaders.find(family_hash);
    auto shaders = current_shaders;
    if (cpu_vertex) {
        shaders[ProgramType::VS] = &trivial_vertex_shader;
        shaders[ProgramType::GS] = nullptr;
    }
    if (!instance.UseGeometryShaders() || instance.IsFragmentShaderBarycentricSupported()) {
        shaders[ProgramType::GS] = nullptr;
    }
    if (shader_it != tev_shaders.end()) {
        if (shader_it->second->HasFailed()) {
            return nullptr;
        }
        shaders[ProgramType::FS] = shader_it->second.get();
        const u64 key = fallback_info.state.ExecutionHash(
            instance.IsExtendedDynamicStateSupported(), HostShaderIds(shaders));
        if (auto it = tev_pipelines.find(key); it != tev_pipelines.end()) {
            return it->second->HasFailed() ? nullptr : it->second.get();
        }
    }
    if (tev_pipelines.size() >= MaxPipelines) {
        return nullptr;
    }
    // AstraEH: Do not accumulate speculative work while a fallback is compiling.
    // Later misses can retry; BindPipeline also queues the required specialization.
    if (!force_tev && warming_tev_pipeline && !warming_tev_pipeline->IsDone()) {
        ++fallback_deferred;
        return nullptr;
    }

    if (shader_it == tev_shaders.end()) {
        if (tev_shaders.size() >= MaxFamilies) {
            return nullptr;
        }
        auto shader = std::make_unique<Shader>(instance);
        shader_it = tev_shaders.emplace(family_hash, std::move(shader)).first;
    }
    // AstraEH: CPU vertices already include transforms and quaternion sign fixes.
    // Both routes preserve native fixed-function state in their execution key.
    auto* shader_ptr = shader_it->second.get();
    shaders[ProgramType::FS] = shader_ptr;
    const u64 hash = fallback_info.state.ExecutionHash(instance.IsExtendedDynamicStateSupported(),
                                                       HostShaderIds(shaders));
    auto pipeline = std::make_unique<GraphicsPipeline>(
        instance, renderpass_cache, fallback_info, *driver_pipeline_cache, *pipeline_layout,
        shaders, tev_worker.get(),
        PipelineBuildOptions{&pipeline_completion, &fallback_build_stats, true});
    auto* pipeline_ptr = pipeline.get();
    tev_pipelines.emplace(hash, std::move(pipeline));
    warming_tev_pipeline = pipeline_ptr;

    // AstraEH: One serial job compiles the fragment module before building its pipeline.
    // This cannot fill specialized pipeline workers with waits for large fallback shaders.
    // Owned config copies and map-stable pointers stay alive until ClearTevFallbacks drains.
    const auto queued = std::chrono::steady_clock::now();
    // AstraEH: A title/profile switch may start before the serial job drains. Freeze all
    // generation and disk-key inputs now; workers must not reread mutable UI/profile state.
    const auto job_profile = profile;
    const bool disable_optimizer = Settings::values.disable_spirv_optimizer.GetValue();
    const bool persistent = Settings::values.use_disk_shader_cache.GetValue();
    const u64 title = current_program_id;
    const auto directory = GetPipelineCacheDir();
    tev_worker->QueueWork([this, shader_ptr, pipeline_ptr, family_hash, queued, cpu_vertex,
                           config = family_config, user = tev_user, job_profile, disable_optimizer,
                           persistent, title, directory] {
        try {
            if (shader_ptr->HasFailed()) {
                throw std::runtime_error("fallback fragment family previously failed");
            }
            const auto start = std::chrono::steady_clock::now();
            const auto activity_start = Common::UberharActivity::Capture();
            // AstraEH: Size diagnostics let device logs confirm the compact module
            // reached the driver. Zero means this family reused an existing module.
            std::size_t glsl_bytes = 0;
            std::size_t spirv_bytes = 0;
            if (!shader_ptr->IsDone()) {
                GLSL::FragmentModule module{config, user, job_profile, true};
                const auto source = module.Generate();
                const auto code =
                    LoadOrCompileTevModule(source, disable_optimizer, persistent, title, directory);
                if (code.empty()) {
                    throw std::runtime_error("fallback GLSL compilation produced no SPIR-V");
                }
                glsl_bytes = source.size();
                spirv_bytes = code.size() * sizeof(u32);
                shader_ptr->module = CompileSPV(code, instance.GetDevice());
                if (!shader_ptr->module) {
                    throw std::runtime_error("fallback shader module is null");
                }
                shader_ptr->MarkDone();
            }
            const auto shader_done = std::chrono::steady_clock::now();
            if (!pipeline_ptr->Build()) {
                throw std::runtime_error("fallback graphics pipeline creation failed");
            }
            const auto pipeline_done = std::chrono::steady_clock::now();
            const u64 shader_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(shader_done - start).count();
            const u64 driver_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(pipeline_done - shader_done)
                    .count();
            const u64 job_queue_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(start - queued).count();
            const auto activity_end = Common::UberharActivity::Capture();
            const auto phase = Common::UberharActivity::Between(activity_start, activity_end);
            const auto phase_index = static_cast<std::size_t>(phase);
            phase_builds[phase_index].fetch_add(1, std::memory_order_relaxed);
            phase_build_ns[phase_index].fetch_add(shader_ns + driver_ns, std::memory_order_relaxed);
            const auto builds = fallback_compile_jobs.fetch_add(1, std::memory_order::relaxed) + 1;
            fallback_job_queue_ns.fetch_add(job_queue_ns, std::memory_order::relaxed);
            fallback_shader_ns.fetch_add(shader_ns, std::memory_order::relaxed);
            fallback_driver_ns.fetch_add(driver_ns, std::memory_order::relaxed);
            // AstraEH: First 32 distinct materialized families per title, including
            // disk hits. Pipeline variants cannot consume the family detail budget.
            // Describe remaining static state without uniforms or guest shader code.
            if (glsl_bytes != 0 && tev_family_details < 32) {
                ++tev_family_details;
                // AstraEH Log Line: Bounded static shapes; count/slot controls are now runtime.
                LOG_INFO(Render_Vulkan,
                         "Uberhar lighting family: ordinal={} limit=32 family={:016X} "
                         "lighting_enabled={} light_count_mode=runtime key_flags={:08X} "
                         "texture0={} proctex={:08X} glsl_bytes={} spirv_bytes={}",
                         tev_family_details, family_hash, config.lighting.enable.Value(),
                         config.lighting.raw,
                         static_cast<u32>(config.texture.texture0_type.Value()), config.proctex.raw,
                         glsl_bytes, spirv_bytes);
            }
            // AstraEH: Build includes waiting for VS/GS dependencies; this is wall time,
            // not pure driver CPU time. Report only the first 20 builds to bound output.
            if (builds <= 20) {
                // AstraEH Log Line: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
                LOG_INFO(Render_Vulkan,
                         "Uberhar fallback build: family={:016X} key={:016X} queue_ms={:.3f} "
                         "shader_ms={:.3f} pipeline_ms={:.3f} glsl_bytes={} spirv_bytes={} "
                         "cpu_vertex={} phase={} session={}",
                         family_hash, pipeline_ptr->Key(), job_queue_ns / 1000000.0,
                         shader_ns / 1000000.0, driver_ns / 1000000.0, glsl_bytes, spirv_bytes,
                         cpu_vertex, Common::UberharActivity::Name(phase), activity_start.run);
            }
        } catch (const std::exception& error) {
            if (!shader_ptr->IsDone()) {
                shader_ptr->MarkFailed();
            }
            pipeline_ptr->MarkFailed();
            if (fallback_failures.fetch_add(1, std::memory_order_relaxed) < 8) {
                // AstraEH Log Line: capped failure detail; wake waiters and use specialization.
                LOG_ERROR(
                    Render_Vulkan,
                    "Uberhar fallback failure: family={:016X} key={:016X} cpu_vertex={} error={}",
                    family_hash, pipeline_ptr->Key(), cpu_vertex, error.what());
            }
        }
    });
    return pipeline_ptr;
}

// AstraEH: Persist generated generic modules, not game programs. The title-prefixed
// files live alongside driver caches so Android's existing Vulkan-cache deletion removes
// both. Optional I/O runs on the serial TEV worker; failure retains normal compilation.
std::vector<u32> PipelineCache::LoadOrCompileTevModule(std::string_view source,
                                                       bool disable_optimizer, bool persistent,
                                                       u64 title, const std::string& directory) {
    using namespace UberharSpirvCache;
    const std::string_view version{Common::g_shader_cache_version};
    // AstraEH: Also separate builds so a compiler-library/submodule update cannot reuse old output.
    const std::string_view revision{Common::g_scm_rev};
    const Key key{Common::ComputeHash64(source.data(), source.size()),
                  Common::HashCombine(Common::ComputeHash64(version.data(), version.size()),
                                      Common::ComputeHash64(revision.data(), revision.size()),
                                      disable_optimizer)};
    const auto path = fmt::format("{}{:016X}-uber-{:016X}.spv", directory, title,
                                  Common::HashCombine(key.source, key.compiler));
    if (persistent && FileUtil::Exists(path)) {
        FileUtil::IOFile file(path, "rb");
        const u64 bytes = file ? file.GetSize() : 0;
        if (bytes >= HeaderWords * sizeof(u32) && bytes <= MaxFileBytes &&
            bytes % sizeof(u32) == 0) {
            std::vector<u32> words(bytes / sizeof(u32));
            if (file.ReadBytes(words.data(), bytes) == bytes) {
                auto code = Decode(key, words);
                if (!code.empty()) {
                    ++generic_module_hits;
                    return code;
                }
            }
        }
        if (generic_module_rejected.fetch_add(1) < 4) {
            // AstraEH Log Line: Rebuild bad/obsolete entries; never pass truncated data to Vulkan.
            LOG_WARNING(Render_Vulkan, "Uberhar generic module cache rejected; rebuilding");
        }
    }
    ++generic_module_misses;
    auto code = CompileGLSL(source, vk::ShaderStageFlagBits::eFragment, "", disable_optimizer);
    if (persistent && !code.empty()) {
        const auto words = Encode(key, code);
        const auto temporary = path + ".tmp";
        FileUtil::IOFile file(temporary, "wb");
        const bool written = !words.empty() && file &&
                             file.WriteBytes(words.data(), words.size() * sizeof(u32)) ==
                                 words.size() * sizeof(u32) &&
                             file.Flush();
        file.Close();
        if (!written || !FileUtil::Rename(temporary, path)) {
            FileUtil::Delete(temporary);
            if (generic_module_write_failures.fetch_add(1) < 4) {
                // AstraEH Log Line: Optional cache failures do not fail rendering or installation.
                LOG_WARNING(Render_Vulkan,
                            "Uberhar generic module cache write failed; using compiled module");
            }
        }
    }
    return code;
}

// AstraEH: Warm a shared software-vertex fallback while the demanded GPU pipeline
// compiles. Switch only when it is ready, before recording a GPU draw, so the PICA
// caller can execute exactly one CPU vertex batch through its established path.
PipelineCache::CpuBridgePreparation PipelineCache::PrepareCpuFallback(
    const PipelineInfo& original, const VertexLayout& software_layout, u32 vertices) {
    if (!cpu_vertex_bridge || !tev_supported || !tev_family_config) {
        return {};
    }
    // AstraEH: The PICA bridge contract now isolates strip/fan assembly. Record
    // exact admission reasons so a disabled route cannot hide behind one total.
    const auto topology = original.state.rasterization.topology.Value();
    const auto topology_index = std::min<u32>(static_cast<u32>(topology), 4);
    ++cpu_bridge_topology[topology_index];
    const auto admission = CheckCpuBridgeAdmission(topology, vertices);
    ++cpu_bridge_admission[static_cast<std::size_t>(admission)];
    if (admission != CpuBridgeAdmission::Eligible) {
        ++cpu_bridge_limited;
        return {};
    }
    auto info = original;
    info.state.shader_ids = shader_hashes;
    auto* specialized = curr_disk_cache->GetPipeline(info);
    if (specialized->IsDone()) {
        return {};
    }
    ++cpu_bridge_pending;
    info.state.shader_ids[ProgramType::VS] = 0;
    info.state.shader_ids[ProgramType::GS] = 0;
    info.state.vertex_layout = software_layout;
    // AstraEH: CPU assembly emits independent triangles regardless of input
    // topology. Key and build the same list pipeline that DrawTriangles binds.
    info.state.rasterization.topology.Assign(Pica::PipelineRegs::TriangleTopology::List);
    auto* fallback = GetTevFallback(info, true);
    specialized->TryBuild(true, true);
    // AstraEH: Prefer specialization if it finished while the generic lookup ran.
    if (specialized->IsDone()) {
        return {nullptr, true};
    }
    if (fallback && fallback->IsDone() && !fallback->HasFailed()) {
        ++cpu_bridge_selected;
        ++cpu_bridge_selected_topology[topology_index];
        return {fallback, true};
    }
    ++cpu_bridge_warming;
    return {nullptr, true};
}

// AstraEH: Time the CPU batch outside the shader/driver waits. This includes the
// existing CPU JIT on a first program encounter; it is not pure vertex arithmetic.
void PipelineCache::RecordCpuBridgeVertices(u64 elapsed_ns, u32 vertices) {
    ++cpu_bridge_batches;
    cpu_bridge_vertices += vertices;
    cpu_bridge_cpu_ns += elapsed_ns;
    cpu_bridge_cpu_max_ns = std::max(cpu_bridge_cpu_max_ns, elapsed_ns);
}

// AstraEH: Drain users before destroying objects referenced by queued commands/compiler jobs.
void PipelineCache::ClearTevFallbacks() {
    scheduler.Finish();
    pipeline_workers.WaitForRequests();
    shader_workers.WaitForRequests();
    tev_worker->WaitForRequests();
    // AstraPro: Drain and release optional pipelines before their shared shaders.
    if (ready_vertex_worker)
        ready_vertex_worker->WaitForRequests();
    // CodexAstraUlt Log Line: Sample at the existing drained cache boundary, without another
    // wait. Allocation counters cannot attribute opaque driver memory to these shader objects.
    instance.ReportMemoryUsage("before_cache_clear", GetProgramID(), cache_session,
                               scheduler.CurrentTick(),
                               scheduler.GetMasterSemaphore()->KnownGpuTick());
    warming_ready_vertex = nullptr;
    ready_vertex_pipelines.clear();
    warming_tev_pipeline = nullptr;
    tev_pipelines.clear();
    tev_shaders.clear();
    // AstraEH: Keep candidate coverage scoped to the same title as the fallback maps.
    for (auto& keys : tev_candidate_keys) {
        keys.clear();
    }
    tev_push_constants.Reset(); // AstraEH: Scheduler was drained above; no cross-title reuse.
    tev_preparation.Reset(); // AstraEH: Match census/title lifetime; retain no old observation.
    tev_prepare_hit_samples = tev_prepare_miss_samples = 0;
    tev_prepare_hit_ns = tev_prepare_miss_ns = 0;
    tev_census_capped = false;
    tev_light_counts_mask = 0; // AstraEH: Runtime count coverage belongs to this title.
    tev_family_details = 0;    // AstraEH: The serial compiler is drained above.
    bound_pipeline = nullptr;
    tev_family_config.reset();
    // CodexAstraUlt Log Line: Pair the post-clear observation with the same title/session.
    instance.ReportMemoryUsage("after_cache_clear", GetProgramID(), cache_session,
                               scheduler.CurrentTick(),
                               scheduler.GetMasterSemaphore()->KnownGpuTick());
}

// AstraEH: Ordered worker snapshots avoid per-draw atomics or a reporting stall.
// Progress is queued; the destructor reads directly only after WaitWorker().
void PipelineCache::ReportTevPushStats(const char* kind) {
    // CodexAstraUlt: Only repeated progress is optional; every final report stays reliable.
    const auto delivery = std::strcmp(kind, "progress") == 0
                              ? Common::Log::Delivery::Diagnostic
                              : Common::Log::Delivery::Reliable;
    const auto& stats = tev_push_constants.Stats();
    // AstraEH: Same five-second/final cadence; completed worker commands only.
    // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar push constants {}: schema=1 scope=current_title requests={} uploads={} "
             "reuses={} invalidations={} bytes_per_upload={} saved_bytes={} "
             "observation=command_worker timing=not_measured",
             kind, stats.requests, stats.uploads, stats.reuses, stats.invalidations,
             sizeof(TevPushConstants), stats.reuses * sizeof(TevPushConstants));
}

// AstraEH: These are draw observations and CPU wait durations, not GPU timings or frame counts.
void PipelineCache::ReportUberharStats(const char* kind) {
    // CodexAstraUlt: Only repeated progress is optional; every final report stays reliable.
    const auto delivery = std::strcmp(kind, "progress") == 0
                              ? Common::Log::Delivery::Diagnostic
                              : Common::Log::Delivery::Reliable;
    // AstraPro: Existing five-second/final cadence; no per-draw strings.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar ready GPU vertices {}: schema=1 scope=renderer_lifetime requests={} selected={} dependency_misses={} "
             "deferred={} capped={} failed_draw_attempts={} key_mismatches={} pipelines={} "
             "builds={} driver_ms={:.3f} driver_max_ms={:.3f} max_pipelines=256 "
             "selection=ready_only fallback=full_cpu_draw fragment_preflight_deferred={}",
             kind, ready_vertex_requests, ready_vertex_selected, ready_vertex_dependency_misses,
             ready_vertex_deferred, ready_vertex_capped, ready_vertex_failed,
             ready_vertex_key_mismatches, ready_vertex_pipelines.size(),
             ready_vertex_build_stats.builds.load(), ready_vertex_build_stats.driver_ns.load()/1e6,
             ready_vertex_build_stats.driver_max_ns.load()/1e6, ready_fragment_preflight_deferred);

    // AstraEH: File presence at load and observed compatible reuse answer different
    // questions. Fingerprint changes can produce present_files + cold_encountered.
    const u64 observed_hits = generic_module_hits.load() - cache_start_hits;
    const u64 observed_misses = generic_module_misses.load() - cache_start_misses;
    const bool disk_enabled = Settings::values.use_disk_shader_cache.GetValue();
    // AstraEH Log Line: Existing progress cadence and final report, not per lookup.
    LOG_INFO(Render_Vulkan,
             "Uberhar cache use {}: schema=1 session={} title={:016X} start_state={} "
             "generic_reuse={} observed_hits={} observed_misses={} driver_load={} "
             "disk_enabled_start={} disk_enabled_now={} driver_internal=unknown",
             kind, cache_session, GetProgramID(), cache_start.State(),
             UberharCacheDiagnostics::ReuseState(disk_enabled, observed_hits, observed_misses),
             observed_hits, observed_misses, driver_cache_load, cache_start.enabled, disk_enabled);
    if (hybrid_tev) {
        // AstraEH: Final reporting already drains this worker. Progress runs in
        // queue order and may describe fewer draws than renderer-thread records.
        if (std::strcmp(kind, "totals") == 0) {
            ReportTevPushStats(kind);
        } else {
            scheduler.Record([this](vk::CommandBuffer) { ReportTevPushStats("progress"); });
        }
        const auto& preparation = tev_preparation.Stats();
        // AstraEH: Aggregate at the existing five-second/final cadence only.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar preparation {}: schema=1 scope=current_title requests={} hits={} "
                 "misses={} evictions={} invalidations={} capacity={} storage_bytes={} "
                 "sample_period_requests=1024 hit_samples={} hit_sample_ns={} miss_samples={} "
                 "miss_sample_ns={} timing=host_preparation_and_census",
                 kind, preparation.requests, preparation.hits, preparation.misses,
                 preparation.evictions, preparation.invalidations, TevPreparationCache<>::Entries,
                 sizeof(tev_preparation), tev_prepare_hit_samples, tev_prepare_hit_ns,
                 tev_prepare_miss_samples, tev_prepare_miss_ns);
        // AstraEH Log Line: Existing bounded progress cadence; files never contain guest shader
        // code.
        LOG_INFO(Render_Vulkan,
                 "Uberhar generic modules {}: hits={} misses={} rejected={} write_failures={} "
                 "optimizer_disabled={}",
                 kind, generic_module_hits.load(), generic_module_misses.load(),
                 generic_module_rejected.load(), generic_module_write_failures.load(),
                 Settings::values.disable_spirv_optimizer.GetValue());
    }
    if (Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom) {
        // AstraEH: Foreground generic waits must not disappear from measured stutter.
        // AstraPro: CPU and optional GPU vertices may share these fragment-route counts.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar virtual native {}: generic_draws={} recovery_draws={} generic_waits={} "
                 "generic_wait_ms={:.3f} generic_max_wait_ms={:.3f} vertex_engine={} "
                 "complete_ready_bank=false optimized_gpu_draws={}",
                 kind, virtual_generic_draws, virtual_recovery_draws, virtual_waits,
                 virtual_wait_ns / 1000000.0, virtual_max_wait_ns / 1000000.0,
                 ready_vertex_worker ? "cpu_and_ready_gpu" : "cpu", virtual_specialized_gpu_draws);
    }
    // AstraEH Log Line: One exclusive route-reason summary at the existing cadence.
    // Counts describe draw-path choices, never GPU cost or lost visuals.
    if (Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom) {
        // AstraPro: GPU transport bypass is not unsupported-fragment recovery.
        // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar recovery reasons {}: schema=1 spirv_incompatible={} shadow2d={} "
                 "gas_fog={} custom_user={} light_count={} lighting_lut={} add_signed={} "
                 "generic_failed={} generic_unavailable={} disabled_or_other={} "
                 "transport_prepared={} transport_skipped={} transport_bypassed_gpu={} weighting=draws exclusive=true",
                 kind, virtual_recovery_reasons[1], virtual_recovery_reasons[2],
                 virtual_recovery_reasons[3], virtual_recovery_reasons[4],
                 virtual_recovery_reasons[5], virtual_recovery_reasons[6],
                 virtual_recovery_reasons[7], virtual_recovery_reasons[8],
                 virtual_recovery_reasons[9], virtual_recovery_reasons[10],
                 tev_transport_prepared, tev_transport_skipped, tev_transport_bypassed_gpu);
    }
    // AstraEH: Cumulative buckets at the existing cadence; build time and wait time overlap.
    if (hybrid_tev) {
        for (std::size_t i = 0; i < phase_waits.size(); ++i) {
            const auto& wait = phase_waits[i];
            const auto builds = phase_builds[i].load(std::memory_order_relaxed);
            if (!wait.count && !builds)
                continue;
            // AstraEH Log Line: At most five phase summaries per normal progress/final report.
            LOG_INFO(Render_Vulkan,
                     "Uberhar compile phase {}: phase={} generic_waits={} "
                     "generic_wait_ms={:.3f} generic_max_ms={:.3f} "
                     "generic_builds={} build_wall_ms={:.3f}",
                     kind, Common::UberharActivity::Names[i], wait.count, wait.ns / 1e6,
                     wait.maximum / 1e6, builds,
                     phase_build_ns[i].load(std::memory_order_relaxed) / 1e6);
        }
        const auto& loop = tev_loop_histogram;
        const auto& active = tev_active_histogram;
        // AstraEH: Seven fixed bins each (0..6), all selected Native generic draws.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar TEV plan {}: loop_stages=[{},{},{},{},{},{},{}] "
                 "active_stages=[{},{},{},{},{},{},{}] weighting=draws",
                 kind, loop[0], loop[1], loop[2], loop[3], loop[4], loop[5], loop[6], active[0],
                 active[1], active[2], active[3], active[4], active[5], active[6]);
    }
    // AstraEH: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
    // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
        "Uberhar {}: draws={} specialized_pending={} fallback_draws={} "
        "fallback_warming={} fallback_unavailable={} skipped={} families={} fallback_pipelines={} "
        "scheduler_pipeline_waits={} scheduler_pipeline_wait_ms={:.3f} "
        "scheduler_max_wait_ms={:.3f} forced_fallback_wait_ms={:.3f} slow_waits={} "
        "fallback_deferred={} fallback_builds={} fallback_shader_ms={:.3f} "
        "fallback_pipeline_ms={:.3f} first_ready_waits={} late_fallback_draws={} "
        "fallback_job_queue_ms={:.3f} fallback_failures={}",
        kind, draw_requests, specialized_pending, fallback_draws.load(), fallback_warming,
        fallback_unavailable, skipped_draws, tev_shaders.size(), tev_pipelines.size(),
        pipeline_waits.load(), pipeline_wait_ns.load() / 1000000.0,
        pipeline_wait_max_ns.load() / 1000000.0, fallback_wait_ns.load() / 1000000.0,
        slow_pipeline_waits.load(), fallback_deferred, fallback_compile_jobs.load(),
        fallback_shader_ns.load() / 1000000.0, fallback_driver_ns.load() / 1000000.0,
        first_ready_waits.load(), late_fallback_draws.load(),
        fallback_job_queue_ns.load() / 1000000.0, fallback_failures.load());
    // AstraEH: These overlapping worker totals identify the bottleneck; they are not
    // additional scheduler stall time. Progress reads may straddle a build completion.
    const auto report_builds = [kind](const char* path, const PipelineBuildStats& stats) {
        // AstraEH Log Line: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
        LOG_INFO(Render_Vulkan,
                 "Uberhar build {}: path={} builds={} queue_ms={:.3f} vs_wait_ms={:.3f} "
                 "fs_wait_ms={:.3f} gs_wait_ms={:.3f} driver_ms={:.3f} driver_max_ms={:.3f} "
                 "slow_builds={}",
                 kind, path, stats.builds.load(), stats.queue_ns.load() / 1000000.0,
                 stats.shader_wait_ns[0].load() / 1000000.0,
                 stats.shader_wait_ns[1].load() / 1000000.0,
                 stats.shader_wait_ns[2].load() / 1000000.0, stats.driver_ns.load() / 1000000.0,
                 stats.driver_max_ns.load() / 1000000.0, stats.slow_builds.load());
    };
    if (hybrid_tev) {
        const auto histogram = wait_diagnostics.Histogram();
        // AstraEH: one aggregate covers every wait, including late-session events.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar wait histogram {}: lt1ms={} lt16_667ms={} lt50ms={} lt100ms={} "
                 "lt250ms={} lt500ms={} lt1000ms={} ge1000ms={}",
                 kind, histogram[0], histogram[1], histogram[2], histogram[3], histogram[4],
                 histogram[5], histogram[6], histogram[7]);
        if (std::string_view{kind} == "totals") {
            // AstraEH: At shutdown all command/compiler workers have drained.
            for (const auto& event : wait_diagnostics.Worst()) {
                if (event.elapsed_ns == 0)
                    continue;
                // AstraEH Log Line: at most eight worst waits per renderer shutdown.
                LOG_INFO(
                    Render_Vulkan,
                    "Uberhar worst wait: start_ms={:.3f} draw={} wait_ms={:.3f} "
                    "wait_key={:016X} chosen_key={:016X} topology={} pending_stages={} phase={} "
                    "alternative={} alternative_phase={} alternative_stages={} fallback={}",
                    event.start_ns / 1000000.0, event.draw, event.elapsed_ns / 1000000.0,
                    event.wait_key, event.chosen_key, event.topology, event.stages, event.phase,
                    event.alternative, event.alternative_phase, event.alternative_stages,
                    event.fallback);
            }
        }
        const auto& admission = cpu_bridge_admission;
        const auto& topology = cpu_bridge_topology;
        const auto& selected = cpu_bridge_selected_topology;
        // AstraEH: bounded coverage reasons; all are observations, not unique programs.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar bridge coverage {}: eligible={} too_small={} input_limit={} "
                 "incomplete_list={} output_limit={} unsupported_topology={} "
                 "seen_list={} seen_strip={} seen_fan={} seen_shader_list={} seen_unknown={} "
                 "selected_list={} selected_strip={} selected_fan={} selected_shader_list={}",
                 kind, admission[0], admission[1], admission[2], admission[3], admission[4],
                 admission[5], topology[0], topology[1], topology[2], topology[3], topology[4],
                 selected[0], selected[1], selected[2], selected[3]);
        // AstraEH: aggregate bridge utility and CPU cost; no per-draw output.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
            "Uberhar CPU bridge {}: pending={} selected={} warming={} ineligible_draws={} draws={} "
            "mismatches={} batches={} vertices={} cpu_ms={:.3f} cpu_max_ms={:.3f}",
            kind, cpu_bridge_pending, cpu_bridge_selected, cpu_bridge_warming, cpu_bridge_limited,
            cpu_bridge_draws, cpu_bridge_mismatches, cpu_bridge_batches, cpu_bridge_vertices,
            cpu_bridge_cpu_ns / 1000000.0, cpu_bridge_cpu_max_ns / 1000000.0);
        if (curr_disk_cache) {
            curr_disk_cache->ReportUberharStats(kind);
        }
        report_builds("specialized", specialized_build_stats);
        report_builds("fallback_compact", fallback_build_stats);
        // AstraEH: Capped counts are lower bounds. Fixed-state counts describe raw
        // candidates even when a device can make some of those states dynamic.
        // AstraEH: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
            "Uberhar variant census {}: scope=current_title_candidates raw_families={} "
            "canonical_families={} raw_pipelines={} canonical_pipelines={} vertex_programs={} "
            "geometry_programs={} vertex_layouts={} attachments={} blending={} rasterization={} "
            "depth_stencil={} previous_lighting_families={} lighting_shapes={} proctex_shapes={} "
            "alpha13_families={} alpha14_families={} seen_light_counts={:03X} lighting_abi={} "
            "capped={} limit=2048",
            kind, tev_candidate_keys[0].size(), tev_candidate_keys[1].size(),
            tev_candidate_keys[2].size(), tev_candidate_keys[3].size(),
            tev_candidate_keys[4].size(), tev_candidate_keys[5].size(),
            tev_candidate_keys[6].size(), tev_candidate_keys[7].size(),
            tev_candidate_keys[8].size(), tev_candidate_keys[9].size(),
            tev_candidate_keys[10].size(), tev_candidate_keys[11].size(),
            tev_candidate_keys[12].size(), tev_candidate_keys[13].size(),
            tev_candidate_keys[14].size(), tev_candidate_keys[15].size(), tev_light_counts_mask,
            GLSL::DynamicTevAbiVersion, tev_census_capped);
        // AstraEH: Unused means not selected by the scheduler as of this snapshot,
        // not permanently useless. Only completed builds contribute driver time.
        u64 used = 0;
        u64 unused = 0;
        u64 unused_driver_ns = 0;
        for (const auto& [key, pipeline] : tev_pipelines) {
            if (!pipeline->IsDone() || pipeline->HasFailed()) {
                continue;
            }
            if (pipeline->FallbackUses() != 0) {
                ++used;
            } else {
                ++unused;
                unused_driver_ns += pipeline->DriverBuildNs();
            }
        }
        // AstraEH: bounded renderer diagnostics; see docs/UBERHAR_DIAGNOSTICS.md.
        // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
                 "Uberhar fallback utility {}: used_pipelines={} unused_pipelines={} "
                 "unused_driver_ms={:.3f}",
                 kind, used, unused, unused_driver_ns / 1000000.0);
    }
}

bool PipelineCache::IsCacheValid(std::span<const u8> data) const {
    if (data.size() < sizeof(vk::PipelineCacheHeaderVersionOne)) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Invalid header");
        return false;
    }

    vk::PipelineCacheHeaderVersionOne header;
    std::memcpy(&header, data.data(), sizeof(header));
    if (header.headerSize < sizeof(header)) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Invalid header length");
        return false;
    }

    if (header.headerVersion != vk::PipelineCacheHeaderVersion::eOne) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Invalid header version");
        return false;
    }

    if (u32 vendor_id = instance.GetVendorID(); header.vendorID != vendor_id) {
        LOG_ERROR(
            Render_Vulkan,
            "Pipeline cache failed validation: Incorrect vendor ID (file: {:#X}, device: {:#X})",
            header.vendorID, vendor_id);
        return false;
    }

    if (u32 device_id = instance.GetDeviceID(); header.deviceID != device_id) {
        LOG_ERROR(
            Render_Vulkan,
            "Pipeline cache failed validation: Incorrect device ID (file: {:#X}, device: {:#X})",
            header.deviceID, device_id);
        return false;
    }

    if (header.pipelineCacheUUID != instance.GetPipelineCacheUUID()) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Incorrect UUID");
        return false;
    }

    return true;
}

bool PipelineCache::EnsureDirectories() const {
    const auto create_dir = [](const std::string& dir) {
        if (!FileUtil::CreateDir(dir)) {
            LOG_ERROR(Render_Vulkan, "Failed to create directory={}", dir);
            return false;
        }

        return true;
    };

    return create_dir(FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir)) &&
           create_dir(GetVulkanDir()) && create_dir(GetPipelineCacheDir()) &&
           create_dir(GetTransferableDir());
}

std::string PipelineCache::GetVulkanDir() const {
    return FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir) + "vulkan" + DIR_SEP;
}

std::string PipelineCache::GetPipelineCacheDir() const {
    return GetVulkanDir() + "pipeline" + DIR_SEP;
}

std::string PipelineCache::GetTransferableDir() const {
    return GetVulkanDir() + DIR_SEP + "transferable";
}

} // namespace Vulkan

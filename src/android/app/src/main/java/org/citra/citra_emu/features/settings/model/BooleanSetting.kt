// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.SettingKeys

enum class BooleanSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Boolean
) : AbstractBooleanSetting {
    EXPAND_TO_CUTOUT_AREA(SettingKeys.expand_to_cutout_area(), Settings.SECTION_LAYOUT, false),
    SPIRV_SHADER_GEN(SettingKeys.spirv_shader_gen(), Settings.SECTION_RENDERER, true),
    ASYNC_SHADERS(SettingKeys.async_shader_compilation(), Settings.SECTION_RENDERER, false),
    // AstraEH: Default both experiments off so the first launch uses upstream rendering.
    UBERHAR_HYBRID_TEV(SettingKeys.uberhar_hybrid_tev(), Settings.SECTION_RENDERER, false),
    UBERHAR_FORCE_TEV(SettingKeys.uberhar_force_tev(), Settings.SECTION_RENDERER, false),
    // AstraEH: Enabled within hybrid mode; can be disabled independently for comparison.
    UBERHAR_CPU_VERTEX_BRIDGE(SettingKeys.uberhar_cpu_vertex_bridge(), Settings.SECTION_RENDERER, true),
    // CodexAstraLocal: Preserve the old Combo default; comparison needs a restart.
    UBERHAR_CPU_VERTEX_ONLY(SettingKeys.uberhar_cpu_vertex_only(), Settings.SECTION_RENDERER, false),
    // CodexAstraLocal: Persist independent comparison controls; game restart applies them.
    UBERHAR_NATIVE_FULL_FRAGMENTS(SettingKeys.uberhar_native_full_fragments(), Settings.SECTION_RENDERER, true),
    UBERHAR_CPU_PREFIX_OVERLAP(SettingKeys.uberhar_cpu_prefix_overlap(), Settings.SECTION_RENDERER, true),
    UBERHAR_PARALLEL_TEXTURE_DECODE(SettingKeys.uberhar_parallel_texture_decode(), Settings.SECTION_RENDERER, true),
    // CodexAstraLocal: Avoid optional permit counters unless explicitly requested.
    UBERHAR_CPU_EXECUTION_CENSUS(SettingKeys.uberhar_cpu_execution_census(), Settings.SECTION_RENDERER, false),

    // CodexAstraLocal: Driver submission policy is frozen at renderer creation.
    TURNIP_PASS_END_FLUSH(SettingKeys.turnip_pass_end_flush(), Settings.SECTION_RENDERER, false),
    // CodexAstraLocal: Freeze generic program/cache identity for the game launch.
    UBERHAR_OPTIMIZE_GENERIC_SHADERS(SettingKeys.uberhar_optimize_generic_shaders(), Settings.SECTION_RENDERER, false),
    // CodexAstraLocal: Persist explicit per-game false values; all mechanisms are launch-owned.
    UBERHAR_CHANGED_STATE_ONLY(SettingKeys.uberhar_changed_state_only(), Settings.SECTION_RENDERER, true),
    UBERHAR_REUSE_CONTAINED_PASS(SettingKeys.uberhar_reuse_contained_pass(), Settings.SECTION_RENDERER, true),
    UBERHAR_TYPED_PRIMITIVE_APPEND(SettingKeys.uberhar_typed_primitive_append(), Settings.SECTION_RENDERER, true),
    UBERHAR_INCLUDE_SCISSOR(SettingKeys.uberhar_include_scissor(), Settings.SECTION_RENDERER, true),
    UBERHAR_ADAPTIVE_CPU_WORKERS(SettingKeys.uberhar_adaptive_cpu_workers(), Settings.SECTION_RENDERER, true),
    UBERHAR_GPU_OBSERVER(SettingKeys.uberhar_gpu_observer(), Settings.SECTION_RENDERER, true),
    UBERHAR_FRAME_WORKLOAD_CENSUS(SettingKeys.uberhar_frame_workload_census(), Settings.SECTION_RENDERER, false),
    UBERHAR_PREPARE_DISPLAYED_EYE_ONLY(SettingKeys.uberhar_prepare_displayed_eye_only(), Settings.SECTION_RENDERER, true),
    UBERHAR_EARLY_RIGHT_EYE(SettingKeys.uberhar_early_right_eye(), Settings.SECTION_RENDERER, false),
    UBERHAR_FAST_VERTEX_CONVERSION(SettingKeys.uberhar_fast_vertex_conversion(), Settings.SECTION_RENDERER, false),
    UBERHAR_VERTEX_MEMO(SettingKeys.uberhar_vertex_memo(), Settings.SECTION_RENDERER, false),
    UBERHAR_WORKER_CPU_UPLOAD(SettingKeys.uberhar_worker_cpu_upload(), Settings.SECTION_RENDERER, false),
    UBERHAR_CPU_CLIP_REJECT(SettingKeys.uberhar_cpu_clip_reject(), Settings.SECTION_RENDERER, false),
    UBERHAR_FULL_PIPELINE_BANK_16(SettingKeys.uberhar_full_pipeline_bank_16(), Settings.SECTION_RENDERER, false),
    UBERHAR_PRUNE_SPIRV(SettingKeys.uberhar_prune_spirv(), Settings.SECTION_RENDERER, false),
    UBERHAR_INDEXED_CPU_OUTPUT(SettingKeys.uberhar_indexed_cpu_output(), Settings.SECTION_RENDERER, false),
    UBERHAR_MONO_GUEST_3D(SettingKeys.uberhar_mono_guest_3d(), Settings.SECTION_RENDERER, false),
    DISABLE_SPIRV_OPTIMIZER(SettingKeys.disable_spirv_optimizer(), Settings.SECTION_RENDERER, true),
    PLUGIN_LOADER(SettingKeys.plugin_loader(), Settings.SECTION_SYSTEM, false),
    ALLOW_PLUGIN_LOADER(SettingKeys.allow_plugin_loader(), Settings.SECTION_SYSTEM, true),
    SWAP_SCREEN(SettingKeys.swap_screen(), Settings.SECTION_LAYOUT, false),
    INSTANT_DEBUG_LOG(SettingKeys.instant_debug_log(), Settings.SECTION_DEBUG, false),
    // AstraEH: Preserve this config-file option through saves; no settings-screen entry.
    ANDROID_CRASH_REPORTS(SettingKeys.android_crash_reports(), Settings.SECTION_DEBUG, true),
    ENABLE_RPC_SERVER(SettingKeys.enable_rpc_server(), Settings.SECTION_DEBUG, false),
    TOGGLE_UNIQUE_DATA_CONSOLE_TYPE(
        SettingKeys.toggle_unique_data_console_type(),
        Settings.SECTION_DEBUG,
        false
    ),
    SWAP_EYES_3D(SettingKeys.swap_eyes_3d(), Settings.SECTION_RENDERER, false),
    PERF_OVERLAY_ENABLE(SettingKeys.performance_overlay_enable(), Settings.SECTION_LAYOUT, false),
    PERF_OVERLAY_SHOW_FPS(
        SettingKeys.performance_overlay_show_fps(),
        Settings.SECTION_LAYOUT,
        true
    ),
    PERF_OVERLAY_SHOW_FRAMETIME(
        SettingKeys.performance_overlay_show_frame_time(),
        Settings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_SPEED(
        SettingKeys.performance_overlay_show_speed(),
        Settings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_APP_RAM_USAGE(
        SettingKeys.performance_overlay_show_app_ram_usage(),
        Settings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_AVAILABLE_RAM(
        SettingKeys.performance_overlay_show_available_ram(),
        Settings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_BATTERY_TEMP(
        SettingKeys.performance_overlay_show_battery_temp(),
        Settings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_BACKGROUND(
        SettingKeys.performance_overlay_background(),
        Settings.SECTION_LAYOUT,
        false
    ),
    DELAY_START_LLE_MODULES(
        SettingKeys.delay_start_for_lle_modules(),
        Settings.SECTION_DEBUG,
        true
    ),
    DETERMINISTIC_ASYNC_OPERATIONS(
        SettingKeys.deterministic_async_operations(),
        Settings.SECTION_DEBUG,
        false
    ),
    REQUIRED_ONLINE_LLE_MODULES(
        SettingKeys.enable_required_online_lle_modules(),
        Settings.SECTION_SYSTEM,
        false
    ),
    LLE_APPLETS(SettingKeys.lle_applets(), Settings.SECTION_SYSTEM, false),
    NEW_3DS(SettingKeys.is_new_3ds(), Settings.SECTION_SYSTEM, true),
    LINEAR_FILTERING(SettingKeys.filter_mode(), Settings.SECTION_RENDERER, true),
    SHADERS_ACCURATE_MUL(SettingKeys.shaders_accurate_mul(), Settings.SECTION_RENDERER, false),
    DISK_SHADER_CACHE(SettingKeys.use_disk_shader_cache(), Settings.SECTION_RENDERER, true),
    DUMP_TEXTURES(SettingKeys.dump_textures(), Settings.SECTION_UTILITY, false),
    CUSTOM_TEXTURES(SettingKeys.custom_textures(), Settings.SECTION_UTILITY, false),
    ASYNC_CUSTOM_LOADING(SettingKeys.async_custom_loading(), Settings.SECTION_UTILITY, true),
    PRELOAD_TEXTURES(SettingKeys.preload_textures(), Settings.SECTION_UTILITY, false),
    ENABLE_AUDIO_STRETCHING(SettingKeys.enable_audio_stretching(), Settings.SECTION_AUDIO, true),
    ENABLE_REALTIME_AUDIO(SettingKeys.enable_realtime_audio(), Settings.SECTION_AUDIO, false),
    SIMULATE_HEADPHONES_PLUGGED(
        SettingKeys.simulate_headphones_plugged(),
        Settings.SECTION_AUDIO,
        false
    ),
    CPU_JIT(SettingKeys.use_cpu_jit(), Settings.SECTION_CORE, true),
    HW_SHADER(SettingKeys.use_hw_shader(), Settings.SECTION_RENDERER, true),
    SHADER_JIT(SettingKeys.use_shader_jit(), Settings.SECTION_RENDERER, true),
    VSYNC(SettingKeys.use_vsync(), Settings.SECTION_RENDERER, false),
    USE_SKIP_DUPLICATE_FRAMES(
        SettingKeys.use_skip_duplicate_frames(),
        Settings.SECTION_RENDERER,
        false
    ),
    USE_FRAME_LIMIT(SettingKeys.use_frame_limit(), Settings.SECTION_RENDERER, true),
    DEBUG_RENDERER(SettingKeys.renderer_debug(), Settings.SECTION_DEBUG, false),
    DISABLE_RIGHT_EYE_RENDER(
        SettingKeys.disable_right_eye_render(),
        Settings.SECTION_RENDERER,
        false
    ),
    USE_ARTIC_BASE_CONTROLLER(
        SettingKeys.use_artic_base_controller(),
        Settings.SECTION_CONTROLS,
        false
    ),
    UPRIGHT_SCREEN(SettingKeys.upright_screen(), Settings.SECTION_LAYOUT, false),
    COMPRESS_INSTALLED_CIA_CONTENT(
        SettingKeys.compress_cia_installs(),
        Settings.SECTION_STORAGE,
        false
    ),
    ASYNC_FS_OPERATIONS(SettingKeys.async_fs_operations(), Settings.SECTION_STORAGE, true),
    ANDROID_HIDE_IMAGES(SettingKeys.android_hide_images(), Settings.SECTION_MISC, false),
    APPLY_REGION_FREE_PATCH(SettingKeys.apply_region_free_patch(), Settings.SECTION_SYSTEM, true),
    USE_INTEGER_SCALING(SettingKeys.use_integer_scaling(), Settings.SECTION_RENDERER, false),
    ENABLE_SECONDARY_DISPLAY(SettingKeys.enable_secondary_display(), Settings.SECTION_LAYOUT, true),
    SIMULATE_3DS_GPU_TIMINGS(
        SettingKeys.simulate_3ds_gpu_timings(),
        Settings.SECTION_RENDERER,
        false
    ),
    CHECK_FOR_UPDATES(SettingKeys.check_for_update_on_start(), Settings.SECTION_MISC, true);

    override var boolean: Boolean = defaultValue

    override val valueAsString: String
        get() = boolean.toString()

    override val isRuntimeEditable: Boolean
        get() {
            for (setting in NOT_RUNTIME_EDITABLE) {
                if (setting == this) {
                    return false
                }
            }
            return true
        }

    companion object {
        private val NOT_RUNTIME_EDITABLE = listOf(
            // CodexAstraLocal: Match native renderer/worker lifetimes; edits need a new game launch.
            UBERHAR_HYBRID_TEV,
            UBERHAR_FORCE_TEV,
            UBERHAR_CPU_VERTEX_BRIDGE,
            UBERHAR_CPU_VERTEX_ONLY,
            UBERHAR_NATIVE_FULL_FRAGMENTS,
            UBERHAR_CPU_PREFIX_OVERLAP,
            UBERHAR_PARALLEL_TEXTURE_DECODE,
            UBERHAR_CPU_EXECUTION_CENSUS,
            TURNIP_PASS_END_FLUSH,
            UBERHAR_OPTIMIZE_GENERIC_SHADERS,
            // CodexAstraLocal: Match native .57 launch-owner locks.
            UBERHAR_CHANGED_STATE_ONLY,
            UBERHAR_REUSE_CONTAINED_PASS,
            UBERHAR_TYPED_PRIMITIVE_APPEND,
            UBERHAR_INCLUDE_SCISSOR,
            UBERHAR_ADAPTIVE_CPU_WORKERS,
            UBERHAR_GPU_OBSERVER,
            UBERHAR_FRAME_WORKLOAD_CENSUS,
            UBERHAR_PREPARE_DISPLAYED_EYE_ONLY,
            UBERHAR_EARLY_RIGHT_EYE,
            UBERHAR_FAST_VERTEX_CONVERSION,
            UBERHAR_VERTEX_MEMO,
            UBERHAR_WORKER_CPU_UPLOAD,
            UBERHAR_CPU_CLIP_REJECT,
            UBERHAR_FULL_PIPELINE_BANK_16,
            UBERHAR_PRUNE_SPIRV,
            UBERHAR_INDEXED_CPU_OUTPUT,
            UBERHAR_MONO_GUEST_3D,
            SPIRV_SHADER_GEN,
            DISABLE_SPIRV_OPTIMIZER,
            HW_SHADER,
            SHADER_JIT,
            PLUGIN_LOADER,
            ALLOW_PLUGIN_LOADER,
            ASYNC_SHADERS,
            DELAY_START_LLE_MODULES,
            DETERMINISTIC_ASYNC_OPERATIONS,
            REQUIRED_ONLINE_LLE_MODULES,
            NEW_3DS,
            LLE_APPLETS,
            TOGGLE_UNIQUE_DATA_CONSOLE_TYPE,
            VSYNC,
            DEBUG_RENDERER,
            CPU_JIT,
            ASYNC_CUSTOM_LOADING,
            SHADERS_ACCURATE_MUL,
            USE_ARTIC_BASE_CONTROLLER,
            COMPRESS_INSTALLED_CIA_CONTENT,
            ASYNC_FS_OPERATIONS,
            ANDROID_HIDE_IMAGES,
            PERF_OVERLAY_ENABLE, // Works in overlay options, but not from the settings menu
            APPLY_REGION_FREE_PATCH,
            EXPAND_TO_CUTOUT_AREA
        )

        fun from(key: String): BooleanSetting? =
            BooleanSetting.values().firstOrNull { it.key == key }

        fun clear() = BooleanSetting.values().forEach { it.boolean = it.defaultValue }
    }
}

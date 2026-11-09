// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R

// CodexAstraLocal: One catalog keeps quick labels, persisted keys and default values
// aligned. Backend owners implement each setting; no placeholder control is listed.
enum class TestingExperiment(val setting: BooleanSetting, val title: Int, val description: Int) {
    UBERHAR_CHANGED_STATE_ONLY(BooleanSetting.UBERHAR_CHANGED_STATE_ONLY, R.string.uberhar_changed_state_only, R.string.uberhar_changed_state_only_description),
    UBERHAR_REUSE_CONTAINED_PASS(BooleanSetting.UBERHAR_REUSE_CONTAINED_PASS, R.string.uberhar_reuse_contained_pass, R.string.uberhar_reuse_contained_pass_description),
    UBERHAR_TYPED_PRIMITIVE_APPEND(BooleanSetting.UBERHAR_TYPED_PRIMITIVE_APPEND, R.string.uberhar_typed_primitive_append, R.string.uberhar_typed_primitive_append_description),
    UBERHAR_INCLUDE_SCISSOR(BooleanSetting.UBERHAR_INCLUDE_SCISSOR, R.string.uberhar_include_scissor, R.string.uberhar_include_scissor_description),
    UBERHAR_ADAPTIVE_CPU_WORKERS(BooleanSetting.UBERHAR_ADAPTIVE_CPU_WORKERS, R.string.uberhar_adaptive_cpu_workers, R.string.uberhar_adaptive_cpu_workers_description),
    UBERHAR_GPU_OBSERVER(BooleanSetting.UBERHAR_GPU_OBSERVER, R.string.uberhar_gpu_observer, R.string.uberhar_gpu_observer_description),
    UBERHAR_FRAME_WORKLOAD_CENSUS(BooleanSetting.UBERHAR_FRAME_WORKLOAD_CENSUS, R.string.uberhar_frame_workload_census, R.string.uberhar_frame_workload_census_description),
    UBERHAR_PREPARE_DISPLAYED_EYE_ONLY(BooleanSetting.UBERHAR_PREPARE_DISPLAYED_EYE_ONLY, R.string.uberhar_prepare_displayed_eye_only, R.string.uberhar_prepare_displayed_eye_only_description),
    UBERHAR_EARLY_RIGHT_EYE(BooleanSetting.UBERHAR_EARLY_RIGHT_EYE, R.string.uberhar_early_right_eye, R.string.uberhar_early_right_eye_description),
    UBERHAR_FAST_VERTEX_CONVERSION(BooleanSetting.UBERHAR_FAST_VERTEX_CONVERSION, R.string.uberhar_fast_vertex_conversion, R.string.uberhar_fast_vertex_conversion_description),
    UBERHAR_VERTEX_MEMO(BooleanSetting.UBERHAR_VERTEX_MEMO, R.string.uberhar_vertex_memo, R.string.uberhar_vertex_memo_description),
    UBERHAR_WORKER_CPU_UPLOAD(BooleanSetting.UBERHAR_WORKER_CPU_UPLOAD, R.string.uberhar_worker_cpu_upload, R.string.uberhar_worker_cpu_upload_description),
    UBERHAR_CPU_CLIP_REJECT(BooleanSetting.UBERHAR_CPU_CLIP_REJECT, R.string.uberhar_cpu_clip_reject, R.string.uberhar_cpu_clip_reject_description),
    UBERHAR_FULL_PIPELINE_BANK_16(BooleanSetting.UBERHAR_FULL_PIPELINE_BANK_16, R.string.uberhar_full_pipeline_bank_16, R.string.uberhar_full_pipeline_bank_16_description),
    UBERHAR_PRUNE_SPIRV(BooleanSetting.UBERHAR_PRUNE_SPIRV, R.string.uberhar_prune_spirv, R.string.uberhar_prune_spirv_description),
    UBERHAR_INDEXED_CPU_OUTPUT(BooleanSetting.UBERHAR_INDEXED_CPU_OUTPUT, R.string.uberhar_indexed_cpu_output, R.string.uberhar_indexed_cpu_output_description),
    UBERHAR_MONO_GUEST_3D(BooleanSetting.UBERHAR_MONO_GUEST_3D, R.string.uberhar_mono_guest_3d, R.string.uberhar_mono_guest_3d_description),
    OPTIMIZE_GENERIC_SHADERS(BooleanSetting.UBERHAR_OPTIMIZE_GENERIC_SHADERS,
        R.string.uberhar_optimize_generic_shaders, R.string.uberhar_optimize_generic_shaders_description);
    val key get() = setting.key
    val defaultValue get() = setting.defaultValue
}

// CodexAstraLocal: Testing is a sparse layer in each scope; ordinary title choices
// outrank global defaults and only explicitly selected title Testing keys supersede them.
object TestingSettings {
    val available get() = NativeLibrary.isAlphaTestingAvailable()
    var enabled = false
    private var mode = UberharTestMode.CUSTOM
    private var cpuVertices = false
    // CodexAstraLocal: Custom has no matching Testing chip; never relabel it as Native.
    var preset = -1
        set(value) {
            field = value
            if (value in 0..2) {
                mode = if (value == 0) UberharTestMode.NATIVE else UberharTestMode.AUTOMATIC
                cpuVertices = value == 1
            }
        }
    var resolution = 1
    var disableRightEyeRender = false
    var renderThreadDelayUs = 0
    var stereoDepth = 0
    private val experiments = TestingExperiment.entries.associateWith { it.defaultValue }.toMutableMap()
    var gameId: String? = null
        private set

    private val section get() = gameId?.let { "Testing.$it" } ?: "Testing"
    private val keys = setOf("enabled", "preset", "resolution", "disable_right_eye_render",
        "delay_game_render_thread_us", "factor_3d") + TestingExperiment.entries.map { it.key }
    private fun parsedBoolean(value: String?): Boolean? = when (value?.lowercase()) {
        "true", "yes", "on", "1" -> true
        "false", "no", "off", "0" -> false
        else -> null
    }
    private fun boolean(value: String?, fallback: Boolean): Boolean = parsedBoolean(value) ?: fallback

    private data class Choices(
        var enabled: Boolean = false,
        var mode: UberharTestMode = UberharTestMode.CUSTOM,
        var cpuVertices: Boolean = false,
        var resolution: Int = 1,
        var rightEye: Boolean = false,
        var delay: Int = 0,
        var depth: Int = 0,
        val experiments: MutableMap<TestingExperiment, Boolean> =
            TestingExperiment.entries.associateWith { it.defaultValue }.toMutableMap()
    )

    // CodexAstraLocal: Raw reads must not reload shared setting enums and discard unsaved UI edits.
    private fun resolve(titleId: String?, settings: Settings? = null): Choices {
        // CodexAstraLocal: Retire legacy quick keys before either ordinary layer is read.
        if (available) SettingsFile.migrateRetiredTestingSettings(titleId)
        val global = SettingsFile.readRawSettingsFile()["Renderer"]?.toMutableMap() ?: mutableMapOf()
        val title = titleId?.let {
            SettingsFile.readRawSettingsFile(it)["Renderer"]?.toMutableMap()
        } ?: mutableMapOf()
        // CodexAstraLocal: Ordinary settings constraints include draft mode/CPU/backend edits.
        // Per-title reset removes the working override rather than reviving its saved disk value.
        if (settings != null) {
            for (setting in listOf(IntSetting.UBERHAR_TEST_MODE, IntSetting.GRAPHICS_API,
                BooleanSetting.UBERHAR_CPU_VERTEX_ONLY)) {
                if (!settings.isPerGame) global[setting.key!!] = setting.valueAsString
                else if (settings.isPerGameOverride(setting)) title[setting.key!!] = setting.valueAsString
                else title.remove(setting.key)
            }
        }
        val testing = SettingsFile.readTestingFile()
        val titleSection = titleId?.let { "Testing.$it" }
        val globalEnabled = boolean(testing.get("Testing", "enabled"), false)
        val result = Choices(enabled = available &&
            boolean(titleSection?.let { testing.get(it, "enabled") }, globalEnabled))
        fun ordinary(values: Map<String, String>, isTitle: Boolean) {
            values["uberhar_test_mode"]?.toIntOrNull()?.let { result.mode = UberharTestMode.from(it) }
            values["uberhar_cpu_vertex_only"]?.let {
                result.cpuVertices = boolean(it, result.cpuVertices)
            }
            // CodexAstraLocal: An explicit title backend defeats an inherited profile, while an
            // explicit title mode remains the owner's later profile selection, matching native.
            if (isTitle && "uberhar_test_mode" !in values &&
                values["graphics_api"]?.toIntOrNull() in 0..1) result.mode = UberharTestMode.CUSTOM
            values["resolution_factor"]?.toIntOrNull()?.let { result.resolution = it.coerceIn(0, 18) }
            values["disable_right_eye_render"]?.let { result.rightEye = boolean(it, result.rightEye) }
            values["delay_game_render_thread_us"]?.toIntOrNull()?.let { result.delay = it.coerceIn(0, 65000) }
            values["factor_3d"]?.toIntOrNull()?.let { result.depth = it }
            TestingExperiment.entries.forEach { choice ->
                values[choice.key]?.let { result.experiments[choice] =
                    boolean(it, result.experiments.getValue(choice)) }
            }
        }
        fun overlay(section: String) {
            testing.get(section, "preset")?.toIntOrNull()?.takeIf { it in 0..2 }?.let {
                result.mode = if (it == 0) UberharTestMode.NATIVE else UberharTestMode.AUTOMATIC
                result.cpuVertices = it == 1
            }
            testing.get(section, "resolution")?.toIntOrNull()?.takeIf { it == 2 || it == 4 }
                ?.let { result.resolution = it }
            testing.get(section, "disable_right_eye_render")?.let { result.rightEye = boolean(it, false) }
            testing.get(section, "delay_game_render_thread_us")?.let {
                result.delay = it.toIntOrNull()?.takeIf { value -> value in 0..16000 } ?: 0
            }
            testing.get(section, "factor_3d")?.let {
                result.depth = it.toIntOrNull()?.takeIf { value -> value in 0..1 } ?: 0
            }
            TestingExperiment.entries.forEach { choice ->
                testing.get(section, choice.key)?.let { result.experiments[choice] =
                    boolean(it, result.experiments.getValue(choice)) }
            }
        }
        ordinary(global, false)
        if (result.enabled) overlay("Testing")
        ordinary(title, true)
        if (result.enabled && titleSection != null) overlay(titleSection)
        return result
    }

    fun load(titleId: String = gameId ?: "") {
        gameId = SettingsFile.normalizeGameId(titleId)
        val values = resolve(gameId)
        enabled = values.enabled
        mode = values.mode
        cpuVertices = values.cpuVertices
        preset = when (mode) {
            UberharTestMode.NATIVE -> 0
            UberharTestMode.AUTOMATIC -> if (cpuVertices) 1 else 2
            else -> -1
        }
        resolution = values.resolution
        disableRightEyeRender = values.rightEye
        renderThreadDelayUs = values.delay
        stereoDepth = values.depth
        experiments.putAll(values.experiments)
    }

    fun save(editedKey: String): Boolean {
        // CodexAstraLocal: A single edit must not pin inherited values, including the master switch.
        if (!available || editedKey !in keys) return false
        val value: Any = when (editedKey) {
            "enabled" -> enabled
            "preset" -> preset.takeIf { it in 0..2 } ?: return false
            "resolution" -> if (resolution == 4) 4 else 2
            "disable_right_eye_render" -> disableRightEyeRender
            "delay_game_render_thread_us" -> renderThreadDelayUs.coerceIn(0, 16000)
            "factor_3d" -> stereoDepth.takeIf { it in 0..1 } ?: 0
            else -> TestingExperiment.entries.firstOrNull { it.key == editedKey }
                ?.let { experiments.getValue(it) } ?: return false
        }
        val file = SettingsFile.readTestingFile()
        file.put(section, editedKey, value)
        return SettingsFile.saveTestingFile(file)
    }

    fun resolvedMode(settings: Settings? = null): UberharTestMode =
        if (settings == null) mode else resolve(settings.gameId, settings).mode

    fun setExperiment(choice: TestingExperiment, value: Boolean) { experiments[choice] = value }
    fun experiment(choice: TestingExperiment): Boolean = experiments.getValue(choice)
    // CodexAstraLocal: Running-title controls display the launch-owned native value,
    // while global Options and other titles keep their own saved scope.
    fun effectiveExperiment(choice: TestingExperiment): Boolean = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveTestingBoolean(choice.key)
    } else experiment(choice)

    // CodexAstraLocal: Only an explicit editor for the running title displays its launch snapshot.
    private fun showsRunningTitle() = gameId != null && NativeLibrary.isRunning() &&
        gameId == SettingsFile.normalizeGameId("%016X".format(NativeLibrary.getRunningTitleId()))

    fun requestedRightEyeRenderDisabled(): Boolean = if (showsRunningTitle()) {
        NativeLibrary.getRequestedRightEyeRenderDisabled()
    } else disableRightEyeRender

    fun effectiveRenderThreadDelayUs(): Int = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveRenderThreadDelayUs()
    } else renderThreadDelayUs

    fun effectiveStereoDepth(): Int = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveStereoDepth()
    } else stereoDepth

    fun useGlobalChoices(): Boolean {
        if (!available) return false
        if (gameId == null) return true
        val file = SettingsFile.readTestingFile()
        // CodexAstraLocal: Remove only owned title keys; unrelated and retirement metadata survives.
        keys.forEach { file[section]?.remove(it) }
        val saved = SettingsFile.saveTestingFile(file)
        if (saved) load()
        return saved
    }
}

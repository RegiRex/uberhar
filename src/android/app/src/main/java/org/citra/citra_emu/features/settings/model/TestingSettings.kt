// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.NativeLibrary

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
    var hardwareShaders = true
    var renderThreadDelayUs = 0
    var linearFiltering = true
    var stereoDepth = 0
    var gameId: String? = null
        private set

    private val section get() = gameId?.let { "Testing.$it" } ?: "Testing"
    private val keys = setOf("enabled", "preset", "resolution", "disable_right_eye_render",
        "use_hw_shader", "delay_game_render_thread_us", "filter_mode", "factor_3d")
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
        var hardware: Boolean? = null,
        var delay: Int = 0,
        var filtering: Boolean = true,
        var depth: Int = 0
    )

    // CodexAstraLocal: Raw reads must not reload shared setting enums and discard unsaved UI edits.
    private fun resolve(titleId: String?, settings: Settings? = null): Choices {
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
            // CodexAstraLocal: Invalid ordinary values inherit; defer the profile default until
            // all mode layers resolve, so an intermediate mode cannot pin Hardware Shaders.
            parsedBoolean(values["use_hw_shader"])?.let { result.hardware = it }
            values["delay_game_render_thread_us"]?.toIntOrNull()?.let { result.delay = it.coerceIn(0, 65000) }
            values["filter_mode"]?.let { result.filtering = boolean(it, result.filtering) }
            values["factor_3d"]?.toIntOrNull()?.let { result.depth = it }
        }
        fun overlay(section: String) {
            testing.get(section, "preset")?.toIntOrNull()?.takeIf { it in 0..2 }?.let {
                result.mode = if (it == 0) UberharTestMode.NATIVE else UberharTestMode.AUTOMATIC
                result.cpuVertices = it == 1
            }
            testing.get(section, "resolution")?.toIntOrNull()?.takeIf { it == 2 || it == 4 }
                ?.let { result.resolution = it }
            testing.get(section, "disable_right_eye_render")?.let { result.rightEye = boolean(it, false) }
            testing.get(section, "use_hw_shader")?.let { result.hardware = boolean(it, false) }
            testing.get(section, "delay_game_render_thread_us")?.let {
                result.delay = it.toIntOrNull()?.takeIf { value -> value in 0..16000 } ?: 0
            }
            testing.get(section, "filter_mode")?.let { result.filtering = boolean(it, true) }
            testing.get(section, "factor_3d")?.let {
                result.depth = it.toIntOrNull()?.takeIf { value -> value in 0..1 } ?: 0
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
        hardwareShaders = values.hardware ?: (mode == UberharTestMode.CUSTOM)
        renderThreadDelayUs = values.delay
        linearFiltering = values.filtering
        stereoDepth = values.depth
    }

    fun save(editedKey: String): Boolean {
        // CodexAstraLocal: A single edit must not pin inherited values, including the master switch.
        if (!available || editedKey !in keys) return false
        val value: Any = when (editedKey) {
            "enabled" -> enabled
            "preset" -> preset.takeIf { it in 0..2 } ?: return false
            "resolution" -> if (resolution == 4) 4 else 2
            "disable_right_eye_render" -> disableRightEyeRender
            "use_hw_shader" -> hardwareShaders
            "delay_game_render_thread_us" -> renderThreadDelayUs.coerceIn(0, 16000)
            "filter_mode" -> linearFiltering
            "factor_3d" -> stereoDepth.takeIf { it in 0..1 } ?: 0
            else -> return false
        }
        val file = SettingsFile.readTestingFile()
        file.put(section, editedKey, value)
        return SettingsFile.saveTestingFile(file)
    }

    fun resolvedMode(settings: Settings? = null): UberharTestMode =
        if (settings == null) mode else resolve(settings.gameId, settings).mode

    fun hardwareShadersConflictsWithCpuVertices(settings: Settings? = null): Boolean {
        val choices = settings?.let { resolve(it.gameId, it) }
        return (choices?.mode ?: mode) == UberharTestMode.AUTOMATIC &&
            (choices?.cpuVertices ?: cpuVertices)
    }

    // CodexAstraLocal: Only an explicit editor for the running title displays its launch snapshot.
    private fun showsRunningTitle() = gameId != null && NativeLibrary.isRunning() &&
        gameId == SettingsFile.normalizeGameId("%016X".format(NativeLibrary.getRunningTitleId()))

    fun requestedRightEyeRenderDisabled(): Boolean = if (showsRunningTitle()) {
        NativeLibrary.getRequestedRightEyeRenderDisabled()
    } else disableRightEyeRender

    fun effectiveHardwareShaders(): Boolean = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveHardwareShaders()
    } else hardwareShaders && !hardwareShadersConflictsWithCpuVertices()

    fun effectiveRenderThreadDelayUs(): Int = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveRenderThreadDelayUs()
    } else renderThreadDelayUs

    fun effectiveLinearFiltering(): Boolean = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveLinearFiltering()
    } else linearFiltering

    fun effectiveStereoDepth(): Int = if (showsRunningTitle()) {
        NativeLibrary.getEffectiveStereoDepth()
    } else stereoDepth

    fun useGlobalChoices(): Boolean {
        if (!available) return false
        if (gameId == null) return true
        val file = SettingsFile.readTestingFile()
        // CodexAstraLocal: Remove only the eight owned title keys; future metadata survives.
        keys.forEach { file[section]?.remove(it) }
        val saved = SettingsFile.saveTestingFile(file)
        if (saved) load()
        return saved
    }
}

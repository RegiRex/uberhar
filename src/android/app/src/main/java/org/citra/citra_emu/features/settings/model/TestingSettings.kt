// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.NativeLibrary

// CodexAstraLocal: A global master enables a separate overlay. Title choices inherit
// Testing defaults without writing into the game's ordinary saved preferences.
object TestingSettings {
    // CodexAstraLocal: One compiled release.beta.alpha gate also protects the native overlay.
    val available get() = NativeLibrary.isAlphaTestingAvailable()
    var enabled = false
    var preset = 0
    var resolution = 2
    // CodexAstraLocal: Options edits global Testing defaults; title overrides inherit until selected.
    var disableRightEyeRender = false
    var hardwareShaders = false
    var renderThreadDelayUs = 0
    // CodexAstraLocal: Unselected experiments retain the standard profile defaults.
    var linearFiltering = true
    var stereoDepth = 0
    var gameId: String? = null
        private set

    private val section get() = gameId?.let { "Testing.$it" } ?: "Testing"

    fun load(titleId: String = gameId ?: "") {
        gameId = SettingsFile.normalizeGameId(titleId)
        val file = SettingsFile.readTestingFile()
        enabled = available && (file.get("Testing", "enabled")?.toBoolean() ?: false)
        val globalPreset = file.get("Testing", "preset")?.toIntOrNull()?.takeIf { it in 0..2 } ?: 0
        val globalResolution = file.get("Testing", "resolution")?.toIntOrNull()?.takeIf {
            it == 2 || it == 4
        } ?: 2
        preset = file.get(section, "preset")?.toIntOrNull()?.takeIf { it in 0..2 } ?: globalPreset
        resolution = file.get(section, "resolution")?.toIntOrNull()?.takeIf {
            it == 2 || it == 4
        } ?: globalResolution
        // CodexAstraLocal: Missing title keys inherit global experiments; malformed explicit values
        // keep the documented baseline, matching native parsing without touching Custom settings.
        fun experimentValue(key: String) = file.get(section, key) ?: file.get("Testing", key)
        disableRightEyeRender =
            experimentValue("disable_right_eye_render")?.lowercase() in setOf("true", "yes", "on", "1")
        hardwareShaders =
            experimentValue("use_hw_shader")?.lowercase() in setOf("true", "yes", "on", "1")
        renderThreadDelayUs = experimentValue("delay_game_render_thread_us")?.toIntOrNull()
            ?.takeIf { it in 0..16000 } ?: 0
        linearFiltering =
            experimentValue("filter_mode")?.lowercase() !in setOf("false", "no", "off", "0")
        stereoDepth = experimentValue("factor_3d")?.toIntOrNull()?.takeIf { it in 0..1 } ?: 0
    }

    fun save(): Boolean {
        // CodexAstraLocal: Beta/full builds retain alpha preferences on disk without using/editing them.
        if (!available) return false
        val file = SettingsFile.readTestingFile()
        file.put("Testing", "enabled", enabled)
        file.put(section, "preset", preset.coerceIn(0, 2))
        file.put(section, "resolution", if (resolution == 4) 4 else 2)
        // CodexAstraLocal: Global and title editors share the separate, temporary Testing store.
        file.put(section, "disable_right_eye_render", disableRightEyeRender)
        file.put(section, "use_hw_shader", hardwareShaders)
        file.put(section, "delay_game_render_thread_us", renderThreadDelayUs.coerceIn(0, 16000))
        file.put(section, "filter_mode", linearFiltering)
        file.put(section, "factor_3d", stereoDepth.takeIf { it in 0..1 } ?: 0)
        return SettingsFile.saveTestingFile(file)
    }

    // CodexAstraLocal: Running controls show the native requested flag, not pending file edits.
    // Compatibility exclusions and heuristic decisions can still prevent suppression.
    fun requestedRightEyeRenderDisabled(): Boolean = if (NativeLibrary.isRunning()) {
        NativeLibrary.getRequestedRightEyeRenderDisabled()
    } else {
        enabled && disableRightEyeRender
    }

    // CodexAstraLocal: CPU-only Combo vetoes GPU vertices; live resolution edits keep launch values.
    fun effectiveHardwareShaders(): Boolean = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveHardwareShaders()
    } else enabled && hardwareShaders && preset != 1

    fun effectiveRenderThreadDelayUs(): Int = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveRenderThreadDelayUs()
    } else if (enabled) renderThreadDelayUs else 0

    // CodexAstraLocal: Running displays use launch-latched native values, never pending disk edits.
    fun effectiveLinearFiltering(): Boolean = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveLinearFiltering()
    } else if (enabled) linearFiltering else true

    fun effectiveStereoDepth(): Int = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveStereoDepth()
    } else if (enabled) stereoDepth else 0

    // CodexAstraLocal: Removing title keys resumes ongoing inheritance; no normal settings are touched.
    fun useGlobalChoices(): Boolean {
        if (!available) return false
        if (gameId == null) return true
        val file = SettingsFile.readTestingFile()
        file[section]?.remove("preset")
        file[section]?.remove("resolution")
        // CodexAstraLocal: Clearing title experiments restores the current global Testing choices.
        file[section]?.remove("disable_right_eye_render")
        file[section]?.remove("use_hw_shader")
        file[section]?.remove("delay_game_render_thread_us")
        file[section]?.remove("filter_mode")
        file[section]?.remove("factor_3d")
        val saved = SettingsFile.saveTestingFile(file)
        if (saved) load()
        return saved
    }
}

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
    // CodexAstraLocal: This experimental override never inherits global defaults.
    var disableRightEyeRender = false
    // CodexAstraLocal: Experiments are explicit per-title requests, never inherited globally.
    var hardwareShaders = false
    var renderThreadDelayUs = 0
    // CodexAstraLocal: Match standard profile defaults until a title explicitly selects a comparison.
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
        // CodexAstraLocal: Invalid/missing title choices fail OFF, matching native INI parsing.
        disableRightEyeRender = gameId != null &&
            file.get(section, "disable_right_eye_render")?.lowercase() in setOf("true", "yes", "on", "1")
        hardwareShaders = gameId != null &&
            file.get(section, "use_hw_shader")?.lowercase() in setOf("true", "yes", "on", "1")
        renderThreadDelayUs = if (gameId != null) {
            file.get(section, "delay_game_render_thread_us")?.toIntOrNull()?.takeIf { it in 0..16000 } ?: 0
        } else 0
        // CodexAstraLocal: Global or malformed experiment values cannot change another title's baseline.
        linearFiltering = gameId == null ||
            file.get(section, "filter_mode")?.lowercase() !in setOf("false", "no", "off", "0")
        stereoDepth = if (gameId != null) {
            file.get(section, "factor_3d")?.toIntOrNull()?.takeIf { it in 0..1 } ?: 0
        } else 0
    }

    fun save(): Boolean {
        // CodexAstraLocal: Beta/full builds retain alpha preferences on disk without using/editing them.
        if (!available) return false
        val file = SettingsFile.readTestingFile()
        file.put("Testing", "enabled", enabled)
        file.put(section, "preset", preset.coerceIn(0, 2))
        file.put(section, "resolution", if (resolution == 4) 4 else 2)
        // CodexAstraLocal: Saving global Testing defaults must never enable this for every game.
        if (gameId != null) {
            file.put(section, "disable_right_eye_render", disableRightEyeRender)
            file.put(section, "use_hw_shader", hardwareShaders)
            file.put(section, "delay_game_render_thread_us", renderThreadDelayUs.coerceIn(0, 16000))
            file.put(section, "filter_mode", linearFiltering)
            file.put(section, "factor_3d", stereoDepth.takeIf { it in 0..1 } ?: 0)
        }
        return SettingsFile.saveTestingFile(file)
    }

    // CodexAstraLocal: Running controls show the native requested flag, not pending file edits.
    // Compatibility exclusions and heuristic decisions can still prevent suppression.
    fun requestedRightEyeRenderDisabled(): Boolean = if (NativeLibrary.isRunning()) {
        NativeLibrary.getRequestedRightEyeRenderDisabled()
    } else {
        enabled && gameId != null && disableRightEyeRender
    }

    // CodexAstraLocal: CPU-only Combo vetoes GPU vertices; live resolution edits keep launch values.
    fun effectiveHardwareShaders(): Boolean = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveHardwareShaders()
    } else enabled && gameId != null && hardwareShaders && preset != 1

    fun effectiveRenderThreadDelayUs(): Int = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveRenderThreadDelayUs()
    } else if (enabled && gameId != null) renderThreadDelayUs else 0

    // CodexAstraLocal: Running displays use launch-latched native values, never pending disk edits.
    fun effectiveLinearFiltering(): Boolean = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveLinearFiltering()
    } else if (enabled && gameId != null) linearFiltering else true

    fun effectiveStereoDepth(): Int = if (NativeLibrary.isRunning()) {
        NativeLibrary.getEffectiveStereoDepth()
    } else if (enabled && gameId != null) stereoDepth else 0

    // CodexAstraLocal: Removing title keys resumes ongoing inheritance; no normal settings are touched.
    fun useGlobalChoices(): Boolean {
        if (!available) return false
        if (gameId == null) return true
        val file = SettingsFile.readTestingFile()
        file[section]?.remove("preset")
        file[section]?.remove("resolution")
        // CodexAstraLocal: Reset each title-only experiment to its standard profile baseline.
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

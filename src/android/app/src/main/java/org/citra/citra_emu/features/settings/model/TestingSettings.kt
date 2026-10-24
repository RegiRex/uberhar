// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.utils.SettingsFile

// CodexAstraLocal: A global master enables a separate overlay. Title choices inherit
// Testing defaults without writing into the game's ordinary saved preferences.
object TestingSettings {
    var enabled = false
    var preset = 0
    var resolution = 2
    var gameId: String? = null
        private set

    private val section get() = gameId?.let { "Testing.$it" } ?: "Testing"

    fun load(titleId: String = gameId ?: "") {
        gameId = SettingsFile.normalizeGameId(titleId)
        val file = SettingsFile.readTestingFile()
        enabled = file.get("Testing", "enabled")?.toBoolean() ?: false
        val globalPreset = file.get("Testing", "preset")?.toIntOrNull()?.takeIf { it in 0..2 } ?: 0
        val globalResolution = file.get("Testing", "resolution")?.toIntOrNull()?.takeIf {
            it == 2 || it == 4
        } ?: 2
        preset = file.get(section, "preset")?.toIntOrNull()?.takeIf { it in 0..2 } ?: globalPreset
        resolution = file.get(section, "resolution")?.toIntOrNull()?.takeIf {
            it == 2 || it == 4
        } ?: globalResolution
    }

    fun save(): Boolean {
        val file = SettingsFile.readTestingFile()
        file.put("Testing", "enabled", enabled)
        file.put(section, "preset", preset.coerceIn(0, 2))
        file.put(section, "resolution", if (resolution == 4) 4 else 2)
        return SettingsFile.saveTestingFile(file)
    }

    // CodexAstraLocal: Removing title keys resumes ongoing inheritance; no normal settings are touched.
    fun useGlobalChoices(): Boolean {
        if (gameId == null) return true
        val file = SettingsFile.readTestingFile()
        file[section]?.remove("preset")
        file[section]?.remove("resolution")
        val saved = SettingsFile.saveTestingFile(file)
        if (saved) load()
        return saved
    }
}

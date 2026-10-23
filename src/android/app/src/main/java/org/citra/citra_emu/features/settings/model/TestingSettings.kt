// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.utils.SettingsFile

// CodexAstraLocal: Separate global testing overlay; turning it off reveals untouched title/global values.
object TestingSettings {
    var enabled = false
    var preset = 0
    var resolution = 2

    fun load() {
        val file = SettingsFile.readTestingFile()
        enabled = file.get("Testing", "enabled")?.toBoolean() ?: false
        preset = file.get("Testing", "preset")?.toIntOrNull()?.takeIf { it in 0..2 } ?: 0
        resolution = file.get("Testing", "resolution")?.toIntOrNull()?.takeIf {
            it == 2 || it == 4
        } ?: 2
    }

    fun save(): Boolean {
        val file = SettingsFile.readTestingFile()
        file.put("Testing", "enabled", enabled)
        file.put("Testing", "preset", preset.coerceIn(0, 2))
        file.put("Testing", "resolution", if (resolution == 4) 4 else 2)
        return SettingsFile.saveTestingFile(file)
    }
}

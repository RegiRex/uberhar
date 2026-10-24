// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.utils.SettingsFile

// CodexAstraLocal: Driver exceptions never rewrite the global installed driver or ordinary title INI.
object DriverSettings {
    const val SYSTEM = "system"
    data class Selection(val driver: String, val label: String)

    fun load(titleId: String): Selection? {
        val title = SettingsFile.normalizeGameId(titleId) ?: return null
        val file = SettingsFile.readDriverFile()
        val driver = file.get("Driver.$title", "driver")?.takeIf { it.isNotBlank() } ?: return null
        return Selection(driver, file.get("Driver.$title", "label") ?: driver)
    }

    fun save(titleId: String, selection: Selection?): Boolean {
        val title = SettingsFile.normalizeGameId(titleId) ?: return false
        val file = SettingsFile.readDriverFile()
        val section = "Driver.$title"
        if (selection == null) {
            file[section]?.remove("driver")
            file[section]?.remove("label")
        } else {
            file.put(section, "driver", selection.driver)
            file.put(section, "label", selection.label)
        }
        return SettingsFile.saveDriverFile(file)
    }
}

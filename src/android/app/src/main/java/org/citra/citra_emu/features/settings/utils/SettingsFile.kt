// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.utils

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import java.io.BufferedReader
import java.io.FileNotFoundException
import java.io.IOException
import java.io.InputStreamReader
import java.util.TreeMap
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.FloatSetting
import org.citra.citra_emu.features.settings.model.IntListSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.ScaledFloatSetting
import org.citra.citra_emu.features.settings.model.SettingSection
import org.citra.citra_emu.features.settings.model.Settings.SettingsSectionMap
import org.citra.citra_emu.features.settings.model.StringSetting
import org.citra.citra_emu.features.settings.ui.SettingsActivityView
import org.citra.citra_emu.utils.BiMap
import org.citra.citra_emu.utils.DirectoryInitialization.userDirectory
import org.citra.citra_emu.utils.Log
import org.ini4j.Wini

/**
 * Contains static methods for interacting with .ini files in which settings are stored.
 */
object SettingsFile {
    const val FILE_NAME_CONFIG = "config"

    private var sectionsMap = BiMap<String?, String?>()

    /**
     * Reads a given .ini file from disk and returns it as a HashMap of Settings, themselves
     * effectively a HashMap of key/value settings. If unsuccessful, outputs an error telling why it
     * failed.
     *
     * @param ini          The ini file to load the settings from
     * @param isCustomGame
     * @param view         The current view.
     * @return An Observable that emits a HashMap of the file's contents, then completes.
     */
    fun readFile(
        ini: DocumentFile,
        isCustomGame: Boolean,
        view: SettingsActivityView?,
        accepts: (AbstractSetting) -> Boolean = { true }
    ): HashMap<String, SettingSection?> {
        val sections: HashMap<String, SettingSection?> = SettingsSectionMap()
        var reader: BufferedReader? = null
        try {
            val context: Context = CitraApplication.appContext
            val inputStream = context.contentResolver.openInputStream(ini.uri)
            reader = BufferedReader(InputStreamReader(inputStream))
            var current: SettingSection? = null
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                if (line!!.startsWith("[") && line!!.endsWith("]")) {
                    current = sectionFromLine(line!!, isCustomGame)
                    sections[current.name] = current
                } else if (current != null) {
                    val setting = settingFromLine(line!!, if (isCustomGame) current.name else null, accepts)
                    if (setting != null) {
                        current.putSetting(setting)
                    }
                }
            }
        } catch (e: FileNotFoundException) {
            Log.error("[SettingsFile] File not found: " + ini.uri + e.message)
            view?.onSettingsFileNotFound()
        } catch (e: IOException) {
            Log.error("[SettingsFile] Error reading from: " + ini.uri + e.message)
            view?.onSettingsFileNotFound()
        } finally {
            if (reader != null) {
                try {
                    reader.close()
                } catch (e: IOException) {
                    Log.error("[SettingsFile] Error closing: " + ini.uri + e.message)
                }
            }
        }
        return sections
    }

    fun readFile(fileName: String, view: SettingsActivityView?): HashMap<String, SettingSection?> =
        readFile(getSettingsFile(fileName), false, view)

    fun readFile(fileName: String): HashMap<String, SettingSection?> = readFile(fileName, null)

    /**
     * Reads a given .ini file from disk and returns it as a HashMap of SettingSections, themselves
     * effectively a HashMap of key/value settings. If unsuccessful, outputs an error telling why it
     * failed.
     *
     * @param gameId the id of the game to load it's settings.
     * @param view   The current view.
     */
    fun readCustomGameSettings(
        gameId: String,
        view: SettingsActivityView?,
        accepts: (AbstractSetting) -> Boolean = { true }
    ): HashMap<String, SettingSection?> {
        // CodexAstraLocal: A missing title file means inheritance, not a broken settings screen.
        val ini = getCustomGameSettingsFile(gameId, false) ?: return SettingsSectionMap()
        return readFile(ini, true, view, accepts)
    }

    // CodexAstraLocal: Canonical title IDs prevent path traversal and collisions between launch/editor paths.
    fun normalizeGameId(gameId: String): String? = gameId.trim().takeIf {
        it.matches(Regex("[0-9a-fA-F]{1,16}")) && it.any { c -> c != '0' }
    }?.uppercase()?.padStart(16, '0')

    fun saveCustomGameSettings(
        gameId: String,
        settings: List<AbstractSetting>,
        removedKeys: Set<String>,
        view: SettingsActivityView?
    ) {
        try {
            val ini = getCustomGameSettingsFile(gameId, true)
                ?: throw IOException("Cannot create title settings")
            val resolver = CitraApplication.appContext.contentResolver
            val writer = resolver.openInputStream(ini.uri)!!.use { Wini(it) }
            // CodexAstraLocal: Only requested keys are removed; unrecognized future settings survive.
            for (section in writer.values) removedKeys.forEach(section::remove)
            settings.forEach { writer.put(it.section, it.key, it.valueAsString) }
            resolver.openOutputStream(ini.uri, "wt")!!.use { writer.store(it) }
        } catch (e: Exception) {
            Log.error("[SettingsFile] Error saving title $gameId: ${e.message}")
            view?.showToastMessage(
                CitraApplication.appContext.getString(R.string.error_saving, gameId, e.message), false
            )
        }
    }

    // CodexAstraLocal: Resolve Testing against saved ordinary choices without changing enum-backed
    // editor values. A missing title/config is an empty layer, never a write or model reset.
    fun readRawSettingsFile(gameId: String? = null): Wini = try {
        val ini = if (gameId == null) getOptionalConfigFile(FILE_NAME_CONFIG, false)
            else getCustomGameSettingsFile(gameId, false)
        if (ini == null) Wini() else
            CitraApplication.appContext.contentResolver.openInputStream(ini.uri)!!.use { Wini(it) }
    } catch (e: Exception) {
        Log.error("[SettingsFile] Error reading ordinary settings: ${e.message}")
        Wini()
    }

    // CodexAstraLocal: Independent overlay/driver stores preserve ordinary config and title inheritance.
    fun readTestingFile(): Wini = readAuxiliaryFile("testing")
    fun saveTestingFile(writer: Wini): Boolean = saveAuxiliaryFile("testing", writer)
    fun readDriverFile(): Wini = readAuxiliaryFile("drivers")
    fun saveDriverFile(writer: Wini): Boolean = saveAuxiliaryFile("drivers", writer)

    private fun readAuxiliaryFile(name: String): Wini {
        // CodexAstraLocal: Home Options may be reopened after storage permission loss; show defaults safely.
        return try {
            val ini = getOptionalConfigFile(name, false) ?: return Wini()
            CitraApplication.appContext.contentResolver.openInputStream(ini.uri)!!.use { Wini(it) }
        } catch (e: Exception) {
            Log.error("[SettingsFile] Error reading $name settings: ${e.message}")
            Wini()
        }
    }

    private fun saveAuxiliaryFile(name: String, writer: Wini): Boolean = try {
        val ini = getOptionalConfigFile(name, true)
            ?: throw IOException("Cannot create $name settings")
        CitraApplication.appContext.contentResolver.openOutputStream(ini.uri, "wt")!!.use {
            writer.store(it)
        }
        true
    } catch (e: Exception) {
        Log.error("[SettingsFile] Error saving $name settings: ${e.message}")
        false
    }

    private fun getOptionalConfigFile(name: String, create: Boolean): DocumentFile? {
        val root = DocumentFile.fromTreeUri(CitraApplication.appContext, Uri.parse(userDirectory))
        val directory = root?.findFile("config") ?: return null
        return directory.findFile("$name.ini") ?: if (create) {
            directory.createFile("application/octet-stream", "$name.ini")
        } else null
    }

    /**
     * Saves a Settings HashMap to a given .ini file on disk. If unsuccessful, outputs an error
     * telling why it failed.
     *
     * @param fileName The target filename without a path or extension.
     * @param sections The HashMap containing the Settings we want to serialize.
     * @param view     The current view.
     */
    fun saveFile(
        fileName: String,
        sections: TreeMap<String, SettingSection?>,
        view: SettingsActivityView
    ) {
        val ini = getSettingsFile(fileName)
        try {
            val context: Context = CitraApplication.appContext
            val inputStream = context.contentResolver.openInputStream(ini.uri)
            val writer = Wini(inputStream)
            val keySet: Set<String> = sections.keys
            for (key in keySet) {
                val section = sections[key]
                writeSection(writer, section!!)
            }
            inputStream!!.close()
            val outputStream = context.contentResolver.openOutputStream(ini.uri, "wt")
            writer.store(outputStream)
            outputStream!!.flush()
            outputStream.close()
        } catch (e: Exception) {
            Log.error("[SettingsFile] File not found: $fileName.ini: ${e.message}")
            view.showToastMessage(
                CitraApplication.appContext
                    .getString(R.string.error_saving, fileName, e.message),
                false
            )
        }
    }

    fun saveFile(fileName: String, setting: AbstractSetting) {
        val ini = getSettingsFile(fileName)
        try {
            val context: Context = CitraApplication.appContext
            val inputStream = context.contentResolver.openInputStream(ini.uri)
            val writer = Wini(inputStream)
            writer.put(setting.section, setting.key, setting.valueAsString)
            inputStream!!.close()
            val outputStream = context.contentResolver.openOutputStream(ini.uri, "wt")
            writer.store(outputStream)
            outputStream!!.flush()
            outputStream.close()
        } catch (e: Exception) {
            Log.error("[SettingsFile] File not found: $fileName.ini: ${e.message}")
        }
    }

    private fun mapSectionNameFromIni(generalSectionName: String): String? =
        if (sectionsMap.getForward(generalSectionName) != null) {
            sectionsMap.getForward(generalSectionName)
        } else {
            generalSectionName
        }

    private fun mapSectionNameToIni(generalSectionName: String): String =
        if (sectionsMap.getBackward(generalSectionName) != null) {
            sectionsMap.getBackward(generalSectionName).toString()
        } else {
            generalSectionName
        }

    fun getSettingsFile(fileName: String): DocumentFile {
        val root = DocumentFile.fromTreeUri(CitraApplication.appContext, Uri.parse(userDirectory))
        val configDirectory = root!!.findFile("config")
        return configDirectory!!.findFile("$fileName.ini")!!
    }

    private fun getCustomGameSettingsFile(gameId: String, create: Boolean): DocumentFile? {
        val canonicalId = normalizeGameId(gameId) ?: return null
        val root = DocumentFile.fromTreeUri(CitraApplication.appContext, Uri.parse(userDirectory))
            ?: return null
        val directory = root.findFile("GameSettings")
            ?: if (create) root.createDirectory("GameSettings") else null
        return directory?.findFile("$canonicalId.ini") ?: if (create) {
            directory?.createFile("application/octet-stream", "$canonicalId.ini")
        } else null
    }

    private fun sectionFromLine(line: String, isCustomGame: Boolean): SettingSection {
        var sectionName: String = line.substring(1, line.length - 1)
        if (isCustomGame) {
            sectionName = mapSectionNameToIni(sectionName)
        }
        return SettingSection(sectionName)
    }

    /**
     * For a line of text, determines what type of data is being represented, and returns
     * a Setting object containing this data.
     *
     * @param line    The line of text being parsed.
     * @return A typed Setting containing the key/value contained in the line.
     */
    private fun settingFromLine(
        line: String,
        section: String?,
        accepts: (AbstractSetting) -> Boolean
    ): AbstractSetting? {
        // CodexAstraLocal: Split once so parameter packages containing '=' remain intact.
        val parts = line.split('=', limit = 2)
        if (parts.size != 2 || parts[0].trim().startsWith('#') ||
            parts[0].trim().startsWith(';')) return null
        val key = parts[0].trim()
        val setting = org.citra.citra_emu.features.settings.model.Settings.allSettings.firstOrNull {
            it.key == key && (section == null || it.section == section)
        } ?: return null
        if (!accepts(setting)) return null
        return if (applyValue(setting, parts[1].trim())) setting else null
    }

    // CodexAstraLocal: Reuse typed enum parsing for global/title loads and reset-to-global snapshots.
    fun applyValue(setting: AbstractSetting, value: String): Boolean {
        return try {
            when (setting) {
                is BooleanSetting -> setting.boolean = when (value.lowercase()) {
                    "true", "yes", "on", "1" -> true
                    "false", "no", "off", "0" -> false
                    else -> throw IllegalArgumentException("Invalid boolean")
                }
                is IntSetting -> setting.int = value.toIntOrNull() ?: when (value.lowercase()) {
                    "true" -> 1
                    "false" -> 0
                    else -> throw IllegalArgumentException("Invalid integer")
                }
                is ScaledFloatSetting -> setting.float = value.toFloat() * setting.scale
                is FloatSetting -> setting.float = value.toFloat()
                is StringSetting -> setting.string = value
                is IntListSetting -> setting.list = if (value.isEmpty()) emptyList() else
                    value.split(',').map { it.trim().toInt() }
                else -> return false
            }
            true
        } catch (e: IllegalArgumentException) {
            Log.error("[SettingsFile] Invalid value for ${setting.key}: ${e.message}")
            false
        }
    }

    /**
     * Writes the contents of a Section HashMap to disk.
     *
     * @param parser  A Wini pointed at a file on disk.
     * @param section A section containing settings to be written to the file.
     */
    private fun writeSection(parser: Wini, section: SettingSection) {
        // Write the section header.
        val header = section.name

        // Write this section's values.
        val settings = section.settings
        val keySet: Set<String> = settings.keys
        for (key in keySet) {
            val setting = settings[key]
            parser.put(header, setting!!.key, setting.valueAsString)
        }
    }
}

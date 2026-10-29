#!/usr/bin/env python3
"""CodexAstraLocal: Exercise production Android settings with file-backed host boundaries.

Requires kotlinc, a compatible JAVA_HOME, and the actual ini4j dependency. No Android
device, emulator, SDK, network, or user configuration is accessed by this fixture.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from android_testing_panel_fixture import panel_stubs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kotlinc", required=True)
    parser.add_argument("--ini4j", required=True)
    parser.add_argument("--output", default="build/android-settings-scope-check")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / "src/android/app/src/main/java/org/citra/citra_emu"
    output = (root / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    files = list((source / "features/settings/model").glob("Abstract*Setting.kt"))
    files += [source / "features/settings/model" / name for name in (
        "Settings.kt", "SettingSection.kt", "BooleanSetting.kt", "IntSetting.kt",
        "FloatSetting.kt", "ScaledFloatSetting.kt", "StringSetting.kt", "IntListSetting.kt",
        "TestingSettings.kt", "DriverSettings.kt", "UberharTestMode.kt",
    )]
    files += [source / "features/settings/utils/SettingsFile.kt", source / "utils/BiMap.kt"]
    # CodexAstraLocal: Execute the real panel handlers as well as persistence for both editor scopes.
    files += [source / "features/settings/ui/TestingPanelController.kt"]
    combined = "\n".join(path.read_text() for path in files)
    resources = sorted(set(re.findall(r"R\.string\.(\w+)", combined)))
    ids = sorted(set(re.findall(r"R\.id\.(\w+)", combined)))
    keys = sorted(set(re.findall(r"SettingKeys\.(\w+)\(\)", combined)))
    # CodexAstraLocal: Only platform/JNI boundaries are substituted; all value and INI logic is real.
    stubs = {
        "TextUtils.kt": '''package android.text
object TextUtils { fun isEmpty(value: CharSequence?) = value.isNullOrEmpty() }
''',
        "Uri.kt": '''package android.net
data class Uri(val path: String) {
    companion object { fun parse(value: String) = Uri(value) }
}
''',
        "Context.kt": '''package android.content
import android.net.Uri
import java.io.File
class ContentResolver {
    fun openInputStream(uri: Uri) = File(uri.path).inputStream()
    fun openOutputStream(uri: Uri, mode: String) = File(uri.path).outputStream()
}
open class Context {
    val contentResolver = ContentResolver()
    fun getString(id: Int, vararg args: Any?) = "$id ${args.joinToString()}"
}
''',
        "DocumentFile.kt": '''package androidx.documentfile.provider
import android.content.Context
import android.net.Uri
import java.io.File
class DocumentFile(private val file: File) {
    val uri get() = Uri(file.path)
    fun findFile(name: String): DocumentFile? = File(file, name).takeIf { it.exists() }?.let(::DocumentFile)
    fun createDirectory(name: String): DocumentFile? = File(file, name).let {
        if (it.mkdirs() || it.isDirectory) DocumentFile(it) else null
    }
    fun createFile(type: String, name: String): DocumentFile? = File(file, name).let {
        if (it.createNewFile() || it.isFile) DocumentFile(it) else null
    }
    companion object { fun fromTreeUri(context: Context, uri: Uri) = DocumentFile(File(uri.path)) }
}
''',
        "Application.kt": '''package org.citra.citra_emu
object CitraApplication { val appContext = android.content.Context() }
// CodexAstraLocal: Native launch state is a boundary; all Testing persistence remains production code.
object NativeLibrary {
    var running = false
    var requestedRightEye = false
    var alphaTestingAvailable = true
    var effectiveHardware = false
    var effectiveDelay = 0
    var effectiveFiltering = true
    var effectiveDepth = 0
    var runningTitle = 0x000400000F700100L
    var reloadCount = 0
    var framebufferCount = 0
    fun isRunning() = running
    fun isAlphaTestingAvailable() = alphaTestingAvailable
    fun getRequestedRightEyeRenderDisabled() = requestedRightEye
    fun getEffectiveHardwareShaders() = effectiveHardware
    fun getEffectiveRenderThreadDelayUs() = effectiveDelay
    fun getEffectiveLinearFiltering() = effectiveFiltering
    fun getEffectiveStereoDepth() = effectiveDepth
    fun getRunningTitleId() = runningTitle
    fun reloadSettings() { reloadCount++ }
    fun isPortraitMode() = false
    fun updateFramebuffer(portrait: Boolean) { framebufferCount++ }
}
object R { object string {
''' + "\n".join(f"const val {name} = {number}" for number, name in enumerate(resources, 1)) +
        "\n} object id {\n" +
        "\n".join(f"const val {name} = {number}" for number, name in enumerate(ids, 1000)) + "\n} }\n",
        "SettingKeys.kt": '''package org.citra.citra_emu.features.settings
object SettingKeys {
''' + "\n".join(f'fun {key}() = "{key}"' for key in keys) + "\n}\n",
        "View.kt": '''package org.citra.citra_emu.features.settings.ui
interface SettingsActivityView {
    fun showToastMessage(message: String, long: Boolean)
    fun onSettingsFileNotFound()
}
''',
        "Utils.kt": '''package org.citra.citra_emu.utils
object DirectoryInitialization { var userDirectory = "" }
object Log { fun error(message: String) { System.err.println(message) } }
''',
        "ScopeCheck.kt": r'''import java.io.File
import org.ini4j.Wini
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.*
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.features.settings.ui.SettingsActivityView
import org.citra.citra_emu.utils.DirectoryInitialization

// CodexAstraLocal: Assert observable persistence and cross-title outcomes, not implementation text.
object View : SettingsActivityView {
    override fun showToastMessage(message: String, long: Boolean) { check(!message.contains("Error")) }
    override fun onSettingsFileNotFound() = error("Unexpected missing configuration")
}
fun read(path: File) = path.inputStream().use(::Wini)
fun write(path: File, ini: Wini) = path.outputStream().use { ini.store(it) }
fun main(args: Array<String>) {
    val folder = File(args.single()).apply { mkdirs() }
    DirectoryInitialization.userDirectory = folder.path
    val config = File(folder, "config/config.ini").apply { parentFile.mkdirs() }
    val title = File(folder, "GameSettings/000400000F700100.ini").apply { parentFile.mkdirs() }
    config.writeText("""
        [Renderer]
        uberhar_test_mode=1
        uberhar_cpu_vertex_only=false
        resolution_factor=2
        use_frame_limit=true
        frame_limit=100
        [Core]
        turbo_limit=400
        [Audio]
        volume=0.75
        [Layout]
        screen_gap=12
        layouts_to_cycle=0, 1, 2
        [WebService]
        network_token=value=with=equals
        [System]
        is_new_3ds=true
    """.trimIndent())
    title.writeText("""
        [Renderer]
        uberhar_test_mode=3
        resolution_factor=4
        future_unknown=value=retained
        [System]
        is_new_3ds=false
    """.trimIndent())
    val originalGlobal = config.readBytes()
    val a = Settings().apply { loadSettings("f700100") }
    check(a.gameId == "000000000F700100") // Normalization is independent of title family.
    a.loadSettings("000400000f700100")
    check(IntSetting.UBERHAR_TEST_MODE.int == 3 && IntSetting.RESOLUTION_FACTOR.int == 4)
    check(BooleanSetting.NEW_3DS.boolean) { "Unsupported title system identity was applied" }
    check(StringSetting.NETWORK_TOKEN.string == "value=with=equals")
    check(ScaledFloatSetting.AUDIO_VOLUME.float == 75f)
    check(!a.isPerGameOverride(BooleanSetting.UBERHAR_CPU_VERTEX_ONLY))
    check(a.isPerGameOverride(IntSetting.RESOLUTION_FACTOR))
    check(Settings.allSettings.map { it.section to it.key }.distinct().size == Settings.allSettings.size) {
        "Duplicate setting aliases can corrupt snapshots and saves"
    }

    // A changed limiter must not be overwritten by the historical integer alias.
    BooleanSetting.USE_FRAME_LIMIT.boolean = false
    BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean = true
    a.saveSettings(View)
    val saved = read(title)
    check(saved.get("Renderer", "use_frame_limit") == "false")
    check(saved.get("Renderer", "future_unknown") == "value=retained")
    check(config.readBytes().contentEquals(originalGlobal)) { "Title save changed global preferences" }
    check(saved.get("Layout", "screen_gap") == null) { "Inherited values were frozen into title overrides" }
    a.loadSettings("000400000f700100")
    check(!BooleanSetting.USE_FRAME_LIMIT.boolean)

    // Reset uses the current saved global value, removes only that explicit override and preserves neighbors.
    a.resetPerGameSetting(BooleanSetting.USE_FRAME_LIMIT)
    check(BooleanSetting.USE_FRAME_LIMIT.boolean && !a.isPerGameOverride(BooleanSetting.USE_FRAME_LIMIT))
    a.saveSettings(View)
    check(read(title).get("Renderer", "use_frame_limit") == null)
    check(read(title).get("Renderer", "resolution_factor") == "4")
    check(config.readBytes().contentEquals(originalGlobal))

    // Immediate synthetic layout saves must target the same title and never its global file.
    IntListSetting.LAYOUTS_TO_CYCLE.list = listOf(1, 4)
    a.saveSetting(IntListSetting.LAYOUTS_TO_CYCLE, SettingsFile.FILE_NAME_CONFIG)
    check(read(title).get("Layout", "layouts_to_cycle") == "1, 4")
    check(config.readBytes().contentEquals(originalGlobal))

    // CodexAstraLocal: Exercise the production four-layer resolver without mutating UI enum drafts.
    val beforeTesting = title.readBytes()
    val testingFile = File(folder, "config/testing.ini")
    val titleSection = "Testing.000400000F700100"
    val allTestingKeys = setOf("enabled", "preset", "resolution", "disable_right_eye_render",
        "use_hw_shader", "delay_game_render_thread_us", "filter_mode", "factor_3d")
    val workingBefore = Settings.allSettings.map { it.valueAsString }
    TestingSettings.load("")
    check(!TestingSettings.enabled && TestingSettings.preset == 0)
    check(TestingSettings.resolution == 2 && !TestingSettings.hardwareShaders)
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))
    check(read(testingFile)["Testing"]!!.keys == setOf("enabled")) {
        "Enabling Testing pinned unrelated inherited choices"
    }
    TestingSettings.preset = 2
    check(TestingSettings.save("preset"))
    TestingSettings.resolution = 2
    check(TestingSettings.save("resolution"))
    TestingSettings.disableRightEyeRender = true
    check(TestingSettings.save("disable_right_eye_render"))
    TestingSettings.hardwareShaders = true
    check(TestingSettings.save("use_hw_shader"))
    TestingSettings.renderThreadDelayUs = 100
    check(TestingSettings.save("delay_game_render_thread_us"))
    TestingSettings.linearFiltering = false
    check(TestingSettings.save("filter_mode"))
    TestingSettings.stereoDepth = 1
    check(TestingSettings.save("factor_3d"))
    check(read(testingFile)["Testing"]!!.keys == allTestingKeys)
    check(!TestingSettings.save("unknown"))
    val globalsBeforeTitleEdit = read(testingFile)["Testing"]!!.toMap()
    TestingSettings.load("000400000f700100")
    check(TestingSettings.preset == 1 && TestingSettings.resolution == 4) {
        "Global Testing clobbered ordinary title mode/CPU or resolution"
    }
    check(TestingSettings.hardwareShaders && !TestingSettings.effectiveHardwareShaders())
    check(TestingSettings.disableRightEyeRender && !TestingSettings.linearFiltering && TestingSettings.stereoDepth == 1)
    TestingSettings.renderThreadDelayUs = 0
    check(TestingSettings.save("delay_game_render_thread_us"))
    check(read(testingFile)[titleSection]!!.keys == setOf("delay_game_render_thread_us"))
    check(read(testingFile)["Testing"]!!.toMap() == globalsBeforeTitleEdit)
    TestingSettings.load("")
    TestingSettings.linearFiltering = true
    check(TestingSettings.save("filter_mode"))
    TestingSettings.stereoDepth = 0
    check(TestingSettings.save("factor_3d"))
    TestingSettings.load("000400000F700100")
    check(TestingSettings.linearFiltering && TestingSettings.stereoDepth == 0 && TestingSettings.renderThreadDelayUs == 0) {
        "A title delay edit pinned unrelated global defaults"
    }
    check(Settings.allSettings.map { it.valueAsString } == workingBefore) {
        "Testing raw reads changed ordinary UI working values"
    }
    check(title.readBytes().contentEquals(beforeTesting) && config.readBytes().contentEquals(originalGlobal))

    // CodexAstraLocal: Ordinary title false/zero values beat contradictory global Testing keys.
    val ordinary = read(title)
    for ((key, value) in mapOf("uberhar_test_mode" to "0", "graphics_api" to "1",
        "disable_right_eye_render" to "false", "use_hw_shader" to "false",
        "delay_game_render_thread_us" to "0", "filter_mode" to "true", "factor_3d" to "0")) {
        ordinary.put("Renderer", key, value)
    }
    write(title, ordinary)
    val contradictory = read(testingFile)
    contradictory.put("Testing", "filter_mode", false)
    contradictory.put("Testing", "factor_3d", 1)
    contradictory[titleSection]!!.remove("delay_game_render_thread_us")
    write(testingFile, contradictory)
    TestingSettings.load("000400000F700100")
    check(TestingSettings.preset == -1 && TestingSettings.resolvedMode() == UberharTestMode.CUSTOM)
    check(TestingSettings.resolution == 4 && !TestingSettings.disableRightEyeRender && !TestingSettings.hardwareShaders)
    check(TestingSettings.renderThreadDelayUs == 0 && TestingSettings.linearFiltering && TestingSettings.stereoDepth == 0)
    // CodexAstraLocal: The later title Testing layer can explicitly replace every ordinary title choice.
    TestingSettings.preset = 2
    check(TestingSettings.save("preset"))
    TestingSettings.resolution = 2
    check(TestingSettings.save("resolution"))
    TestingSettings.disableRightEyeRender = true
    check(TestingSettings.save("disable_right_eye_render"))
    TestingSettings.hardwareShaders = true
    check(TestingSettings.save("use_hw_shader"))
    TestingSettings.renderThreadDelayUs = 100
    check(TestingSettings.save("delay_game_render_thread_us"))
    TestingSettings.linearFiltering = false
    check(TestingSettings.save("filter_mode"))
    TestingSettings.stereoDepth = 1
    check(TestingSettings.save("factor_3d"))
    TestingSettings.load("000400000F700100")
    check(TestingSettings.preset == 2 && TestingSettings.resolution == 2 && TestingSettings.effectiveHardwareShaders())
    check(TestingSettings.disableRightEyeRender && TestingSettings.renderThreadDelayUs == 100)
    check(!TestingSettings.linearFiltering && TestingSettings.stereoDepth == 1)

    // CodexAstraLocal: Title OFF survives global ON; title ON also works while global is OFF.
    val globalsBeforeSwitch = read(testingFile)["Testing"]!!.toMap()
    TestingSettings.enabled = false
    check(TestingSettings.save("enabled"))
    TestingSettings.load("000400000F700100")
    check(!TestingSettings.enabled && TestingSettings.preset == -1 && TestingSettings.resolution == 4)
    check(!TestingSettings.disableRightEyeRender && !TestingSettings.hardwareShaders)
    check(read(testingFile)["Testing"]!!.toMap() == globalsBeforeSwitch)
    TestingSettings.load("0004000000123400")
    check(TestingSettings.enabled && TestingSettings.preset == 2 && TestingSettings.resolution == 2)
    TestingSettings.load("")
    TestingSettings.enabled = false
    check(TestingSettings.save("enabled"))
    TestingSettings.load("000400000F700100")
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))
    TestingSettings.load("000400000F700100")
    check(TestingSettings.enabled && TestingSettings.preset == 2 && TestingSettings.effectiveHardwareShaders())
    TestingSettings.load("0004000000123400")
    check(!TestingSettings.enabled && TestingSettings.preset == 0)
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))
    TestingSettings.load("0004000000123400")
    check(TestingSettings.enabled && TestingSettings.preset == 2 && TestingSettings.disableRightEyeRender) {
        "Title ON did not inherit explicit global Testing defaults while global was OFF"
    }

    // CodexAstraLocal: Reset removes exactly eight owned keys and preserves ordinary/unknown data.
    val withUnknown = read(testingFile)
    withUnknown.put(titleSection, "future_unknown", "keep")
    write(testingFile, withUnknown)
    val ordinaryBeforeReset = title.readBytes()
    TestingSettings.load("000400000F700100")
    check(TestingSettings.useGlobalChoices())
    check(read(testingFile)[titleSection]!!.keys == setOf("future_unknown"))
    check(!TestingSettings.enabled && TestingSettings.preset == -1)
    check(title.readBytes().contentEquals(ordinaryBeforeReset))
    TestingSettings.load("")
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))

    // CodexAstraLocal: Malformed explicit experiments use safe baselines; invalid selectors inherit.
    val malformed = read(testingFile)
    for (key in allTestingKeys - setOf("enabled")) malformed.put(titleSection, key, "broken")
    write(testingFile, malformed)
    TestingSettings.load("000400000F700100")
    check(TestingSettings.preset == -1 && TestingSettings.resolution == 4)
    check(!TestingSettings.disableRightEyeRender && !TestingSettings.hardwareShaders)
    check(TestingSettings.renderThreadDelayUs == 0 && TestingSettings.linearFiltering && TestingSettings.stereoDepth == 0)
    for (value in listOf("-1", "16001", "99999999999999999999")) {
        val invalid = read(testingFile)
        invalid.put(titleSection, "delay_game_render_thread_us", value)
        invalid.put(titleSection, "factor_3d", value)
        write(testingFile, invalid)
        TestingSettings.load("000400000F700100")
        check(TestingSettings.renderThreadDelayUs == 0 && TestingSettings.stereoDepth == 0)
    }
    TestingSettings.renderThreadDelayUs = 16000
    check(TestingSettings.save("delay_game_render_thread_us"))
    TestingSettings.stereoDepth = 1
    check(TestingSettings.save("factor_3d"))
    TestingSettings.load("000400000F700100")
    check(TestingSettings.renderThreadDelayUs == 16000 && TestingSettings.stereoDepth == 1)

    // CodexAstraLocal: Only the matching explicit title can display native launch-latched controls.
    NativeLibrary.running = true
    NativeLibrary.requestedRightEye = true
    NativeLibrary.effectiveHardware = true
    NativeLibrary.effectiveDelay = 17
    NativeLibrary.effectiveFiltering = false
    NativeLibrary.effectiveDepth = 1
    check(TestingSettings.requestedRightEyeRenderDisabled() && TestingSettings.effectiveHardwareShaders())
    check(TestingSettings.effectiveRenderThreadDelayUs() == 17 && !TestingSettings.effectiveLinearFiltering())
    TestingSettings.resolution = 2
    check(TestingSettings.save("resolution"))
    TestingSettings.load("000400000F700100")
    check(TestingSettings.effectiveRenderThreadDelayUs() == 17)
    TestingSettings.load("")
    check(TestingSettings.effectiveRenderThreadDelayUs() == 100)
    TestingSettings.load("0004000000123400")
    check(TestingSettings.effectiveRenderThreadDelayUs() == 100)
    NativeLibrary.running = false

    // CodexAstraLocal: Alpha-disabled builds resolve ordinary values and never mutate stale Testing.
    val alphaBytes = testingFile.readBytes()
    NativeLibrary.alphaTestingAvailable = false
    TestingSettings.load("000400000F700100")
    check(!TestingSettings.available && !TestingSettings.enabled && TestingSettings.preset == -1)
    check(!TestingSettings.requestedRightEyeRenderDisabled() && !TestingSettings.effectiveHardwareShaders())
    check(TestingSettings.effectiveRenderThreadDelayUs() == 0 && TestingSettings.effectiveStereoDepth() == 0)
    check(!TestingSettings.save("enabled") && !TestingSettings.useGlobalChoices())
    check(testingFile.readBytes().contentEquals(alphaBytes))
    check(DriverSettings.save("000400000F700100", DriverSettings.Selection("content://driver=v27", "v27")))
    check(DriverSettings.load("000400000F700100")?.driver == "content://driver=v27")
    check(DriverSettings.load("0004000000123400") == null)
    check(DriverSettings.save("000400000F700100", DriverSettings.Selection(DriverSettings.SYSTEM, "System")))
    check(DriverSettings.save("000400000F700100", null))
    check(DriverSettings.load("000400000F700100") == null)
    check(!DriverSettings.save("../config", DriverSettings.Selection("bad", "bad")))
    NativeLibrary.alphaTestingAvailable = true
    // CodexAstraLocal: Test-only contradictions are restored before continuing ordinary model checks.
    title.writeBytes(beforeTesting)
    check(config.readBytes().contentEquals(originalGlobal))
    a.loadSettings("000400000F700100")
    IntSetting.UBERHAR_TEST_MODE.int = 3
    BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean = false
    check(!TestingSettings.hardwareShadersConflictsWithCpuVertices(a))
    BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean = true
    check(TestingSettings.hardwareShadersConflictsWithCpuVertices(a))
    check(IntSetting.UBERHAR_TEST_MODE.int == 3 && BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean)

    // Switching titles restores globals rather than leaking the preceding title's enum values.
    Settings().loadSettings("0004000000123400")
    check(IntSetting.UBERHAR_TEST_MODE.int == 1 && IntSetting.RESOLUTION_FACTOR.int == 2)
    check(!BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean && BooleanSetting.USE_FRAME_LIMIT.boolean)
    check(IntListSetting.LAYOUTS_TO_CYCLE.list == listOf(0, 1, 2))

    // Reset all removes known title overrides while preserving unknown/future data and global identity.
    a.loadSettings("000400000f700100")
    a.resetPerGameOverrides()
    a.saveSettings(View)
    val reset = read(title)
    check(reset.get("Renderer", "resolution_factor") == null)
    check(reset.get("Renderer", "uberhar_test_mode") == null)
    check(reset.get("Layout", "layouts_to_cycle") == null)
    check(reset.get("Renderer", "future_unknown") == "value=retained")
    check(reset.get("System", "is_new_3ds") == "false") // Unapplied foreign keys survive unchanged.
    check(config.readBytes().contentEquals(originalGlobal))

    // Reset+subsequent edit in the same editor creates an explicit override again.
    a.resetPerGameSetting(IntSetting.RESOLUTION_FACTOR)
    IntSetting.RESOLUTION_FACTOR.int = 3
    a.saveSettings(View)
    check(read(title).get("Renderer", "resolution_factor") == "3")
    for (id in listOf("", "0000000000000000", "../config", "00/1234", "10000000000000000")) {
        check(SettingsFile.normalizeGameId(id) == null)
    }
    // Older Android files put the turbo cap under Renderer; do not silently reset 400 to 200.
    config.writeText("[Renderer]\nturbo_limit=400\n")
    Settings().loadSettings("")
    check(IntSetting.TURBO_LIMIT.int == 400) { "Legacy turbo cap was lost by section matching" }
    // CodexAstraLocal: Invalid ordinary HW values defer to the final mode, preserving valid inheritance.
    val fallbackDirectory = File(folder, "hardware-fallback").apply { mkdirs() }
    DirectoryInitialization.userDirectory = fallbackDirectory.path
    val fallbackGlobal = File(fallbackDirectory, "config/config.ini").apply { parentFile.mkdirs() }
    val fallbackTesting = File(fallbackDirectory, "config/testing.ini")
    val fallbackTitle = File(fallbackDirectory, "GameSettings/0004000000777700.ini").apply { parentFile.mkdirs() }
    for (invalid in listOf("", "broken")) {
        fallbackGlobal.writeText("[Renderer]\nuberhar_test_mode=0\nuse_hw_shader=$invalid\n")
        fallbackTesting.writeText("[Testing]\nenabled=true\npreset=0\n")
        fallbackTitle.writeText("[Renderer]\n")
        TestingSettings.load("0004000000777700")
        check(TestingSettings.resolvedMode() == UberharTestMode.NATIVE && !TestingSettings.hardwareShaders) {
            "Invalid global HW froze Custom's default before global Testing selected Native"
        }
        fallbackGlobal.writeText("[Renderer]\nuberhar_test_mode=1\nuse_hw_shader=$invalid\n")
        fallbackTitle.writeText("[Renderer]\nuberhar_test_mode=0\n")
        TestingSettings.load("0004000000777700")
        check(TestingSettings.resolvedMode() == UberharTestMode.CUSTOM && TestingSettings.hardwareShaders) {
            "Invalid global HW froze Native's default before the ordinary title selected Custom"
        }
        for (explicit in listOf(false, true)) {
            fallbackGlobal.writeText("[Renderer]\nuberhar_test_mode=1\nuse_hw_shader=$explicit\n")
            fallbackTitle.writeText("[Renderer]\nuberhar_test_mode=0\nuse_hw_shader=$invalid\n")
            TestingSettings.load("0004000000777700")
            check(TestingSettings.hardwareShaders == explicit) {
                "Invalid ordinary title HW discarded an explicit inherited value"
            }
        }
    }
    fallbackGlobal.writeText("[Renderer]\n")
    fallbackTesting.writeText("[Testing]\nenabled=false\n")
    fallbackTitle.writeText("[Renderer]\n")
    TestingSettings.load("0004000000777700")
    check(TestingSettings.resolvedMode() == UberharTestMode.CUSTOM && TestingSettings.hardwareShaders) {
        "Missing values did not match the native Custom/HW defaults"
    }
    // CodexAstraLocal: Selecting an ordinary title value equal to its loaded default
    // still creates a choice that beats different global Testing values.
    fallbackGlobal.writeText("[Renderer]\nuberhar_test_mode=1\nresolution_factor=2\n" +
        "use_hw_shader=false\ndelay_game_render_thread_us=0\n")
    fallbackTesting.writeText("[Testing]\nenabled=true\npreset=2\nresolution=4\n" +
        "use_hw_shader=true\ndelay_game_render_thread_us=100\n")
    fallbackTitle.writeText("[Renderer]\n")
    val explicitOrdinary = Settings()
    explicitOrdinary.loadSettings("0004000000777700")
    check(IntSetting.RESOLUTION_FACTOR.int == 2 && !BooleanSetting.HW_SHADER.boolean)
    for (setting in listOf(IntSetting.RESOLUTION_FACTOR, BooleanSetting.HW_SHADER,
        IntSetting.DELAY_RENDER_THREAD_US)) check(explicitOrdinary.markPerGameSettingEdited(setting))
    explicitOrdinary.saveSettings(View)
    check(read(fallbackTitle).get("Renderer", "resolution_factor") == "2")
    check(read(fallbackTitle).get("Renderer", "use_hw_shader") == "false")
    check(read(fallbackTitle).get("Renderer", "delay_game_render_thread_us") == "0")
    TestingSettings.load("0004000000777700")
    check(TestingSettings.resolution == 2 && !TestingSettings.hardwareShaders &&
        TestingSettings.renderThreadDelayUs == 0)
    for (setting in listOf(IntSetting.RESOLUTION_FACTOR, BooleanSetting.HW_SHADER,
        IntSetting.DELAY_RENDER_THREAD_US)) explicitOrdinary.resetPerGameSetting(setting)
    explicitOrdinary.saveSettings(View)
    TestingSettings.load("0004000000777700")
    check(TestingSettings.resolution == 4 && TestingSettings.hardwareShaders &&
        TestingSettings.renderThreadDelayUs == 100)
    DirectoryInitialization.userDirectory = folder.path
    checkTestingPanel(folder)
    println("PASS: production settings/INI title isolation, sparse saves, limiter alias, inheritance reset, unknown retention, immediate layout saves, Testing global/title defaults and panel interactions, CPU veto, launch snapshots, alpha/beta preservation, driver independence and ID validation")
}
''',
    }
    # CodexAstraLocal: Substitute Android widgets, dialogs and lifecycle only; controller/model are real.
    stubs.update(panel_stubs())
    for name, contents in stubs.items():
        path = output / name
        path.write_text(contents)
        files.append(path)
    jar = output / "settings-scope.jar"
    command = [args.kotlinc, *map(str, files), "-cp", args.ini4j, "-include-runtime", "-d", str(jar)]
    with (output / "compile.log").open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    if result.returncode:
        (output / "results.json").write_text(json.dumps({
            "status": "failed", "phase": "compile", "exit_code": result.returncode,
        }, indent=2) + "\n")
        raise SystemExit(f"Kotlin compile failed: {output / 'compile.log'}")
    java = Path(os.environ["JAVA_HOME"]) / "bin/java"
    result = subprocess.run([str(java), "-cp", os.pathsep.join((str(jar), args.ini4j)),
                             "ScopeCheckKt", str(output / "private-user-dir")], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    (output / "run.log").write_text(result.stdout)
    (output / "results.json").write_text(json.dumps({
        "status": "passed" if result.returncode == 0 else "failed",
        "phase": "run", "exit_code": result.returncode,
    }, indent=2) + "\n")
    print(result.stdout, end="")
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()

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
    val effectiveExperiments = mutableMapOf<String, Boolean>()
    var effectiveDelay = 0
    var effectiveDepth = 0
    var runningTitle = 0x000400000F700100L
    var reloadCount = 0
    var framebufferCount = 0
    fun isRunning() = running
    fun isAlphaTestingAvailable() = alphaTestingAvailable
    fun getRequestedRightEyeRenderDisabled() = requestedRightEye
    fun getEffectiveTestingBoolean(key: String) = effectiveExperiments[key] ?: false
    fun getEffectiveRenderThreadDelayUs() = effectiveDelay
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

    // CodexAstraLocal: Exercise every real .57 key through all four sparse layers.
    val beforeTesting = title.readBytes()
    val testingFile = File(folder, "config/testing.ini")
    val titleSection = "Testing.000400000F700100"
    val choices = TestingExperiment.entries
    TestingSettings.load("")
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))
    check(read(testingFile)["Testing"]!!.keys == setOf("enabled"))
    check(!TestingSettings.save("use_hw_shader") && !TestingSettings.save("filter_mode"))
    val workingBefore = Settings.allSettings.map { it.valueAsString }
    for (choice in choices) {
        check(!choice.setting.isRuntimeEditable)
        TestingSettings.load("")
        TestingSettings.setExperiment(choice, true)
        check(TestingSettings.save(choice.key))
        val ordinary = read(title)
        ordinary.put("Renderer", choice.key, false)
        write(title, ordinary)
        TestingSettings.load("000400000F700100")
        check(!TestingSettings.experiment(choice)) { "Global Testing beat ordinary title ${choice.key}" }
        val globalBefore = read(testingFile)["Testing"]!!.toMap()
        TestingSettings.setExperiment(choice, true)
        check(TestingSettings.save(choice.key))
        TestingSettings.load("000400000F700100")
        check(TestingSettings.experiment(choice))
        TestingSettings.setExperiment(choice, false)
        check(TestingSettings.save(choice.key))
        TestingSettings.load("000400000F700100")
        check(!TestingSettings.experiment(choice)) { "Explicit title false lost for ${choice.key}" }
        check(read(testingFile)["Testing"]!!.toMap() == globalBefore)
        TestingSettings.load("0004000000123400")
        check(TestingSettings.experiment(choice)) { "Another title inherited wrong experiment" }
    }
    check(Settings.allSettings.map { it.valueAsString } == workingBefore)
    check(config.readBytes().contentEquals(originalGlobal))
    TestingSettings.load("000400000F700100")
    TestingSettings.renderThreadDelayUs = 0
    check(TestingSettings.save("delay_game_render_thread_us"))
    TestingSettings.stereoDepth = 0
    check(TestingSettings.save("factor_3d"))
    TestingSettings.enabled = false
    check(TestingSettings.save("enabled"))
    TestingSettings.load("000400000F700100")
    check(!TestingSettings.enabled)
    TestingSettings.load("0004000000123400")
    check(TestingSettings.enabled)
    // Title ON can override global OFF without modifying that global master.
    TestingSettings.load("")
    TestingSettings.enabled = false
    check(TestingSettings.save("enabled"))
    TestingSettings.load("000400000F700100")
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))
    TestingSettings.load("000400000F700100")
    check(TestingSettings.enabled)
    check(read(testingFile).get("Testing", "enabled") == "false")
    val unknown = read(testingFile).apply { put(titleSection, "future_unknown", "keep") }
    write(testingFile, unknown)
    check(TestingSettings.useGlobalChoices())
    check(read(testingFile)[titleSection]!!.keys == setOf("future_unknown"))
    // Sparse numeric edits and invalid selector handling still preserve normal choices.
    TestingSettings.enabled = true
    check(TestingSettings.save("enabled"))
    for (value in listOf("-1", "16001", "broken", "99999999999999999999")) {
        val malformed = read(testingFile)
        malformed.put(titleSection, "delay_game_render_thread_us", value)
        malformed.put(titleSection, "factor_3d", value)
        write(testingFile, malformed)
        TestingSettings.load("000400000F700100")
        check(TestingSettings.renderThreadDelayUs == 0 && TestingSettings.stereoDepth == 0)
    }
    val c = choices.first()
    NativeLibrary.running = true
    NativeLibrary.requestedRightEye = true
    NativeLibrary.effectiveExperiments[c.key] = true
    NativeLibrary.effectiveDelay = 17
    TestingSettings.load("000400000F700100")
    check(TestingSettings.effectiveExperiment(c) && TestingSettings.requestedRightEyeRenderDisabled())
    check(TestingSettings.effectiveRenderThreadDelayUs() == 17)
    TestingSettings.load("")
    check(TestingSettings.effectiveExperiment(c) == TestingSettings.experiment(c))
    NativeLibrary.running = false
    val alphaBytes = testingFile.readBytes()
    NativeLibrary.alphaTestingAvailable = false
    TestingSettings.load("000400000F700100")
    check(!TestingSettings.available && !TestingSettings.enabled)
    check(!TestingSettings.save("enabled") && !TestingSettings.useGlobalChoices())
    check(testingFile.readBytes().contentEquals(alphaBytes))
    check(DriverSettings.save("000400000F700100", DriverSettings.Selection("content://driver=v27", "v27")))
    check(DriverSettings.load("000400000F700100")?.driver == "content://driver=v27")
    check(DriverSettings.save("000400000F700100", null))
    NativeLibrary.alphaTestingAvailable = true
    // CodexAstraLocal: Test-only contradictions are restored before continuing ordinary model checks.
    title.writeBytes(beforeTesting)
    check(config.readBytes().contentEquals(originalGlobal))
    a.loadSettings("000400000F700100")
    IntSetting.UBERHAR_TEST_MODE.int = 3
    BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean = false
    BooleanSetting.UBERHAR_CPU_VERTEX_ONLY.boolean = true
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
    // CodexAstraLocal: Retired keys preserve data and active filtering without hiding an override.
    val migrationDirectory = File(folder, "retired-controls").apply { mkdirs() }
    DirectoryInitialization.userDirectory = migrationDirectory.path
    val migrationGlobal = File(migrationDirectory, "config/config.ini").apply {
        parentFile.mkdirs(); writeText("[Renderer]\nuse_hw_shader=true\n")
    }
    val migrationTitle = File(migrationDirectory, "GameSettings/0004000000777700.ini").apply {
        parentFile.mkdirs(); writeText("[Renderer]\nfilter_mode=false\nuse_hw_shader=true\n")
    }
    val migrationTesting = File(migrationDirectory, "config/testing.ini").apply {
        writeText("[Testing]\nenabled=true\nfilter_mode=false\nuse_hw_shader=true\n" +
            "[Testing.0004000000777700]\nfilter_mode=true\nuse_hw_shader=false\n")
    }
    Settings().loadSettings("0004000000777700")
    check(read(migrationGlobal).get("Renderer", "filter_mode") == "false")
    check(read(migrationTitle).get("Renderer", "filter_mode") == "false")
    check(read(migrationGlobal).get("Renderer", "use_hw_shader") == "true")
    check(read(migrationTitle).get("Renderer", "use_hw_shader") == "true")
    val migrated = read(migrationTesting)
    check(migrated.get("Testing", "filter_mode") == null)
    check(migrated.get("Testing", "use_hw_shader") == null)
    check(migrated.get("Retired.056.Testing", "filter_mode") == "false")
    check(migrated.get("Retired.056.Testing.0004000000777700", "filter_mode") == "true")
    val stable = listOf(migrationGlobal,migrationTitle,migrationTesting).map { it.readBytes() }
    Settings().loadSettings("0004000000777700")
    check(listOf(migrationGlobal,migrationTitle,migrationTesting).zip(stable).all { (file, bytes) ->
        file.readBytes().contentEquals(bytes) })
    migrationGlobal.writeText("[Renderer]\nfilter_mode=true\n")
    migrationTesting.writeText("[Testing]\nenabled=false\nfilter_mode=false\n")
    Settings().loadSettings("")
    check(read(migrationGlobal).get("Renderer", "filter_mode") == "true")
    check(read(migrationTesting).get("Retired.056.Testing", "filter_mode") == "false")
    DirectoryInitialization.userDirectory = folder.path
    checkTestingPanel(folder)
    println("PASS: production settings/INI title isolation, sparse saves, limiter alias, inheritance reset, unknown retention, immediate layout saves, Testing global/title defaults and panel interactions, retired-key migration, all experiment precedence, launch snapshots, alpha/beta preservation, driver independence and ID validation")
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

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
        "TestingSettings.kt", "DriverSettings.kt",
    )]
    files += [source / "features/settings/utils/SettingsFile.kt", source / "utils/BiMap.kt"]
    combined = "\n".join(path.read_text() for path in files)
    resources = sorted(set(re.findall(r"R\.string\.(\w+)", combined)))
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
object R { object string {
''' + "\n".join(f"const val {name} = {number}" for number, name in enumerate(resources, 1)) + "\n} }\n",
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

    // Testing changes persist separately and never overwrite the ordinary model or title/global values.
    val beforeTesting = title.readBytes()
    TestingSettings.load()
    check(!TestingSettings.enabled)
    TestingSettings.enabled = true
    TestingSettings.preset = 2
    TestingSettings.resolution = 2
    check(TestingSettings.save())
    TestingSettings.load()
    check(TestingSettings.enabled && TestingSettings.preset == 2 && TestingSettings.resolution == 2)
    check(title.readBytes().contentEquals(beforeTesting) && config.readBytes().contentEquals(originalGlobal))
    check(IntSetting.UBERHAR_TEST_MODE.int == 3 && IntSetting.RESOLUTION_FACTOR.int == 4)
    // CodexAstraLocal: Per-title Testing choices do not contaminate other titles or global defaults.
    TestingSettings.load("000400000f700100")
    check(TestingSettings.preset == 2 && TestingSettings.resolution == 2)
    TestingSettings.preset = 1
    TestingSettings.resolution = 4
    check(TestingSettings.save())
    TestingSettings.load("0004000000123400")
    check(TestingSettings.preset == 2 && TestingSettings.resolution == 2)
    TestingSettings.load("")
    TestingSettings.preset = 0
    check(TestingSettings.save())
    TestingSettings.load("000400000f700100")
    check(TestingSettings.preset == 1 && TestingSettings.resolution == 4)
    check(TestingSettings.useGlobalChoices())
    check(TestingSettings.preset == 0 && TestingSettings.resolution == 2)
    TestingSettings.enabled = false
    check(TestingSettings.save())
    // CodexAstraLocal: Driver exceptions are a third independent store; null resumes global inheritance.
    check(DriverSettings.save("000400000f700100", DriverSettings.Selection("content://driver=v27", "v27")))
    check(DriverSettings.load("000400000f700100")?.driver == "content://driver=v27")
    check(DriverSettings.load("0004000000123400") == null)
    check(DriverSettings.save("000400000f700100", DriverSettings.Selection(DriverSettings.SYSTEM, "System")))
    check(DriverSettings.load("000400000f700100")?.driver == DriverSettings.SYSTEM)
    check(DriverSettings.save("000400000f700100", null))
    check(DriverSettings.load("000400000f700100") == null)
    check(!DriverSettings.save("../config", DriverSettings.Selection("bad", "bad")))
    check(title.readBytes().contentEquals(beforeTesting) && config.readBytes().contentEquals(originalGlobal))
    a.loadSettings("000400000f700100")
    check(IntSetting.UBERHAR_TEST_MODE.int == 3 && IntSetting.RESOLUTION_FACTOR.int == 4)

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
    println("PASS: production settings/INI title isolation, sparse saves, limiter alias, inheritance reset, unknown retention, immediate layout saves, Testing separation and ID validation")
}
''',
    }
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

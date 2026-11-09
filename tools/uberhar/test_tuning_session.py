#!/usr/bin/env python3
"""CodexAstraLocal: Execute actual advisory session/evaluator with real JSON/INI and host platform adapters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / "src/android/app/src/main/java/org/citra/citra_emu/features/settings/model"
TEST = ROOT / "tools/uberhar/tests/PerformanceTuningSessionHostTest.kt"

# CodexAstraLocal: These adapters model platform/native boundaries only. No settings
# write API is supplied, so introducing one into the journal breaks host compilation.
STUBS = {
    "Context.kt": '''package android.content
import java.io.File
import java.io.InputStream
import android.net.Uri
class Context(val filesDir: File) { val contentResolver = ContentResolver() }
class ContentResolver {
 var denyReads = false
 fun openInputStream(uri: Uri): InputStream? {
  check(!denyReads) { "host denied read" }; return File(uri.path).inputStream()
 }
}
''',
    "Uri.kt": '''package android.net
data class Uri(val path: String) { companion object { fun parse(value: String) = Uri(value.removePrefix("file://")) } }
''',
    "DocumentFile.kt": '''package androidx.documentfile.provider
import android.content.Context
import android.net.Uri
import java.io.File
class DocumentFile(private val file: File) {
 val uri get() = Uri(file.path)
 fun findFile(name: String): DocumentFile? = File(file, name).takeIf { it.exists() }?.let { DocumentFile(it) }
 companion object { fun fromTreeUri(context: Context, uri: Uri): DocumentFile? =
  File(uri.path).takeIf { it.isDirectory }?.let { DocumentFile(it) } }
}
''',
    "AtomicFile.kt": '''package android.util
import java.io.File
import java.io.FileOutputStream
class AtomicFile(val baseFile: File) {
 private val backup get() = File(baseFile.path + ".bak")
 fun openRead() = (if (backup.exists()) backup else baseFile).inputStream()
 fun startWrite(): FileOutputStream {
  if (baseFile.exists()) check(baseFile.renameTo(backup)); return baseFile.outputStream()
 }
 fun finishWrite(stream: FileOutputStream) { stream.close(); backup.delete() }
 fun failWrite(stream: FileOutputStream) { stream.close(); baseFile.delete(); backup.renameTo(baseFile) }
}
''',
    "Build.kt": '''package android.os
object Build { const val FINGERPRINT="host/fingerprint"; const val DEVICE="host-device"
 const val MODEL="host-model"; const val BOARD="host-board"; const val HARDWARE="host-hardware"
 const val SOC_MODEL="host-soc"; object VERSION { const val SDK_INT=33 } }
''',
    "Settings.kt": '''package android.provider
import android.content.ContentResolver
object Settings {
 object Global { var bootCount=7; fun getInt(resolver: ContentResolver, key: String, fallback: Int)=bootCount }
 object Secure { const val ANDROID_ID="android_id"; var deviceId="host-fixture-app-scoped-id"
  fun getString(resolver: ContentResolver, key: String): String?=deviceId }
}
''',
    "Native.kt": '''package org.citra.citra_emu
object NativeLibrary {
 var running=false; var title=0L; var sample="{}"; var cancelCount=0
 fun isRunning()=running
 fun getRunningTitleId()=title
 fun getTuningSample()=sample
 fun beginTuningSample(frames: Int): Boolean { check(frames==1200); return true }
 fun cancelTuningSample() { cancelCount++ }
}
object BuildConfig { var GIT_HASH="build-original"; var VERSION_NAME="0.1.58"; var VERSION_CODE=58 }
''',
    "Activity.kt": '''package org.citra.citra_emu.activities
object EmulationActivity { var running=false; fun isRunning()=running }
''',
    "SettingsFile.kt": '''package org.citra.citra_emu.features.settings.utils
object SettingsFile { fun normalizeGameId(id: String): String? =
 id.uppercase().takeIf { it.matches(Regex("[0-9A-F]{16}")) } }
''',
    "Testing.kt": '''package org.citra.citra_emu.features.settings.model
object TestingSettings { var available=true; var gameId=""; var enabled=true; var preset=2 }
''',
    "Directory.kt": '''package org.citra.citra_emu.utils
object DirectoryInitialization { var userDirectory="" }
''',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kotlinc", default=shutil.which("kotlinc"))
    parser.add_argument("--java", default=shutil.which("java"))
    parser.add_argument("--ini4j", required=True, type=Path)
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/tuning-session")
    args = parser.parse_args()
    if not args.kotlinc or not args.java:
        parser.error("Supply --kotlinc and --java when absent from PATH")
    args.output.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="run-", dir=args.output))
    adapters = []
    for name, content in STUBS.items():
        path = folder / name
        path.write_text(content)
        adapters.append(path)
    sources = [MODEL / "UberharTuningEngine.kt", MODEL / "PerformanceTuningSession.kt",
               ROOT / "src/android/app/src/main/java/org/citra/citra_emu/utils/LogTelemetrySnapshot.kt", TEST]
    jar = folder / "tuning-session.jar"
    compiler = Path(args.kotlinc).resolve()
    command = ([str(compiler)] if os.access(compiler, os.X_OK) else ["bash", str(compiler)])
    dependencies = [args.ini4j.resolve(), args.json.resolve()]
    cp = os.pathsep.join(map(str, dependencies))
    compile_command = command + list(map(str, sources + adapters)) + ["-cp", cp, "-include-runtime", "-d", str(jar)]
    run_command = [str(Path(args.java).resolve()), "-cp", str(jar) + os.pathsep + cp,
                   "org.citra.citra_emu.features.settings.model.PerformanceTuningSessionHostTestKt"]
    env = dict(os.environ, JAVA_HOME=str(Path(args.java).resolve().parent.parent))
    proof = {"author": "CodexAstraLocal", "scope": "actual production session + evaluator; real JSON/INI; platform/native adapters",
             "sources": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
             "dependencies": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies},
             "compile_command": compile_command, "run_command": run_command}
    print(f"Evidence: {folder}", flush=True)
    for name, command in (("compile", compile_command), ("run", run_command)):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=120)
        (folder / f"{name}.log").write_text(result.stdout)
        proof[f"{name}_returncode"] = result.returncode
        (folder / "RESULT.json").write_text(json.dumps(proof, indent=2) + "\n")
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        if name == "run":
            print(result.stdout, end="")


if __name__ == "__main__":
    main()

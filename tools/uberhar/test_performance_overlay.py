#!/usr/bin/env python3
"""CodexAstraLocal: Execute real overlay/lifecycle Kotlin against a modeled queue.

This is a host regression for callback ownership, not Android lifecycle/device
validation. The fixture compiles the actual fragment methods without rewriting
their scheduling logic. Supply the project's Kotlin compiler and a Java runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FRAGMENT = ROOT / "src/android/app/src/main/java/org/citra/citra_emu/fragments/EmulationFragment.kt"


def method(source, name, optional=False):
    # CodexAstraLocal: Keep braces in strings/comments out of the extractor's
    # nesting count; every returned method body remains byte-for-byte source.
    match = re.search(r"^    (?:(?:private|override) )?fun " + name + r"\(", source, re.M)
    if not match:
        if optional:
            return ""
        raise ValueError(f"missing production method: {name}")
    opening = source.index("{", match.start())
    index, depth = opening, 0
    while index < len(source):
        if source.startswith("//", index):
            index = source.index("\n", index)
        elif source.startswith("/*", index):
            index = source.index("*/", index + 2) + 2
        elif source.startswith('"""', index):
            index = source.index('"""', index + 3) + 3
        elif source[index] in "\"'":
            quote = source[index]
            index += 1
            while index < len(source) and source[index] != quote:
                index += 2 if source[index] == "\\" else 1
            index += 1
        else:
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[match.start():index + 1]
            index += 1
    raise ValueError(f"unterminated production method: {name}")


def fixture(source):
    # CodexAstraLocal: Preserve both the old companion Handler and the current
    # instance form, so a baseline run demonstrates the original ownership bug.
    declarations = []
    for name in ("perfStatsUpdateHandler", "perfStatsUpdater", "performanceOverlayResumed"):
        match = re.search(r"^( +)private (?:val|var) " + name + r"[^\n]*", source, re.M)
        if match:
            line = match.group(0)
            declarations.append("    companion object {\n" + line + "\n    }"
                                if len(match.group(1)) == 8 else line)
        elif name != "performanceOverlayResumed":
            raise ValueError(f"missing production field: {name}")
    names = ("onResume", "onPause", "onDestroyView", "onDestroy",
             "stopPerformanceOverlayUpdates", "updateShowPerformanceOverlay")
    bodies = [method(source, name, name in ("onDestroyView", "stopPerformanceOverlayUpdates"))
              for name in names]
    if "updateShowPerformanceOverlay()" not in method(source, "onViewCreated"):
        raise ValueError("fixture no longer models production view-created startup")
    return MODEL + "\nclass Fixture : Fragment() {\n" + FIXTURE_MEMBERS + "\n" + \
        "\n\n".join(declarations + bodies) + "\n}\n" + SCENARIOS


# CodexAstraLocal: Android plumbing is modeled only at its boundary: a single
# main queue, independent views, settings and a counted destructive native read.
MODEL = r'''
import java.io.File

object Queue {
    data class Entry(val owner: Handler, val callback: Runnable, val at: Long)
    val pending = mutableListOf<Entry>()
    var now = 0L
    fun reset() { pending.clear(); now = 0L }
    fun one() {
        val entry = pending.firstOrNull { it.at <= now } ?: return
        pending.remove(entry)
        entry.callback.run()
    }
    fun drain() {
        var count = 0
        while (pending.any { it.at <= now }) {
            check(++count <= 128) { "queue did not quiesce" }
            one()
        }
    }
    fun advance(ms: Long) { now += ms; drain() }
}
class Looper {
    companion object {
        private val main = Looper()
        fun getMainLooper() = main
        fun myLooper(): Looper? = main
    }
}
class Handler(val looper: Looper) {
    fun post(callback: Runnable) = postDelayed(callback, 0L)
    fun postDelayed(callback: Runnable, delay: Long) {
        Queue.pending.add(Queue.Entry(this, callback, Queue.now + delay))
    }
    fun removeCallbacks(callback: Runnable) {
        Queue.pending.removeAll { it.owner === this && it.callback === callback }
    }
}
open class Fragment {
    open fun onResume() {}
    open fun onPause() {}
    open fun onDestroyView() {}
    open fun onDestroy() {}
}
object NativeLibrary {
    var reads = 0
    var running = true
    var starts = 0
    var onRead: (() -> Unit)? = null
    fun isRunning() = running
    fun getPerfStats(): DoubleArray {
        ++reads
        onRead?.invoke()
        return doubleArrayOf(60.0, 60.0, 1.0, 0.016, 0.0, 0.0, 0.0, 0.0, 0.0)
    }
}
enum class BooleanSetting(var boolean: Boolean = false) {
    PERF_OVERLAY_ENABLE, PERF_OVERLAY_SHOW_FPS, PERF_OVERLAY_SHOW_FRAMETIME,
    PERF_OVERLAY_SHOW_SPEED, PERF_OVERLAY_SHOW_APP_RAM_USAGE,
    PERF_OVERLAY_SHOW_AVAILABLE_RAM, PERF_OVERLAY_SHOW_BATTERY_TEMP,
    PERF_OVERLAY_BACKGROUND;
    companion object {
        fun reset() {
            entries.forEach { it.boolean = false }
            PERF_OVERLAY_ENABLE.boolean = true
            PERF_OVERLAY_SHOW_FPS.boolean = true
            PERF_OVERLAY_SHOW_SPEED.boolean = true
        }
    }
}
enum class IntSetting(val int: Int) { PERFORMANCE_OVERLAY_POSITION(0) }
object View { const val VISIBLE = 0; const val GONE = 8 }
class TextView {
    var visibility = View.GONE
    var writes = 0
    var text = ""
        set(value) { field = value; ++writes }
    fun setBackgroundResource(resource: Int) {}
}
class MenuItem { var title = ""; var icon: Any? = null }
class Menu { fun findItem(id: Int): MenuItem? = MenuItem() }
class InGameMenu { val menu = Menu() }
class Binding {
    val performanceOverlayShowText = TextView()
    val inGameMenu = InGameMenu()
}
class Activity { val isFinishing = true; val isActivityRecreated = false }
class Context {
    val theme = Any()
    fun getSystemService(name: String): Any = ActivityManager()
    companion object { const val ACTIVITY_SERVICE = "activity" }
}
class ActivityManager {
    class MemoryInfo { var availMem = 0L }
    fun getMemoryInfo(info: MemoryInfo) {}
}
class Resources { fun getString(id: Int) = "Pause" }
object ResourcesCompat {
    fun getDrawable(resources: Resources, id: Int, theme: Any): Any? = null
}
object R {
    object color { const val citra_transparent_black = 1 }
    object string { const val pause_emulation = 2 }
    object drawable { const val ic_pause = 3 }
    object id { const val menu_emulation_pause = 4 }
}
class EmulationState {
    fun unpause() {}; fun pause() {}; fun stop() {}
    fun run(b: Boolean) { ++NativeLibrary.starts }
}
class Choreographer {
    fun postFrameCallback(callback: Any) {}
    fun removeFrameCallback(callback: Any) {}
    companion object { fun getInstance() = Choreographer() }
}
object DirectoryInitialization { fun areCitraDirectoriesReady() = true }
object EmulationLifecycleUtil { fun removeHook(callback: Runnable) {} }
class ParcelFileDescriptor {
    fun close() {}
    companion object { fun adoptFd(fd: Int) = ParcelFileDescriptor() }
}
'''

FIXTURE_MEMBERS = r'''
    private lateinit var emulationState: EmulationState
    init { emulationState = EmulationState() }
    private var _binding: Binding? = null
    private val binding get() = _binding!!
    private val onPause = Runnable {}
    private val onShutdown = Runnable {}
    private var gameFd: Int? = null
    private val emulationActivity = Activity()
    private val resources = Resources()
    private val context: Context? = Context()
    private fun requireActivity() = emulationActivity
    private fun requireContext() = context!!
    private fun setupCitraDirectoriesThenStartEmulation() {}
    private fun updateStatsPosition(position: Int) {}
    private fun getBatteryTemperature() = 25.0f
    private fun celsiusToFahrenheit(celsius: Float) = celsius * 1.8f + 32.0f
    fun createView(): Binding {
        val next = Binding()
        _binding = next
        updateShowPerformanceOverlay()
        return next
    }
    fun heldCallback() = perfStatsUpdater
'''

# CodexAstraLocal: Exercise queued and already-held stale callbacks, including
# cancellation/replacement during the native read, then a later title fragment.
SCENARIOS = r'''
var checks = 0
val failures = mutableListOf<String>()
fun expect(ok: Boolean, reason: String) {
    ++checks
    if (!ok) failures.add(reason)
}
fun reset() {
    Queue.reset()
    NativeLibrary.reads = 0
    NativeLibrary.running = true
    NativeLibrary.starts = 0
    NativeLibrary.onRead = null
    BooleanSetting.reset()
}
fun active(): Fixture {
    val fragment = Fixture()
    fragment.createView()
    fragment.onResume()
    Queue.drain()
    return fragment
}
fun main() {
    reset()
    NativeLibrary.running = false
    val startup = Fixture()
    startup.createView()
    startup.onResume()
    Queue.drain()
    expect(NativeLibrary.starts == 1 && Queue.pending.size == 1,
        "fresh native-start path did not retain one updater")
    startup.onPause()
    expect(Queue.pending.isEmpty(), "pause while core is not running retained polling")

    reset()
    val first = Fixture()
    val view = first.createView()
    expect(Queue.pending.isEmpty(), "created-but-not-resumed view queued a reset")
    expect(NativeLibrary.reads == 0, "created view consumed statistics")
    first.onResume()
    Queue.drain()
    expect(NativeLibrary.reads == 1 && Queue.pending.size == 1, "resume must own one chain")
    expect(view.performanceOverlayShowText.text.contains("FPS:\u00A060") &&
        view.performanceOverlayShowText.text.contains("Speed:\u00A0100%"), "formatting changed")
    repeat(5) { first.updateShowPerformanceOverlay() }
    expect(Queue.pending.size == 1, "repeated settings updates duplicated callbacks")
    Queue.drain()
    Queue.advance(1000)
    expect(NativeLibrary.reads == 3 && Queue.pending.size == 1, "active cadence changed")
    val stale = first.heldCallback()
    first.onPause()
    expect(Queue.pending.isEmpty(), "pause retained a queued callback")
    expect(first.heldCallback() == null, "pause retained callback ownership")
    val before = NativeLibrary.reads
    val writes = view.performanceOverlayShowText.writes
    stale?.run()
    expect(NativeLibrary.reads == before, "held callback reset counters after pause")
    expect(view.performanceOverlayShowText.writes == writes, "held callback wrote paused view")
    expect(Queue.pending.isEmpty(), "held callback resurrected polling after pause")
    first.updateShowPerformanceOverlay()
    expect(Queue.pending.isEmpty(), "settings update while paused started polling")
    first.onResume()
    Queue.drain()
    expect(Queue.pending.size == 1, "resume did not restore one updater")
    val toggled = first.heldCallback()
    BooleanSetting.PERF_OVERLAY_ENABLE.boolean = false
    first.updateShowPerformanceOverlay()
    val readsAtDisable = NativeLibrary.reads
    expect(Queue.pending.isEmpty() && view.performanceOverlayShowText.visibility == View.GONE,
        "disable did not hide and cancel overlay")
    toggled?.run()
    expect(NativeLibrary.reads == readsAtDisable && Queue.pending.isEmpty(),
        "stale enabled callback survived disable")
    BooleanSetting.PERF_OVERLAY_ENABLE.boolean = true
    first.updateShowPerformanceOverlay()
    Queue.drain()
    expect(Queue.pending.size == 1 && view.performanceOverlayShowText.visibility == View.VISIBLE,
        "reenable did not restore one visible updater")

    reset()
    val replacement = active()
    val oldViewCallback = replacement.heldCallback()
    replacement.onDestroyView()
    expect(Queue.pending.isEmpty(), "view destruction retained queued work")
    expect(replacement.heldCallback() == null, "view destruction retained ownership")
    val newView = replacement.createView()
    val readsBeforeReplacement = NativeLibrary.reads
    expect(Queue.pending.isEmpty(), "replacement view polled before resume")
    oldViewCallback?.run()
    expect(NativeLibrary.reads == readsBeforeReplacement && Queue.pending.isEmpty(),
        "old view callback survived view replacement")
    replacement.onResume()
    Queue.drain()
    expect(Queue.pending.size == 1 && newView.performanceOverlayShowText.writes == 1,
        "replacement resumed view did not own one chain")
    oldViewCallback?.run()
    expect(newView.performanceOverlayShowText.writes == 1 && Queue.pending.size == 1,
        "old view callback interfered with resumed replacement")

    reset()
    val canceled = active()
    val callback = canceled.heldCallback()
    canceled.updateShowPerformanceOverlay()
    NativeLibrary.onRead = { canceled.onPause() }
    Queue.one()
    NativeLibrary.onRead = null
    expect(Queue.pending.isEmpty(), "cancellation inside native read reposted")
    val readsAfterCancel = NativeLibrary.reads
    callback?.run()
    expect(NativeLibrary.reads == readsAfterCancel, "replaced callback consumed a second read")

    reset()
    val interrupted = Fixture()
    val interruptedView = interrupted.createView()
    interrupted.onResume()
    NativeLibrary.onRead = { interrupted.onDestroyView() }
    Queue.one()
    NativeLibrary.onRead = null
    expect(interruptedView.performanceOverlayShowText.writes == 0,
        "native-read teardown wrote the destroyed view")
    expect(Queue.pending.isEmpty(), "native-read teardown resurrected queue")

    reset()
    val reconfigured = active()
    reconfigured.updateShowPerformanceOverlay()
    NativeLibrary.onRead = { reconfigured.updateShowPerformanceOverlay() }
    Queue.one()
    NativeLibrary.onRead = null
    expect(Queue.pending.size == 1, "in-read replacement posted old and new callbacks")
    Queue.drain()
    expect(Queue.pending.size == 1, "replacement callback failed to maintain one chain")

    reset()
    val retired = active()
    val retiredCallback = retired.heldCallback()
    retired.onDestroy()
    expect(Queue.pending.isEmpty() && retired.heldCallback() == null,
        "fragment destruction retained a callback")
    val current = active()
    val currentReads = NativeLibrary.reads
    retiredCallback?.run()
    expect(NativeLibrary.reads == currentReads && Queue.pending.size == 1,
        "retired title reset current title statistics")
    Queue.advance(1000)
    expect(NativeLibrary.reads == currentReads + 1 && Queue.pending.size == 1,
        "multiple fragments produced multiple periodic resetters")
    current.onPause()
    current.onDestroyView()
    current.onDestroy()
    expect(Queue.pending.isEmpty(), "ordinary pause/view/destroy order leaked a chain")
    failures.forEach { println("FAIL: $it") }
    check(failures.isEmpty()) { "${failures.size} callback lifecycle failures across $checks checks" }
    println("PASS: $checks extracted Kotlin callback checks (modeled main Handler, no Android/device execution)")
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=FRAGMENT)
    parser.add_argument("--kotlinc", default=shutil.which("kotlinc"))
    parser.add_argument("--java", default=shutil.which("java"))
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/performance-overlay")
    args = parser.parse_args()
    if not args.kotlinc or not args.java:
        parser.error("--kotlinc and --java are required when absent from PATH")
    args.output.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="fixture-", dir=args.output))
    source = args.source.read_text()
    kotlin = folder / "OverlayLifecycle.kt"
    kotlin.write_text(fixture(source))
    jar = folder / "overlay.jar"
    env = dict(os.environ)
    env["JAVA_HOME"] = str(Path(args.java).resolve().parent.parent)
    compile_command = [str(Path(args.kotlinc).resolve()), str(kotlin), "-include-runtime", "-d", str(jar)]
    execution_command = [str(Path(args.java).resolve()), "-jar", str(jar)]
    metadata = {"author": "CodexAstraLocal", "source": str(args.source.resolve()),
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "fixture_sha256": hashlib.sha256(kotlin.read_bytes()).hexdigest(),
                "compile_command": compile_command, "execution_command": execution_command,
                "scope": "real extracted Kotlin methods; deterministic modeled Android plumbing"}
    for label, command in (("compile", compile_command), ("run", execution_command)):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=90 if label == "compile" else 15, text=True)
        (folder / f"{label}.log").write_text(result.stdout)
        metadata[f"{label}_returncode"] = result.returncode
        (folder / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        if label == "run":
            print(result.stdout, end="")
    print(f"Retained fixture/provenance: {folder}")


if __name__ == "__main__":
    main()

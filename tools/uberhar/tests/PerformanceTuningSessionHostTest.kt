// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.features.settings.model

import android.content.Context
import android.provider.Settings
import org.citra.citra_emu.BuildConfig
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.utils.DirectoryInitialization
import org.json.JSONArray
import org.json.JSONObject
import org.ini4j.Wini
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

// CodexAstraLocal: Exercise the real journal/evaluator; only platform and native boundaries
// are adapted. Actual Wini/JSONObject parse the preference bytes and persisted evidence.
private val typed = "uberhar_typed_primitive_append"
private val adaptive = "uberhar_adaptive_cpu_workers"
private val one = UberharTuningEngine.Candidate("typed", setOf(typed))
private val pair = UberharTuningEngine.Candidate("typed+adaptive", setOf(typed, adaptive))
private const val TITLE = "00040000000A0500"
private var checks = 0
private fun expect(condition: Boolean, message: String) { checks++; check(condition) { message } }
private inline fun rejected(message: String, block: () -> Unit) {
    checks++
    try { block() } catch (_: IllegalStateException) { return }
    catch (_: IllegalArgumentException) { return }
    error(message)
}
private fun digest(file: File) = MessageDigest.getInstance("SHA-256")
    .digest(file.readBytes()).joinToString("") { "%02x".format(it) }

private class Fixture(candidates: List<UberharTuningEngine.Candidate> = listOf(one)) {
    val folder = Files.createTempDirectory("uberhar-tuning-session-").toFile()
    val user = File(folder, "user").apply { mkdirs() }
    val context = Context(File(folder, "app").apply { mkdirs() })
    var session: PerformanceTuningSession
    var launches = 0
    val journal get() = File(context.filesDir, "performance-tuning/$TITLE.json")
    val testing get() = File(user, "config/testing.ini")
    val global get() = File(user, "config/config.ini")
    init {
        Settings.Global.bootCount = 7
        Settings.Secure.deviceId = "host-fixture-app-scoped-id"
        BuildConfig.GIT_HASH = "build-original"
        BuildConfig.VERSION_NAME = "0.1.58"
        BuildConfig.VERSION_CODE = 58
        DirectoryInitialization.userDirectory = user.path
        File(user, "config").mkdirs(); File(user, "GameSettings").mkdirs()
        global.writeText("[Renderer]\nresolution_factor=2\n$typed=true\n[Core]\ncpu_clock_percentage=100\n[Other]\nuntouched=global\n")
        testing.writeText("[Testing]\nenabled=true\n$adaptive=true\n[Testing.$TITLE]\npreset=2\n$typed=false\n[Testing.0004000000000001]\nuntouched=other-title\n")
        File(user, "config/drivers.ini").writeText("[Driver.$TITLE]\nselection=v27\n[Driver.0004000000000001]\nselection=system\n")
        File(user, "GameSettings/$TITLE.ini").writeText("[Renderer]\nfilter_mode=true\n[Core]\ncpu_jit=true\n")
        File(user, "GameSettings/0004000000000001.ini").writeText("[Renderer]\nresolution_factor=4\n")
        File(user, "save.bin").writeBytes(byteArrayOf(0, 1, 2, 3))
        File(user, "cache.bin").writeBytes(byteArrayOf(4, 5, 6))
        TestingSettings.available = true; TestingSettings.enabled = true
        TestingSettings.gameId = TITLE; TestingSettings.preset = 2
        stop()
        session = PerformanceTuningSession(context, TITLE)
        untouched { session.start("Same confirmed enemy attack", "Standard; fixed driver/cache", candidates) }
    }
    fun bytes() = user.walkTopDown().filter { it.isFile }.associate { it.relativeTo(user).path to digest(it) }
    fun untouched(action: () -> Unit) {
        val before = bytes()
        try { action() } finally { expect(bytes() == before, "No global/title/save/cache writes") }
    }
    fun stop() { NativeLibrary.running = false; EmulationActivity.running = false }
    fun manualSetting(file: File, section: String, key: String, value: String) {
        Wini(file).apply { put(section, key, value); store(file) }
    }
    fun restart() { session = PerformanceTuningSession(context, TITLE) }
    fun value(): JSONObject = requireNotNull(session.read())
    fun raw(features: Set<String>, low: Double = 80.0, mean: Double = 100.0): JSONObject = JSONObject()
        .put("schema", 1).put("titleId", TITLE).put("context", "build=fixture;uberhar_test_mode=1;resolution_factor=2;actual_driver=fixture-driver;quality=fixed;")
        .put("features", JSONArray(features.sorted())).put("runId", "launch-$launches:${launches * 1000}")
        .put("status", "complete").put("targetFrames", 1200).put("frames", 1200)
        .put("binGuestFrames", 120).put("binCount", 10).put("limit", 400.0)
        .put("startNanos", launches * 1000L).put("endNanos", launches * 1000L + 100)
        .put("low", low).put("mean", mean).put("high", 130.0)
        .put("p95Ms", 15.0).put("p99Ms", 19.0).put("paused", false).put("pacingLimited", false)
    fun ready(): Set<String> {
        stop()
        val expected = session.nextFeatures(value())
        untouched { session.prepare() }
        expect(value().getString("state") == "ready", "Plan acknowledgment")
        return expected
    }
    fun arm(features: Set<String>, mutate: (JSONObject) -> Unit = {}): JSONObject {
        launches++
        NativeLibrary.running = true; EmulationActivity.running = true
        NativeLibrary.title = TITLE.toULong(16).toLong()
        val n = raw(features).apply(mutate)
        NativeLibrary.sample = n.toString()
        untouched { session.begin() }
        return n
    }
    fun measure(low: Double = 80.0, mean: Double = 100.0, mutate: (JSONObject) -> Unit = {}) {
        val n = arm(ready()).put("low", low).put("mean", mean).apply(mutate)
        NativeLibrary.sample = n.toString()
        untouched { session.collect(true) }
        stop()
    }
    fun close() { stop(); folder.deleteRecursively() }
}

fun main() {
    // CodexAstraLocal: Actual bounded export helper preserves complete JSONL records
    // without chasing appended bytes or presenting a partial final record as complete.
    val snapshotFolder = java.nio.file.Files.createTempDirectory("telemetry-export-").toFile()
    try {
        val output = File(snapshotFolder, "snapshot.jsonl")
        val bytes = "{\"a\":1}\n{\"partial\":".toByteArray()
        val proof = org.citra.citra_emu.utils.LogTelemetrySnapshot.copy(bytes.inputStream(), output, bytes.size.toLong())
        expect(output.readText() == "{\"a\":1}\n" && proof.partialTailBytes > 0, "Partial tail is explicit and source unchanged")
        expect(bytes.decodeToString().endsWith(":"), "Snapshot did not change source")
        val complete = "{}\n".toByteArray()
        org.citra.citra_emu.utils.LogTelemetrySnapshot.copy((complete + bytes).inputStream(), output, 3)
        expect(output.readBytes().contentEquals(complete), "Export reads only frozen endpoint")
        expect(org.citra.citra_emu.utils.LogTelemetrySnapshot.accepts("azahar_log.txt.frames-100-000001.jsonl") &&
            !org.citra.citra_emu.utils.LogTelemetrySnapshot.accepts("../azahar_log.txt.frames-100-000001.jsonl"),
            "Only canonical compact chunks enter export picker")
    } finally { snapshotFolder.deleteRecursively() }
    val f = Fixture(listOf(one, pair))
    // The owner may manually change only candidate flags without invalidating the fixed context.
    val initial = f.value()
    f.testing.appendText("\n[$typed-owner-note]\nnot_settings=true\n")
    expect(f.session.isContextCurrent(initial), "Unrelated INI section does not invalidate")
    f.manualSetting(f.global, "Renderer", adaptive, "false")
    expect(f.session.isContextCurrent(initial), "Candidate feature override excluded from fixed context")
    f.measure() // A
    f.restart()
    expect(f.session.step(f.value()) == "candidate", "App restart restores completed baseline step")
    f.measure(72.0, 94.0) // losing singleton
    f.measure() // A
    val first = f.session.engine(f.value()).decisions().getValue(one.id)
    expect(first.status == UberharTuningEngine.Status.REJECT, "Real journal submits rejected singleton")
    expect(f.session.nextFeatures(f.value()) == pair.features, "Positive pair remains scheduled")
    f.measure(88.0, 112.0) // pair B, no repeated A launch
    f.measure() // A
    expect(f.launches == 5 && f.value().getJSONArray("samples").length() == 5, "Trailing A reused: five launches not six")
    expect(f.value().getString("state") == "results", "Bounded plan completed")
    val saved = f.journal.readBytes()
    f.restart()
    expect(f.journal.readBytes().contentEquals(saved), "Reading after restart does not rewrite evidence")
    expect(f.session.engine(f.value()).bestCandidate() == pair, "Restart replays actual stored decisions")
    f.untouched { f.session.recommend() }
    val recommendation = f.value().getJSONObject("recommendation")
    expect(recommendation.getBoolean("accepted") && recommendation.getJSONArray("features").length() == 2,
        "Advisory complete-tuple recommendation")
    expect(recommendation.getJSONObject("measurement").getDouble("low") == 88.0, "Recommendation binds actual measured candidate")
    expect(recommendation.getString("scene") == "Same confirmed enemy attack", "Advice names measured scene")
    val reportBytes = f.journal.readBytes()
    val report = f.session.comparisonReport(f.value())
    expect(report.getString("selectedOptimalId") == pair.id, "Only explicitly accepted recommendation anchors secondary report")
    val reportRows = report.getJSONArray("rows")
    expect(reportRows.length() == 2 && !reportRows.getJSONObject(0).isNull("primary"), "Real journal exports matched baseline changes")
    expect(!reportRows.getJSONObject(0).isNull("secondaryOptimal") &&
        reportRows.getJSONObject(1).isNull("secondaryOptimal"), "Secondary report excludes optimal self")
    expect(report.isNull("resolutionBridge"), "Archived session does not invent cross-resolution references")
    expect(f.journal.readBytes().contentEquals(reportBytes), "Report/export read does not mutate journal or settings")
    val oldJournal = JSONObject(f.value().toString()).apply { remove("recommendation"); remove("comparisonReport") }
    expect(f.session.comparisonReport(oldJournal).isNull("selectedOptimalId"), "Old plan without selected reference stays baseline-only")
    f.manualSetting(f.testing, "Testing.$TITLE", typed, "true")
    f.untouched { f.session.manualChange(typed) }
    expect(f.session.isContextCurrent(f.value()) && f.value().getString("state") == "recommended",
        "Owner can manually use recommendation without automatic settings mutation")
    f.untouched { f.session.cancel() }
    expect(f.value().getString("state") == "cancelled", "Cancel only changes journal state")
    f.close()

    // CodexAstraLocal: Wrong effective flags/title/driver/quality cannot enter the finite ledger.
    for (kind in listOf("flags", "title", "driver", "quality")) {
        val x = Fixture()
        if (kind in listOf("driver", "quality")) x.measure()
        val wanted = x.ready()
        rejected("Reject native $kind mismatch") {
            x.arm(wanted) { n -> when (kind) {
                "flags" -> n.put("features", JSONArray(listOf("unexpected")))
                "title" -> n.put("titleId", "0004000000000001")
                "driver" -> n.put("context", "other-driver:quality:renderer")
                "quality" -> n.put("context", "actual-driver:other-quality:renderer")
            } }
        }
        expect(x.value().getString("state") == "ready", "Mismatched launch did not arm measurement")
        x.close()
    }

    // CodexAstraLocal: A reboot/build/fixed preference edit is historical evidence, not current advice.
    for (kind in listOf("fixed-settings", "boot", "build", "version", "device")) {
        val x = Fixture()
        val old = x.value(); val journalBefore = x.journal.readBytes()
        when (kind) {
            "fixed-settings" -> x.manualSetting(x.global, "Core", "cpu_clock_percentage", "80")
            "boot" -> Settings.Global.bootCount++
            "build" -> BuildConfig.GIT_HASH = "other-build"
            "version" -> BuildConfig.VERSION_CODE++
            "device" -> Settings.Secure.deviceId = "different-device"
        }
        expect(!x.session.isContextCurrent(old), "Stale display: $kind")
        expect(x.journal.readBytes().contentEquals(journalBefore), "Display guard does not rewrite stale evidence")
        rejected("Stale context cannot prepare") { x.untouched { x.session.prepare() } }
        x.close()
    }

    val staleRun = Fixture()
    val pending = staleRun.arm(staleRun.ready())
    staleRun.restart()
    NativeLibrary.sample = pending.put("runId", "different-launch").toString()
    rejected("Restart cannot adopt another launch's sample") { staleRun.untouched { staleRun.session.collect(true) } }
    expect(staleRun.value().getJSONArray("samples").length() == 0, "Stale trial not added to samples")
    staleRun.close()

    val edited = Fixture()
    edited.arm(edited.ready())
    edited.manualSetting(edited.testing, "Testing.$TITLE", typed, "true")
    edited.untouched { edited.session.manualChange(typed) }
    expect(edited.value().getString("state") == "invalid" && NativeLibrary.cancelCount > 0,
        "Manual feature edit during measurement invalidates without restoring preferences")
    rejected("Invalid trial cannot collect") { edited.untouched { edited.session.collect(true) } }
    edited.close()

    val limit = Fixture()
    limit.measure()
    val changed = limit.arm(limit.ready()).put("limit", 200.0)
    NativeLibrary.sample = changed.toString()
    rejected("Speed-limit change rejects comparison") { limit.untouched { limit.session.collect(true) } }
    limit.close()

    val paused = Fixture()
    paused.measure(); paused.measure(100.0, 120.0) { it.put("paused", true) }; paused.measure()
    val pauseDecision = paused.session.engine(paused.value()).decisions().getValue(one.id)
    expect(pauseDecision.status == UberharTuningEngine.Status.INCONCLUSIVE &&
        UberharTuningEngine.Reason.PAUSED in pauseDecision.reasons, "Paused high result cannot win")
    paused.untouched { paused.session.declineRetry() }
    paused.untouched { paused.session.recommend() }
    expect(!paused.value().getJSONObject("recommendation").getBoolean("accepted"), "No accepted result returns baseline advice")
    paused.close()

    val denied = Fixture()
    denied.context.contentResolver.denyReads = true
    expect(!denied.session.isContextCurrent(denied.value()), "Unreadable storage cannot masquerade as inheritance")
    rejected("Storage failure blocks next trial") { denied.untouched { denied.session.prepare() } }
    denied.close()
    println("PASS: $checks real session/evaluator lifecycle assertions; settings/save/cache bytes unchanged by all operations")
}

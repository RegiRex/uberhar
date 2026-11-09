// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import android.content.Context
import android.net.Uri
import android.os.Build
import android.util.AtomicFile
import androidx.documentfile.provider.DocumentFile
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.BuildConfig
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.DirectoryInitialization
import org.ini4j.Wini
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest

/** CodexAstraLocal: An advisory per-title journal records requested profiles and native
 * measurements plus pure-engine decisions before the next manual launch.
 * No setting writes, game launch, save, cache operation or quality change occurs here. */
class PerformanceTuningSession(private val context: Context, titleId: String) {
    val title = requireNotNull(SettingsFile.normalizeGameId(titleId))
    private val directory = File(context.filesDir, "performance-tuning")
    private val file = AtomicFile(File(directory, "$title.json"))
    private val section = "Testing.$title"

    fun exists(): Boolean = file.baseFile.exists() || File(file.baseFile.path + ".bak").exists()

    fun read(): JSONObject? {
        if (!exists()) return null
        val value = file.openRead().use { JSONObject(it.reader().readText()) }
        check(value.getInt("schema") == 1 && value.getString("titleId") == title)
        check(value.getJSONArray("candidates").length() in 1..UberharTuningEngine.MAX_CANDIDATES)
        return value
    }

    private fun save(value: JSONObject) {
        check(directory.isDirectory || directory.mkdirs())
        val stream = file.startWrite()
        try {
            stream.write(value.toString().toByteArray(Charsets.UTF_8))
            file.finishWrite(stream)
        } catch (e: Exception) {
            file.failWrite(stream)
            throw e
        }
    }

    private fun stopped() {
        check(!EmulationActivity.isRunning() && !NativeLibrary.isRunning()) {
            "Close the game normally before planning the next manual launch."
        }
    }

    private fun root(): DocumentFile = requireNotNull(DocumentFile.fromTreeUri(
        context, Uri.parse(DirectoryInitialization.userDirectory)))

    // CodexAstraLocal: Unlike a forgiving settings display, admission must fail on
    // unreadable storage. An I/O error must never masquerade as key absence/inheritance.
    private fun readIni(parent: String, name: String): Wini {
        val dir = root().findFile(parent)
        if (dir == null) {
            check(parent != "config") { "Settings directory is unavailable." }
            return Wini()
        }
        val document = dir.findFile(name) ?: return Wini()
        return requireNotNull(context.contentResolver.openInputStream(document.uri)).use { Wini(it) }
    }

    private fun fixedSettingsFingerprint(): String {
        val values = sortedMapOf<String, String>()
        fun include(tag: String, ini: Wini, names: List<String>) {
            names.forEach { name -> ini[name]?.forEach { (key, value) ->
                if (!(key in featureKeys)) {
                    values["$tag/$name/$key"] = value
                }
            } }
        }
        include("global", readIni("config", "config.ini"), listOf("Renderer", "Core", "System"))
        include("title", readIni("GameSettings", "$title.ini"), listOf("Renderer", "Core", "System"))
        include("testing", readIni("config", "testing.ini"), listOf("Testing", section))
        include("driver", readIni("config", "drivers.ini"), listOf("Driver.$title"))
        values["directory"] = DirectoryInitialization.userDirectory
        // CodexAstraLocal: Monotonic samples cannot cross a reboot or restored app data on
        // another device. Hash host identity at UI boundaries, never on the native frame path.
        val boot = android.provider.Settings.Global.getInt(context.contentResolver, "boot_count", -1)
        check(boot >= 0) { "Android boot identity is unavailable; this tuning session cannot be compared safely." }
        val deviceId = android.provider.Settings.Secure.getString(context.contentResolver, android.provider.Settings.Secure.ANDROID_ID)
        check(!deviceId.isNullOrBlank()) { "App-scoped device identity is unavailable." }
        values["host/boot"] = boot.toString()
        values["host/deviceId"] = deviceId
        values["host/fingerprint"] = Build.FINGERPRINT
        // CodexAstraLocal: Persisted advice is tied to the installed APK, not a future source tree.
        values["app/git"] = BuildConfig.GIT_HASH
        values["app/versionName"] = BuildConfig.VERSION_NAME
        values["app/versionCode"] = BuildConfig.VERSION_CODE.toString()
        values["host/device"] = Build.DEVICE
        values["host/model"] = Build.MODEL
        values["host/board"] = Build.BOARD
        values["host/hardware"] = Build.HARDWARE
        if (Build.VERSION.SDK_INT >= 31) values["host/soc"] = Build.SOC_MODEL
        val bytes = JSONObject(values as Map<*, *>).toString().toByteArray(Charsets.UTF_8)
        return MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    }

    // CodexAstraLocal: Display validity is read-only. Historical evidence survives ordinary
    // Graphics edits, reboot or APK changes without being shown as a current recommendation.
    fun isContextCurrent(value: JSONObject): Boolean = runCatching {
        value.getString("fixedSettings") == fixedSettingsFingerprint()
    }.getOrDefault(false)

    private fun verifySession(value: JSONObject) {
        check(value.getString("fixedSettings") == fixedSettingsFingerprint()) {
            "A fixed setting changed. End this advisory session before planning another."
        }
    }

    fun start(scene: String, conditions: String, selected: List<UberharTuningEngine.Candidate>) {
        stopped()
        check(scene.isNotBlank() && conditions.isNotBlank())
        check(scene.length <= 120 && conditions.length <= 240)
        check(selected.size in 1..UberharTuningEngine.MAX_CANDIDATES)
        check(selected.all { it.features.isNotEmpty() && featureKeys.containsAll(it.features) })
        check(selected.map { it.id }.distinct().size == selected.size)
        UberharTuningEngine("planned", scene.trim(), emptySet(), selected)
        check(TestingSettings.available && TestingSettings.gameId == title && TestingSettings.enabled && TestingSettings.preset in 0..2) {
            "Enable Testing for this game and select Native or Combo before starting."
        }
        read()?.let { old ->
            check(old.getString("state") == "cancelled") { "End the previous advisory session first." }
            File(directory, "$title-${old.getLong("createdAt")}.json").writeText(old.toString())
        }
        save(JSONObject().put("schema", 1).put("titleId", title).put("createdAt", System.currentTimeMillis())
            .put("scene", scene.trim()).put("conditions", conditions.trim()).put("state", "prepare")
            .put("fixedSettings", fixedSettingsFingerprint()).put("candidates", JSONArray().apply {
                selected.forEach { put(JSONObject().put("id", it.id).put("features", array(it.features))) }
            }).put("events", JSONArray()).put("samples", JSONArray()).put("nativeContext", "")
            .put("effectiveLimit", JSONObject.NULL).put("before", JSONObject.NULL).put("trial", JSONObject.NULL))
    }

    fun candidates(value: JSONObject): List<UberharTuningEngine.Candidate> =
        value.getJSONArray("candidates").objects().map {
            UberharTuningEngine.Candidate(it.getString("id"), strings(it.getJSONArray("features")))
        }

    private fun engineContext(value: JSONObject): String = value.getString("nativeContext") +
        "|limit=" + (if (value.isNull("effectiveLimit")) "pending" else value.getDouble("effectiveLimit").toString()) + "|conditions=" + value.getString("conditions") +
        "|settings=" + value.getString("fixedSettings")

    fun engine(value: JSONObject): UberharTuningEngine {
        val result = UberharTuningEngine(engineContext(value), value.getString("scene"), emptySet(), candidates(value))
        value.getJSONArray("events").objects().forEach { event ->
            if (event.getString("type") == "decline") result.declineRetry(event.getString("candidateId"))
            else result.submit(event.getString("candidateId"), comparison(value, event))
        }
        return result
    }

    private fun next(value: JSONObject): UberharTuningEngine.Candidate? =
        value.optString("validationCandidate").takeIf { it.isNotBlank() }?.let { id ->
            candidates(value).first { it.id == id }
        } ?: engine(value).nextCandidate()

    fun step(value: JSONObject): String = when {
        value.isNull("before") -> "baseline before"
        value.isNull("trial") -> "candidate"
        else -> "baseline after"
    }

    fun nextFeatures(value: JSONObject): Set<String> =
        if (step(value) == "candidate") requireNotNull(next(value)).features else emptySet()

    // CodexAstraLocal: Planning records the suggestion only. The owner edits preferences
    // manually and restarts; begin() checks the resulting native profile before accepting it.
    fun prepare() {
        stopped()
        val value = requireNotNull(read())
        check(value.getString("state") == "prepare")
        verifySession(value)
        check(next(value) != null) { "The bounded plan is complete." }
        value.put("requestedFeatures", array(nextFeatures(value))).put("state", "ready")
        save(value)
    }

    fun begin() {
        val value = requireNotNull(read())
        check(value.getString("state") == "ready")
        verifySession(value)
        check(NativeLibrary.isRunning() && title == "%016X".format(NativeLibrary.getRunningTitleId())) {
            "Manually select the suggested profile and launch this game first."
        }
        val native = JSONObject(NativeLibrary.getTuningSample())
        verifyNative(value, native, complete = false)
        val usedRuns = value.getJSONArray("samples").objects().map { it.getString("runId") }
        check(native.getString("runId") !in usedRuns) { "Each prepared step needs a new normal game launch." }
        value.put("sampleRunId", native.getString("runId")).put("state", "arming")
        if (value.getString("nativeContext").isBlank()) value.put("nativeContext", native.getString("context"))
        save(value)
        if (!NativeLibrary.beginTuningSample(GUEST_FRAMES)) {
            invalidate("Native measurement could not start. Close normally and end this advisory session.")
            error("Native measurement could not start.")
        }
        value.put("state", "measuring")
        save(value)
    }

    private fun verifyNative(value: JSONObject, native: JSONObject, complete: Boolean) {
        check(native.getInt("schema") == 1 && native.getString("titleId") == title)
        check(native.getString("context").isNotBlank())
        val expected = value.getString("nativeContext")
        check(expected.isBlank() || native.getString("context") == expected) { "Native driver/mode/quality context changed." }
        check(strings(native.getJSONArray("features")) == nextFeatures(value)) { "Effective native flags do not match this scheduled step." }
        if (complete) {
            check(native.getString("runId") == value.getString("sampleRunId")) { "A new game replaced the uncollected measurement." }
            check(native.getInt("targetFrames") == GUEST_FRAMES && native.getInt("frames") == GUEST_FRAMES)
            check(native.getInt("binGuestFrames") == BIN_FRAMES && native.getInt("binCount") == GUEST_FRAMES / BIN_FRAMES)
            val limit = native.getDouble("limit")
            check(limit.isFinite() && limit >= 0 && (value.isNull("effectiveLimit") || value.getDouble("effectiveLimit") == limit)) {
                "Effective speed limit changed between samples."
            }
        }
    }

    /** Returns native progress; collection is explicit, with no polling or background launch. */
    fun collect(sceneConfirmed: Boolean): String {
        val value = requireNotNull(read())
        check(value.getString("state") in listOf("measuring", "arming"))
        verifySession(value)
        val native = JSONObject(NativeLibrary.getTuningSample())
        value.put("lastNative", native)
        save(value)
        check(native.getString("runId") == value.getString("sampleRunId")) {
            "A new launch replaced the uncollected sample."
        }
        val status = native.getString("status")
        if (status in listOf("armed", "collecting")) return "$status: ${native.optInt("frames")}/$GUEST_FRAMES guest frames"
        if (status != "complete" || !sceneConfirmed) {
            value.put("lastNative", native)
            save(value)
            invalidate(if (!sceneConfirmed) "Scene or conditions were not consistent." else native.optString("reason", "Measurement unavailable."))
            return "Trial invalid; end this session before starting another."
        }
        verifyNative(value, native, complete = true)
        if (value.isNull("effectiveLimit")) value.put("effectiveLimit", native.getDouble("limit"))
        native.put("sceneConfirmed", true)
        value.getJSONArray("samples").put(native)
        when (step(value)) {
            "baseline before" -> value.put("before", native)
            "candidate" -> value.put("trial", native)
            else -> {
                val id = requireNotNull(next(value)).id
                val event = JSONObject().put("type", "submit").put("candidateId", id)
                    .put("before", value.getJSONObject("before")).put("trial", value.getJSONObject("trial")).put("after", native)
                val controller = engine(value)
                if (value.optString("validationCandidate").isNotBlank()) {
                    value.put("confirmation", event)
                    recordRecommendation(value, controller.recommendation(id, comparison(value, event)))
                } else {
                    controller.submit(id, comparison(value, event))
                    value.getJSONArray("events").put(event)
                }
                // CodexAstraLocal: Reuse this exact trailing baseline, not a synthesized reference.
                value.put("before", native).put("trial", JSONObject.NULL)
            }
        }
        if (!value.has("recommendation")) value.put("state", if (next(value) == null) "results" else "prepare")
        save(value)
        return "Measurement collected. Close normally and manually select the next suggested profile."
    }

    private fun sample(value: JSONObject, n: JSONObject): UberharTuningEngine.Sample {
        check(n.getInt("schema") == 1 && n.getString("titleId") == title)
        check(n.getString("context") == value.getString("nativeContext"))
        check(n.getDouble("limit") == value.getDouble("effectiveLimit"))
        check(n.getInt("targetFrames") == GUEST_FRAMES)
        return UberharTuningEngine.Sample(
            contextId = engineContext(value), sceneId = value.getString("scene"),
            features = strings(n.getJSONArray("features")), guestFrames = n.getInt("frames"),
            binGuestFrames = n.getInt("binGuestFrames"), startNanos = n.getLong("startNanos"), endNanos = n.getLong("endNanos"),
            lowSpeed = n.getDouble("low"), meanSpeed = n.getDouble("mean"), highSpeed = n.getDouble("high"),
            p95FrameMs = n.getDouble("p95Ms"), p99FrameMs = n.getDouble("p99Ms"),
            paused = n.getBoolean("paused"), pacingLimited = n.getBoolean("pacingLimited"), valid = n.getString("status") == "complete",
            sceneConfirmed = n.getBoolean("sceneConfirmed"), binCount = n.getInt("binCount")
        )
    }

    private fun comparison(v: JSONObject, event: JSONObject) = UberharTuningEngine.Comparison(
        sample(v, event.getJSONObject("before")), sample(v, event.getJSONObject("trial")), sample(v, event.getJSONObject("after")))

    fun declineRetry() {
        val value = requireNotNull(read())
        check(value.getString("state") == "prepare")
        val controller = engine(value)
        val id = requireNotNull(controller.nextCandidate()).id
        controller.declineRetry(id)
        value.getJSONArray("events").put(JSONObject().put("type", "decline").put("candidateId", id))
        value.put("state", if (controller.nextCandidate() == null) "results" else "prepare")
        save(value)
    }

    fun recommend() {
        val value = requireNotNull(read())
        check(value.getString("state") in listOf("prepare", "results"))
        verifySession(value)
        check(value.isNull("trial")) { "Finish the current baseline bracket first." }
        val controller = engine(value)
        val best = controller.bestCandidate()
        val recommendation = controller.recommendation(best?.id)
        if (UberharTuningEngine.Reason.FRESH_CONFIRMATION_REQUIRED in recommendation.reasons) {
            value.put("validationCandidate", requireNotNull(best).id).put("state", "prepare")
        } else recordRecommendation(value, recommendation)
        save(value)
    }

    private fun recordRecommendation(value: JSONObject, r: UberharTuningEngine.Recommendation) {
        value.put("recommendation", JSONObject().put("accepted", r.accepted).put("features", array(r.features))
            .put("reasons", JSONArray(r.reasons.map { it.name })).put("scene", r.sceneId).put("context", r.contextId))
        r.decision?.let { decision ->
            val sample = decision.evidence.candidate
            value.getJSONObject("recommendation").put("measurement", JSONObject()
                .put("low", sample.lowSpeed).put("mean", sample.meanSpeed).put("high", sample.highSpeed)
                .put("p95Ms", sample.p95FrameMs).put("p99Ms", sample.p99FrameMs)
                .put("startNanos", sample.startNanos).put("endNanos", sample.endNanos))
            decision.gain?.let { gain ->
                value.getJSONObject("recommendation").put("gain", JSONObject()
                    .put("low", gain.low).put("mean", gain.mean).put("high", gain.high)
                    .put("baselineLow", gain.baselineLow).put("baselineMean", gain.baselineMean)
                    .put("baselineHigh", gain.baselineHigh))
            }
        }
        value.put("state", "recommended")
        value.put("comparisonReport", comparisonReport(value))
    }

    /** CodexAstraLocal: Read-only export of measured primary/secondary comparisons.
     * Each candidate keeps its own verified A/B/A interpolated baseline. Only an
     * explicitly requested, accepted recommendation supplies the secondary reference.
     * No resolution bridge is inferred from archived sessions or missing identities. */
    fun comparisonReport(value: JSONObject): JSONObject {
        val output = JSONObject().put("schema", 1).put("titleId", title)
            .put("scene", value.getString("scene")).put("conditions", value.getString("conditions"))
            .put("currentContext", isContextCurrent(value))
            .put("measurement", "120-system-interval bins; actual guest throughput; not one-second sections")
            .put("resolutionBridge", JSONObject.NULL)
            .put("resolutionBridgeReason", "No explicit cross-resolution baseline pair in this journal")
        val nativeContext = value.getString("nativeContext")
        fun field(name: String) = nativeContext.split(';').firstOrNull { it.startsWith("$name=") }
            ?.substringAfter('=') ?: ""
        val reportContext = UberharTuningReport.Context(field("build"), value.getString("fixedSettings"),
            field("actual_driver"), field("uberhar_test_mode"),
            field("resolution_factor").toDoubleOrNull()?.toInt() ?: 0,
            value.getString("conditions"), "system_interval_bins:$GUEST_FRAMES:$BIN_FRAMES", value.getString("scene"))
        fun settings(features: Set<String>) = mutableMapOf(
            "native_context" to nativeContext, "fixed_settings" to value.getString("fixedSettings"),
            "effective_limit" to if (value.isNull("effectiveLimit")) "unknown" else value.getDouble("effectiveLimit").toString()
        ).apply { featureKeys.forEach { put(it, (it in features).toString()) } }
        fun row(id: String, features: Set<String>, low: Double, mean: Double, high: Double) =
            UberharTuningReport.Row(id, reportContext, settings(features), low, mean, high, confirmed = true)
        val recommendation = value.optJSONObject("recommendation")
        val selectedFeatures = recommendation?.optJSONArray("features")?.let { strings(it) }
        val selectedId = if (recommendation?.optBoolean("accepted", false) == true)
            candidates(value).firstOrNull { it.features == selectedFeatures }?.id else null
        val measurement = recommendation?.optJSONObject("measurement")
        val selected = if (selectedId != null && measurement != null) row(selectedId, requireNotNull(selectedFeatures),
            measurement.getDouble("low"), measurement.getDouble("mean"), measurement.getDouble("high")) else null
        output.put("selectedOptimalId", selectedId ?: JSONObject.NULL)
        fun delta(d: UberharTuningReport.Delta?): Any = d?.let {
            fun metric(m: UberharTuningReport.MetricDelta) = JSONObject()
                .put("percentagePoints", m.percentagePoints).put("relativePercent", m.relativePercent)
            JSONObject().put("referenceId", it.referenceId).put("low", metric(it.low))
                .put("overall", metric(it.overall)).put("high", metric(it.high))
        } ?: JSONObject.NULL
        val rows = JSONArray()
        engine(value).decisions().forEach { (id, decision) ->
            val evidence = decision.evidence.candidate
            val gain = decision.gain
            val entry = JSONObject().put("id", id).put("status", decision.status.name)
                .put("features", array(evidence.features)).put("low", evidence.lowSpeed)
                .put("overall", evidence.meanSpeed).put("high", evidence.highSpeed)
                .put("startNanos", evidence.startNanos).put("endNanos", evidence.endNanos)
            if (gain != null) {
                val baseline = row("baseline:$id", emptySet(), gain.baselineLow, gain.baselineMean, gain.baselineHigh)
                val candidate = row(id, evidence.features, evidence.lowSpeed, evidence.meanSpeed, evidence.highSpeed)
                val input = listOf(baseline, candidate) + listOfNotNull(selected?.takeIf { it.id != id })
                val result = UberharTuningReport.compare(input,
                    UberharTuningReport.Plan(baseline.id, selected?.id), featureKeys).first { it.rowId == id }
                entry.put("baseline", JSONObject().put("id", baseline.id).put("low", baseline.low)
                    .put("overall", baseline.overall).put("high", baseline.high)
                    .put("method", "matched A/B/A time-interpolated reference"))
                    .put("primary", delta(result.primary)).put("secondaryOptimal", delta(result.secondaryOptimal))
                    .put("qualifications", JSONArray(result.qualifications))
            } else entry.put("primary", JSONObject.NULL).put("secondaryOptimal", JSONObject.NULL)
                .put("qualifications", JSONArray(decision.reasons.map { it.name }))
            rows.put(entry)
        }
        return output.put("rows", rows)
    }

    fun invalidate(reason: String) {
        val value = read() ?: return
        if (value.getString("state") in listOf("cancelled")) return
        value.put("state", "invalid").put("invalidReason", reason); save(value)
        if (NativeLibrary.isRunning() && title == "%016X".format(NativeLibrary.getRunningTitleId())) NativeLibrary.cancelTuningSample()
    }

    // CodexAstraLocal: Ending a session changes evidence state only; current preferences stay intact.
    fun cancel() {
        val value = requireNotNull(read())
        if (value.getString("state") in listOf("arming", "measuring") && NativeLibrary.isRunning() &&
            title == "%016X".format(NativeLibrary.getRunningTitleId())) NativeLibrary.cancelTuningSample()
        value.put("state", "cancelled")
        save(value)
    }

    fun manualChange(key: String) {
        val value = read() ?: return
        if (value.getString("state") == "cancelled") return
        // Candidate edits are expected only between samples. Fixed-context edits end validity.
        if (key !in featureKeys || value.getString("state") in listOf("arming", "measuring")) {
            invalidate("A preference changed during this advisory trial.")
        }
    }

    companion object {
        const val GUEST_FRAMES = 1200
        const val BIN_FRAMES = 120
        val featureKeys = linkedSetOf("uberhar_changed_state_only", "uberhar_reuse_contained_pass",
            "uberhar_typed_primitive_append", "uberhar_include_scissor", "uberhar_adaptive_cpu_workers", "uberhar_prepare_displayed_eye_only")
        fun strings(a: JSONArray): Set<String> = (0 until a.length()).map { a.getString(it) }.toSet()
        fun array(s: Set<String>) = JSONArray(s.sorted())
        fun JSONArray.objects(): List<JSONObject> = (0 until length()).map { getJSONObject(it) }
    }
}

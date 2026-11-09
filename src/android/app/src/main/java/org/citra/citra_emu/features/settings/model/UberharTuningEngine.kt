// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import java.util.Collections
import kotlin.math.abs

/**
 * CodexAstraLocal: Bounded, launch-aware tuning decisions, independent of Android and JNI.
 * The owner manually chooses A/B/A profiles of a confirmed scene; the caller persists
 * completed comparisons for deterministic replay. This class never applies settings.
 * Low/high describe fixed guest-frame bins, NOT wall-clock five-second windows. Bins are
 * correlated observations, not independent trials or a statistical confidence interval.
 */
class UberharTuningEngine(
    val contextId: String,
    val sceneId: String,
    baselineFeatures: Set<String>,
    candidates: List<Candidate>,
    val policy: Policy = Policy()
) {
    data class Candidate(val id: String, val features: Set<String>)

    data class Policy(
        val guestFrames: Int = 1200,
        val binGuestFrames: Int = 120,
        val usefulGain: Double = 0.03,
        val maximumRegression: Double = 0.02,
        val maximumBaselineDrift: Double = 0.05
    ) {
        init {
            require(guestFrames > 0 && binGuestFrames > 0 && guestFrames % binGuestFrames == 0)
            require(guestFrames / binGuestFrames >= 2)
            require(usefulGain.isFinite() && usefulGain > 0.0)
            require(maximumRegression.isFinite() && maximumRegression in 0.0..0.25)
            require(maximumBaselineDrift.isFinite() && maximumBaselineDrift in 0.0..0.25)
        }
    }

    data class Sample(
        val contextId: String,
        val sceneId: String,
        val features: Set<String>,
        val guestFrames: Int,
        val binGuestFrames: Int,
        val startNanos: Long,
        val endNanos: Long,
        val lowSpeed: Double,
        val meanSpeed: Double,
        val highSpeed: Double,
        val p95FrameMs: Double,
        val p99FrameMs: Double,
        val paused: Boolean = false,
        val pacingLimited: Boolean = false,
        val valid: Boolean = true,
        val sceneConfirmed: Boolean = false,
        val binCount: Int = 0
    )

    data class Comparison(val before: Sample, val candidate: Sample, val after: Sample)
    enum class Status { ACCEPT, REJECT, INCONCLUSIVE }
    enum class Reason {
        USEFUL_GAIN, NO_USEFUL_GAIN, LOW_REGRESSION, MEAN_REGRESSION,
        CONTEXT_MISMATCH, SCENE_MISMATCH, SCENE_UNCONFIRMED, FEATURES_MISMATCH,
        INVALID_MEASUREMENT, PAUSED, PACING_LIMITED, FRAME_COUNT_MISMATCH,
        INVALID_TIME_ORDER, BASELINE_DRIFT, NO_ACCEPTED_CANDIDATE,
        FRESH_CONFIRMATION_REQUIRED, FINAL_CONFIRMATION_FAILED
    }

    data class Gain(
        val low: Double,
        val mean: Double,
        val high: Double,
        val baselineLow: Double,
        val baselineMean: Double,
        val baselineHigh: Double,
        val baselineLowDrift: Double,
        val baselineMeanDrift: Double
    )

    data class Decision(
        val status: Status,
        val reasons: List<Reason>,
        val gain: Gain?,
        val attempt: Int,
        val retryAllowed: Boolean,
        val evidence: Comparison
    )

    data class Recommendation(
        val features: Set<String>,
        val accepted: Boolean,
        val requiresBaselineRestore: Boolean,
        val reasons: List<Reason>,
        val decision: Decision?,
        val contextId: String,
        val sceneId: String
    )

    val baselineFeatures: Set<String> = immutableFeatures(baselineFeatures)
    val candidates: List<Candidate> = Collections.unmodifiableList(
        candidates.map { Candidate(it.id, immutableFeatures(it.features)) }
    )
    private val results = linkedMapOf<String, Decision>()
    private val finished = mutableSetOf<String>()
    private var latestCandidateId: String? = null
    private var lastAcceptedEvidenceEndNanos = 0L
    private var reusableTrailingBaseline: Sample? = null
    private var finalRecommendation: Recommendation? = null

    init {
        require(contextId.isNotBlank() && sceneId.isNotBlank())
        require(this.candidates.size <= MAX_CANDIDATES)
        require(this.candidates.all { it.id.isNotBlank() && it.features != this.baselineFeatures })
        require(this.candidates.map { it.id }.distinct().size == this.candidates.size)
        require(this.candidates.map { it.features }.distinct().size == this.candidates.size)
    }

    // CodexAstraLocal: Explicit pairs retain their own queue entries after a losing singleton.
    fun nextCandidate(): Candidate? =
        if (finalRecommendation != null) null else candidates.firstOrNull { it.id !in finished }

    fun decisions(): Map<String, Decision> = Collections.unmodifiableMap(LinkedHashMap(results))

    fun submit(candidateId: String, comparison: Comparison): Decision {
        check(finalRecommendation == null) { "Session already finalized" }
        val candidate = requireNotNull(nextCandidate()) { "No pending candidate" }
        require(candidate.id == candidateId) { "Submit candidates in the explicit plan order" }
        val attempt = (results[candidateId]?.attempt ?: 0) + 1
        check(attempt <= MAX_ATTEMPTS)
        val decision = evaluate(candidate, snapshot(comparison), attempt, lastAcceptedEvidenceEndNanos)
        results[candidateId] = decision
        latestCandidateId = candidateId
        if (decision.status != Status.INCONCLUSIVE || decision.reasons == listOf(Reason.BASELINE_DRIFT)) {
            lastAcceptedEvidenceEndNanos = comparison.after.endNanos
            reusableTrailingBaseline = decision.evidence.after
        }
        if (!decision.retryAllowed) finished += candidateId
        return decision
    }

    // CodexAstraLocal: An inconclusive first trial permits, but never forces, one retry.
    fun declineRetry(candidateId: String) {
        require(nextCandidate()?.id == candidateId && results[candidateId]?.retryAllowed == true)
        finished += candidateId
    }

    /** Directional ranking only: preserve lows first, then mean, then fewer enabled features. */
    fun bestCandidate(): Candidate? = candidates.filter { results[it.id]?.status == Status.ACCEPT }
        .sortedWith(compareByDescending<Candidate> { results.getValue(it.id).gain!!.low }
            .thenByDescending { results.getValue(it.id).gain!!.mean }
            .thenBy { it.features.size }).firstOrNull()

    /**
     * CodexAstraLocal: Validate the complete selected tuple against ORIGINAL baseline features.
     * Reuse its most recent accepted A/B/A only if no intervening comparison has occurred;
     * otherwise permit one fresh final A/B/A. Failure ends with the baseline, never a guessed
     * second-best profile or a global "optimum". Results are advice, never preference writes.
     */
    fun recommendation(candidateId: String?, fresh: Comparison? = null): Recommendation {
        finalRecommendation?.let { return it }
        val candidate = candidates.firstOrNull { it.id == candidateId }
        val previous = candidateId?.let { results[it] }
        if (candidate == null || previous?.status != Status.ACCEPT) {
            return restored(listOf(Reason.NO_ACCEPTED_CANDIDATE), previous).also {
                finalRecommendation = it
            }
        }
        if (fresh == null && latestCandidateId != candidateId) {
            return restored(listOf(Reason.FRESH_CONFIRMATION_REQUIRED), previous)
        }
        val decision = if (fresh == null) previous else
            evaluate(candidate, snapshot(fresh), MAX_ATTEMPTS, lastAcceptedEvidenceEndNanos)
                .copy(retryAllowed = false)
        val result = if (decision.status == Status.ACCEPT) {
            Recommendation(candidate.features, true, false, decision.reasons, decision, contextId, sceneId)
        } else restored(decision.reasons + Reason.FINAL_CONFIRMATION_FAILED, decision)
        finalRecommendation = result
        return result
    }

    private fun restored(reasons: List<Reason>, decision: Decision?) = Recommendation(
        baselineFeatures, false, true, Collections.unmodifiableList(reasons.toList()),
        decision, contextId, sceneId
    )

    private fun evaluate(candidate: Candidate, c: Comparison, attempt: Int,
                         earliestStartNanos: Long): Decision {
        val invalid = linkedSetOf<Reason>()
        val samples = listOf(c.before, c.candidate, c.after)
        for (s in samples) {
            if (s.contextId != contextId) invalid += Reason.CONTEXT_MISMATCH
            if (s.sceneId != sceneId) invalid += Reason.SCENE_MISMATCH
            if (!s.sceneConfirmed) invalid += Reason.SCENE_UNCONFIRMED
            if (!s.valid) invalid += Reason.INVALID_MEASUREMENT
            if (s.paused) invalid += Reason.PAUSED
            if (s.pacingLimited) invalid += Reason.PACING_LIMITED
            if (s.guestFrames != policy.guestFrames || s.binGuestFrames != policy.binGuestFrames ||
                s.binCount != policy.guestFrames / policy.binGuestFrames) {
                invalid += Reason.FRAME_COUNT_MISMATCH
            }
            if (s.startNanos < 0 || s.endNanos <= s.startNanos) invalid += Reason.INVALID_TIME_ORDER
            if (listOf(s.lowSpeed, s.meanSpeed, s.highSpeed, s.p95FrameMs, s.p99FrameMs)
                    .any { !it.isFinite() || it <= 0.0 } ||
                s.lowSpeed > s.meanSpeed || s.meanSpeed > s.highSpeed || s.p95FrameMs > s.p99FrameMs) {
                invalid += Reason.INVALID_MEASUREMENT
            }
        }
        if (c.before.features != baselineFeatures || c.after.features != baselineFeatures ||
            c.candidate.features != candidate.features) invalid += Reason.FEATURES_MISMATCH
        if (c.before.endNanos > c.candidate.startNanos || c.candidate.endNanos > c.after.startNanos) {
            invalid += Reason.INVALID_TIME_ORDER
        }
        // CodexAstraLocal: Reuse the exact previous trailing A as the next leading A to
        // save one launch. Altered, older or overlapping evidence is not a fresh bracket.
        if (c.before.startNanos < earliestStartNanos && c.before != reusableTrailingBaseline) {
            invalid += Reason.INVALID_TIME_ORDER
        }
        if (invalid.isNotEmpty()) return decision(Status.INCONCLUSIVE, invalid, null, attempt, c)

        // CodexAstraLocal: Bracket drift checks precede interpolation; timestamps only weight
        // nearby reference observations, never establish identical scene content by themselves.
        val lowDrift = c.after.lowSpeed / c.before.lowSpeed - 1.0
        val meanDrift = c.after.meanSpeed / c.before.meanSpeed - 1.0
        if (abs(lowDrift) > policy.maximumBaselineDrift || abs(meanDrift) > policy.maximumBaselineDrift) {
            return decision(Status.INCONCLUSIVE, setOf(Reason.BASELINE_DRIFT), null, attempt, c)
        }
        fun midpoint(s: Sample) = s.startNanos.toDouble() + (s.endNanos - s.startNanos).toDouble() / 2.0
        val fraction = (midpoint(c.candidate) - midpoint(c.before)) /
            (midpoint(c.after) - midpoint(c.before))
        fun interpolate(before: Double, after: Double) = before + fraction * (after - before)
        val low = interpolate(c.before.lowSpeed, c.after.lowSpeed)
        val mean = interpolate(c.before.meanSpeed, c.after.meanSpeed)
        val high = interpolate(c.before.highSpeed, c.after.highSpeed)
        val gain = Gain(c.candidate.lowSpeed / low - 1.0, c.candidate.meanSpeed / mean - 1.0,
            c.candidate.highSpeed / high - 1.0, low, mean, high, lowDrift, meanDrift)
        val loss = linkedSetOf<Reason>()
        if (gain.low < -policy.maximumRegression) loss += Reason.LOW_REGRESSION
        if (gain.mean < -policy.maximumRegression) loss += Reason.MEAN_REGRESSION
        if (loss.isNotEmpty()) return decision(Status.REJECT, loss, gain, attempt, c)
        return if (gain.low >= policy.usefulGain || gain.mean >= policy.usefulGain)
            decision(Status.ACCEPT, setOf(Reason.USEFUL_GAIN), gain, attempt, c)
        else decision(Status.REJECT, setOf(Reason.NO_USEFUL_GAIN), gain, attempt, c)
    }

    private fun decision(status: Status, reasons: Collection<Reason>, gain: Gain?, attempt: Int,
                         evidence: Comparison) = Decision(
        status, Collections.unmodifiableList(reasons.toList()), gain, attempt,
        status == Status.INCONCLUSIVE && attempt < MAX_ATTEMPTS, evidence
    )

    private fun snapshot(c: Comparison) = Comparison(
        c.before.copy(features = immutableFeatures(c.before.features)),
        c.candidate.copy(features = immutableFeatures(c.candidate.features)),
        c.after.copy(features = immutableFeatures(c.after.features))
    )

    companion object {
        const val MAX_CANDIDATES = 12
        const val MAX_ATTEMPTS = 2
        private fun immutableFeatures(features: Set<String>): Set<String> {
            require(features.all { it.isNotBlank() })
            return Collections.unmodifiableSet(LinkedHashSet(features))
        }
    }
}

/**
 * CodexAstraLocal: Pure, explicitly anchored report comparisons. No preference or journal writes.
 * Existing archived tuning plans need no migration: absent optimal/bridge IDs default to null,
 * and callers must supply a confirmed primary baseline instead of inventing an inherited one.
 * This does not replace the A/B/A evaluator or promote an unvalidated candidate to a winner.
 */
object UberharTuningReport {
    data class Context(
        val build: String, val device: String, val driver: String, val renderer: String,
        val resolution: Int, val conditions: String, val measurement: String, val scene: String
    )
    data class Row(
        val id: String, val context: Context,
        // Complete resolved settings, including legacy, quality, diagnostics and effective cap.
        // Resolution is represented exactly once, in Context. Unknown provenance is unconfirmed.
        val settings: Map<String, String>,
        val low: Double, val overall: Double, val high: Double,
        val confirmed: Boolean = false
    )
    data class Plan(
        val primaryBaselineId: String,
        val selectedOptimalId: String? = null,
        val originalResolutionBaselineId: String? = null,
        val schema: Int = 1
    )
    data class MetricDelta(val percentagePoints: Double, val relativePercent: Double)
    data class Delta(
        val referenceId: String, val low: MetricDelta, val overall: MetricDelta, val high: MetricDelta
    )
    data class Result(
        val rowId: String, val primary: Delta?, val secondaryOptimal: Delta?,
        val resolutionBridge: Delta?, val qualifications: List<String>
    )

    fun compare(rows: List<Row>, plan: Plan, testedSettingKeys: Set<String>): List<Result> {
        require(plan.schema == 1) { "Unsupported report plan schema; retain archived evidence" }
        require(plan.primaryBaselineId.isNotBlank()) { "An explicit baseline is required" }
        require(rows.map { it.id }.distinct().size == rows.size) { "Ambiguous row identity" }
        require(testedSettingKeys.all { it.isNotBlank() })
        val byId = rows.associateBy { it.id }
        val baseline = byId[plan.primaryBaselineId]
        val optimal = plan.selectedOptimalId?.let { byId[it] }
        val original = plan.originalResolutionBaselineId?.let { byId[it] }

        fun valid(r: Row): Boolean = r.confirmed && r.id.isNotBlank() && r.settings.isNotEmpty() &&
            r.settings.all { it.key.isNotBlank() && it.value.isNotBlank() } &&
            listOf(r.context.build, r.context.device, r.context.driver, r.context.renderer,
                r.context.conditions, r.context.measurement, r.context.scene).all { it.isNotBlank() } &&
            r.context.resolution > 0 && listOf(r.low, r.overall, r.high).all { it.isFinite() && it > 0 } &&
            r.low <= r.overall && r.overall <= r.high
        fun matched(a: Row, b: Row): Boolean = valid(a) && valid(b) && a.context == b.context &&
            a.settings.keys == b.settings.keys && a.settings.keys.all {
                it in testedSettingKeys || a.settings[it] == b.settings[it]
            }
        fun delta(a: Row, b: Row): Delta {
            fun metric(value: Double, reference: Double) =
                MetricDelta(value - reference, (value / reference - 1.0) * 100.0)
            return Delta(b.id, metric(a.low, b.low), metric(a.overall, b.overall), metric(a.high, b.high))
        }
        return rows.map { row ->
            val notes = mutableListOf<String>()
            val primary = if (baseline != null && matched(row, baseline)) delta(row, baseline)
                else null.also { notes += "Primary baseline missing, unconfirmed, or mismatched" }
            // CodexAstraLocal: Never show the reciprocal baseline-versus-winner comparison.
            val secondary = if (row.id == plan.primaryBaselineId || row.id == plan.selectedOptimalId ||
                plan.selectedOptimalId == null) null
            else if (optimal != null && primary != null && matched(row, optimal)) delta(row, optimal)
            else null.also { notes += "Selected-optimal reference missing, unconfirmed, or mismatched" }
            // Only the baseline tuple may bridge resolution. All actual settings must agree;
            // a candidate plus changed resolution is not a resolution-only measurement.
            val bridge = if (row.id == plan.primaryBaselineId && original != null &&
                valid(row) && valid(original) && row.context.resolution != original.context.resolution &&
                row.context.copy(resolution = original.context.resolution) == original.context &&
                row.settings == original.settings) delta(row, original)
            else null.also {
                if (row.id == plan.primaryBaselineId && plan.originalResolutionBaselineId != null)
                    notes += "Resolution bridge requires the exact baseline tuple except resolution"
            }
            Result(row.id, primary, secondary, bridge, notes.toList())
        }
    }
}

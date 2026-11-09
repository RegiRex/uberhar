// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.model.UberharTuningEngine.Candidate
import org.citra.citra_emu.features.settings.model.UberharTuningEngine.Comparison
import org.citra.citra_emu.features.settings.model.UberharTuningEngine.Reason
import org.citra.citra_emu.features.settings.model.UberharTuningEngine.Sample
import org.citra.citra_emu.features.settings.model.UberharTuningEngine.Status
import kotlin.math.abs

// CodexAstraLocal: Run the actual production class on the host, exercising decisions and
// lifecycle invariants rather than substituting a second implementation of the evaluator.
private val single = Candidate("typed", setOf("uberhar_typed_primitive_append"))
private val other = Candidate("adaptive", setOf("uberhar_adaptive_cpu_workers"))
private val pair = Candidate("typed+adaptive", single.features + other.features)
private var checks = 0
private fun expect(value: Boolean, message: String) {
    checks++
    check(value) { message }
}
private inline fun refuses(message: String, block: () -> Unit) {
    checks++
    try { block() } catch (_: IllegalArgumentException) { return }
    catch (_: IllegalStateException) { return }
    error(message)
}
private fun sample(features: Set<String>, slot: Long, low: Double = 80.0,
                   mean: Double = 100.0, high: Double = 120.0) = Sample(
    "title:driver:resolution:cache:conditions", "confirmed-battle", features,
    1200, 120, slot * 1000, slot * 1000 + 100, low, mean, high, 15.0, 19.0,
    sceneConfirmed = true, binCount = 10
)
private fun comparison(candidate: Candidate = single, offset: Long = 0,
                       low: Double = 84.0, mean: Double = 105.0,
                       baseline: Set<String> = emptySet()) = Comparison(
    sample(baseline, offset), sample(candidate.features, offset + 1, low, mean),
    sample(baseline, offset + 2)
)
private fun engine(candidates: List<Candidate> = listOf(single), baseline: Set<String> = emptySet()) =
    UberharTuningEngine("title:driver:resolution:cache:conditions", "confirmed-battle", baseline, candidates)

fun main() {
    // CodexAstraLocal: A losing standalone option must not blacklist its useful explicit pair.
    val plan = engine(listOf(single, pair))
    val loss = plan.submit(single.id, comparison(low = 72.0, mean = 94.0))
    expect(loss.status == Status.REJECT && Reason.LOW_REGRESSION in loss.reasons, "Losing standalone")
    expect(plan.nextCandidate() == pair, "Pair survives rejected standalone")
    val winner = plan.submit(pair.id, comparison(pair, 3, low = 88.0, mean = 112.0))
    expect(winner.status == Status.ACCEPT, "Explicit positive pair is accepted")
    expect(plan.bestCandidate() == pair, "Pair recommendation candidate")
    val adopted = plan.recommendation(pair.id)
    expect(adopted.accepted && adopted.features == pair.features, "Latest accepted A/B/A reused")
    expect(adopted.contextId == plan.contextId && adopted.sceneId == plan.sceneId, "Scoped recommendation")
    expect(plan.nextCandidate() == null, "Final recommendation ends bounded work")
    refuses("Cannot add measurements after finalization") { plan.submit(pair.id, comparison(pair, 6)) }

    // CodexAstraLocal: A faster average/high cannot conceal a material low regression.
    val tails = engine().submit(single.id, comparison(low = 72.0, mean = 115.0))
    expect(tails.status == Status.REJECT && Reason.LOW_REGRESSION in tails.reasons, "Tail regression veto")
    val meanLoss = engine().submit(single.id, comparison(low = 88.0, mean = 95.0))
    expect(meanLoss.status == Status.REJECT && Reason.MEAN_REGRESSION in meanLoss.reasons, "Mean regression veto")
    val highOnly = comparison().let { it.copy(candidate = it.candidate.copy(lowSpeed = 80.0,
        meanSpeed = 100.0, highSpeed = 180.0)) }
    expect(engine().submit(single.id, highOnly).reasons == listOf(Reason.NO_USEFUL_GAIN), "Peak-only not enough")
    expect(engine().submit(single.id, comparison(low = 84.0, mean = 99.0)).status == Status.ACCEPT,
        "Useful low gain with small tolerated mean variation")

    // CodexAstraLocal: Large bracket drift invalidates, allows one retry, then restores baseline.
    val drifting = engine()
    val bad = comparison().let { it.copy(after = it.after.copy(lowSpeed = 68.0, meanSpeed = 85.0)) }
    val first = drifting.submit(single.id, bad)
    expect(first.status == Status.INCONCLUSIVE && first.reasons == listOf(Reason.BASELINE_DRIFT), "Drift invalidates")
    expect(first.retryAllowed && first.attempt == 1, "One drift retry offered")
    val secondBad = comparison(offset = 3).let { it.copy(after = it.after.copy(lowSpeed = 68.0, meanSpeed = 85.0)) }
    val second = drifting.submit(single.id, secondBad)
    expect(second.status == Status.INCONCLUSIVE && !second.retryAllowed && second.attempt == 2, "Retry bounded")
    expect(drifting.nextCandidate() == null, "No infinite inconclusive loop")
    expect(drifting.recommendation(null).requiresBaselineRestore, "No valid result restores baseline")
    val optional = engine()
    optional.submit(single.id, bad)
    optional.declineRetry(single.id)
    expect(optional.nextCandidate() == null, "Retry may be declined to save testing cost")

    // CodexAstraLocal: Mild drift is interpolated at B's time, not attributed to the feature.
    val mild = comparison().let { it.copy(after = it.after.copy(lowSpeed = 83.2, meanSpeed = 104.0,
        highSpeed = 124.8), candidate = it.candidate.copy(lowSpeed = 81.6, meanSpeed = 102.0,
        highSpeed = 122.4)) }
    val corrected = engine().submit(single.id, mild)
    expect(corrected.status == Status.REJECT && abs(corrected.gain!!.mean) < 1e-12,
        "Interpolated environmental gain not credited to feature")

    // CodexAstraLocal: Invalid provenance/capped/paused data cannot create an accepted policy.
    val valid = comparison()
    val invalids = listOf(
        Reason.CONTEXT_MISMATCH to valid.candidate.copy(contextId = "other-driver"),
        Reason.SCENE_MISMATCH to valid.candidate.copy(sceneId = "different-map"),
        Reason.SCENE_UNCONFIRMED to valid.candidate.copy(sceneConfirmed = false),
        Reason.FEATURES_MISMATCH to valid.candidate.copy(features = other.features),
        Reason.PAUSED to valid.candidate.copy(paused = true),
        Reason.PACING_LIMITED to valid.candidate.copy(pacingLimited = true),
        Reason.FRAME_COUNT_MISMATCH to valid.candidate.copy(guestFrames = 1199),
        Reason.FRAME_COUNT_MISMATCH to valid.candidate.copy(binCount = 9),
        Reason.FRAME_COUNT_MISMATCH to valid.candidate.copy(binGuestFrames = 60),
        Reason.INVALID_TIME_ORDER to valid.candidate.copy(startNanos = 0),
        Reason.INVALID_MEASUREMENT to valid.candidate.copy(meanSpeed = Double.NaN),
        Reason.INVALID_MEASUREMENT to valid.candidate.copy(p99FrameMs = Double.POSITIVE_INFINITY),
        Reason.INVALID_MEASUREMENT to valid.candidate.copy(valid = false)
    )
    for ((reason, sample) in invalids) {
        val result = engine().submit(single.id, valid.copy(candidate = sample))
        expect(result.status == Status.INCONCLUSIVE && reason in result.reasons, "Invalid measurement: $reason")
    }

    // CodexAstraLocal: An older winner requires fresh ORIGINAL-baseline confirmation.
    val original = setOf("original-feature")
    val selected = Candidate("keep-original-plus-typed", original + single.features)
    val loser = Candidate("alternate", original + other.features)
    val finalCheck = engine(listOf(selected, loser), original)
    finalCheck.submit(selected.id, comparison(selected, baseline = original))
    finalCheck.submit(loser.id, comparison(loser, 3, low = 74.0, mean = 95.0, baseline = original))
    expect(finalCheck.recommendation(selected.id).reasons == listOf(Reason.FRESH_CONFIRMATION_REQUIRED),
        "Older winner cannot silently reuse stale result")
    val failed = finalCheck.recommendation(selected.id,
        comparison(selected, 6, low = 70.0, mean = 90.0, baseline = original))
    expect(!failed.accepted && failed.requiresBaselineRestore && failed.features == original,
        "Failed final confirmation restores ORIGINAL features")
    expect(Reason.FINAL_CONFIRMATION_FAILED in failed.reasons, "Failure reason retained")
    expect(finalCheck.recommendation(selected.id, comparison(selected, 9, baseline = original)) == failed,
        "Final failure does not allow unbounded cherry-picked confirmations")

    val freshPass = engine(listOf(single, other))
    freshPass.submit(single.id, comparison())
    freshPass.submit(other.id, comparison(other, 3, low = 80.0, mean = 100.0))
    expect(freshPass.recommendation(single.id, comparison(offset = 6)).accepted, "Fresh confirmation passes")
    val stale = engine(listOf(single, other))
    stale.submit(single.id, comparison())
    stale.submit(other.id, comparison(other, 3, low = 80.0, mean = 100.0))
    val oldReuse = stale.recommendation(single.id, comparison())
    expect(!oldReuse.accepted && Reason.INVALID_TIME_ORDER in oldReuse.reasons, "Cannot relabel stale evidence fresh")

    // CodexAstraLocal: Persistence can reconstruct decisions by replaying the finite ledger.
    val ledger = listOf(single.id to bad, single.id to comparison(offset = 3),
        pair.id to comparison(pair, 6, low = 89.0, mean = 110.0))
    val firstReplay = engine(listOf(single, pair)); val secondReplay = engine(listOf(single, pair))
    for ((id, c) in ledger) { firstReplay.submit(id, c); secondReplay.submit(id, c) }
    expect(firstReplay.decisions() == secondReplay.decisions(), "Deterministic ledger replay")
    expect(firstReplay.recommendation(pair.id) == secondReplay.recommendation(pair.id), "Deterministic recommendation")

    // CodexAstraLocal: Adjacent comparisons share precisely one unchanged baseline sample.
    val shared = engine(listOf(single, pair))
    val initial = comparison()
    shared.submit(single.id, initial)
    val adjacent = comparison(pair, 3).copy(before = initial.after)
    expect(shared.submit(pair.id, adjacent).status == Status.ACCEPT, "Reuse trailing A saves launch")
    val finalAdjacent = comparison(offset = 6).copy(before = adjacent.after)
    expect(shared.recommendation(single.id, finalAdjacent).accepted, "Final confirmation may share trailing A")
    val modifiedShared = engine(listOf(single, pair))
    modifiedShared.submit(single.id, initial)
    val altered = adjacent.copy(before = initial.after.copy(lowSpeed = 79.9))
    val alteredResult = modifiedShared.submit(pair.id, altered)
    expect(alteredResult.status == Status.INCONCLUSIVE && Reason.INVALID_TIME_ORDER in alteredResult.reasons,
        "Only byte-equivalent measured baseline fields can be reused")

    // CodexAstraLocal: Caller mutations must not change the session's candidate/evidence identity.
    val mutable = mutableSetOf("typed")
    val frozen = engine(listOf(Candidate("frozen", mutable)))
    mutable += "new-feature"
    expect(frozen.nextCandidate()!!.features == setOf("typed"), "Candidate features copied")
    val copyFeatures = mutableSetOf("typed")
    val frozenInput = comparison(Candidate("frozen", copyFeatures))
    val frozenDecision = frozen.submit("frozen", frozenInput)
    copyFeatures += "late-change"
    expect(frozenDecision.evidence.candidate.features == setOf("typed"), "Evidence features copied")
    refuses("Bound candidates to12") { engine((1..13).map { Candidate("c$it", setOf("f$it")) }) }
    refuses("Duplicate tuples waste budget") { engine(listOf(single, Candidate("same", single.features))) }
    refuses("Out-of-order plan not reproducible") { engine(listOf(single, pair)).submit(pair.id, comparison(pair)) }
    expect(engine(emptyList()).recommendation(null).features.isEmpty(), "Empty plan restores empty baseline")
    // CodexAstraLocal: Report anchors are explicit and preserve both units of change.
    val rc = UberharTuningReport.Context("build", "device", "driver", "renderer", 2,
        "cold/Standard", "one-second-system-sections", "confirmed-battle")
    val base = UberharTuningReport.Row("base", rc, mapOf("typed" to "false", "observer" to "true"),
        80.0, 100.0, 120.0, true)
    val candidate = base.copy(id = "typed", settings = base.settings + ("typed" to "true"),
        low = 88.0, overall = 110.0, high = 132.0)
    val selectedReport = candidate.copy(id = "selected", low = 96.0, overall = 120.0, high = 144.0)
    val reports = UberharTuningReport.compare(listOf(base, candidate, selectedReport),
        UberharTuningReport.Plan("base", "selected"), setOf("typed"))
    expect(reports[0].primary!!.low.percentagePoints == 0.0 && reports[0].secondaryOptimal == null,
        "Baseline is not reciprocally compared against selected optimal")
    expect(abs(reports[1].primary!!.overall.relativePercent - 10.0) < 1e-10 &&
        reports[1].primary!!.overall.percentagePoints == 10.0, "Percentage points and relative percent separate")
    expect(abs(reports[1].secondaryOptimal!!.overall.relativePercent + 8.3333333333333) < 1e-9,
        "Candidate also compares to explicitly selected measured tuple")
    expect(reports[2].secondaryOptimal == null, "No optimal self comparison")
    for (bad in listOf(candidate.copy(context = rc.copy(driver = "other")),
        candidate.copy(context = rc.copy(resolution = 4)), candidate.copy(context = rc.copy(scene = "other")),
        candidate.copy(settings = candidate.settings + ("observer" to "false")),
        candidate.copy(confirmed = false))) {
        expect(UberharTuningReport.compare(listOf(base, bad), UberharTuningReport.Plan("base"),
            setOf("typed"))[1].primary == null, "Mismatched or unconfirmed primary stays unknown")
    }
    val highResBase = base.copy(id = "4x", context = rc.copy(resolution = 4))
    val bridge = UberharTuningReport.compare(listOf(base, highResBase),
        UberharTuningReport.Plan("4x", originalResolutionBaselineId = "base"), setOf("typed"))
    expect(bridge[1].resolutionBridge != null && bridge[0].primary == null, "Exact baseline resolution bridge only")
    val badBridge = highResBase.copy(settings = candidate.settings)
    expect(UberharTuningReport.compare(listOf(base, badBridge),
        UberharTuningReport.Plan("4x", originalResolutionBaselineId = "base"), setOf("typed"))[1]
        .resolutionBridge == null, "Feature plus resolution is not resolution-only")
    expect(UberharTuningReport.compare(listOf(base, candidate), UberharTuningReport.Plan("base"),
        setOf("typed"))[1].secondaryOptimal == null, "Archived absent optimum remains absent")
    println("PASS: $checks production tuning-engine/report assertions; synthetic measurements, no Android/device claims")
}

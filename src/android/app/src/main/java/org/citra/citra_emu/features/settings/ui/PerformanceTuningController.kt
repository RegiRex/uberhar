// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.ui

import android.text.InputType
import android.widget.EditText
import android.widget.LinearLayout
import androidx.core.view.isVisible
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.model.PerformanceTuningSession
import org.citra.citra_emu.features.settings.model.TestingExperiment
import org.citra.citra_emu.features.settings.model.TestingSettings
import org.citra.citra_emu.features.settings.model.UberharTuningEngine
import org.json.JSONObject
import java.util.Locale

/** CodexAstraLocal: Opt-in advice only. Every profile edit and launch remains a manual owner
 * action; the UI can mark/collect a native sample but cannot apply or restore preferences. */
class PerformanceTuningController(
    private val parent: LinearLayout,
    gameId: String,
    private val changed: () -> Unit
) {
    private val context get() = parent.context
    private val session = if (gameId.isNotEmpty()) PerformanceTuningSession(context, gameId) else null
    private val button = MaterialButton(context, null,
        com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
        setText(R.string.tuning_title)
        setOnClickListener { open() }
    }
    private val catalog = listOf(
        "S" to setOf("uberhar_changed_state_only"),
        "C" to setOf("uberhar_reuse_contained_pass"),
        "T" to setOf("uberhar_typed_primitive_append"),
        "I" to setOf("uberhar_include_scissor"),
        "A" to setOf("uberhar_adaptive_cpu_workers"),
        "E" to setOf("uberhar_prepare_displayed_eye_only"),
        "SE" to setOf("uberhar_changed_state_only", "uberhar_prepare_displayed_eye_only"),
        "TE" to setOf("uberhar_typed_primitive_append", "uberhar_prepare_displayed_eye_only"),
        "TI" to setOf("uberhar_typed_primitive_append", "uberhar_include_scissor"),
        "CA" to setOf("uberhar_reuse_contained_pass", "uberhar_adaptive_cpu_workers")
    ).map { UberharTuningEngine.Candidate(it.first, it.second) }

    init { parent.addView(button) }

    fun render() {
        button.isVisible = session != null && TestingSettings.available
        button.setText(if (session?.exists() == true) R.string.tuning_resume else R.string.tuning_title)
    }

    fun manualChange(key: String) {
        runCatching { session?.manualChange(key) }
        render()
    }

    private fun names(features: Set<String>): String = if (features.isEmpty()) {
        context.getString(R.string.tuning_baseline)
    } else features.map { key ->
        TestingExperiment.entries.firstOrNull { it.key == key }?.let { context.getString(it.title) } ?: key
    }.sorted().joinToString(" + ")

    private fun message(text: String) = MaterialAlertDialogBuilder(context)
        .setTitle(R.string.tuning_title).setMessage(text).setPositiveButton(android.R.string.ok, null).show()

    private fun act(invalidateOnFailure: Boolean = false, action: () -> Unit) {
        runCatching(action).onFailure { error ->
            if (invalidateOnFailure) runCatching { session?.invalidate(error.message ?: "Trial validation failed.") }
            message(error.message ?: context.getString(R.string.tuning_storage_error))
        }
        changed()
        render()
    }

    private fun open(): Unit = act {
        val store = requireNotNull(session)
        val value = store.read()
        if (value == null || value.getString("state") == "cancelled") {
            newPlan()
            return@act
        }
        val state = value.getString("state")
        val currentContext = store.isContextCurrent(value)
        val running = EmulationActivity.isRunning() || NativeLibrary.isRunning()
        val actions = mutableListOf<Pair<String, () -> Unit>>()
        fun add(label: Int, action: () -> Unit) { actions += context.getString(label) to action }
        if (currentContext && state == "prepare" && !running) {
            add(R.string.tuning_next_profile) { showNext(value, acknowledge = true) }
            val controller = store.engine(value)
            controller.nextCandidate()?.let { candidate ->
                if (controller.decisions()[candidate.id]?.retryAllowed == true) {
                    add(R.string.tuning_skip_retry) { act { store.declineRetry(); open() } }
                }
            }
        }
        if (currentContext && state == "ready") {
            add(R.string.tuning_view_profile) { showNext(value, acknowledge = false) }
            if (running) add(R.string.tuning_begin) { confirmBegin(value) }
        }
        if (currentContext && state in listOf("arming", "measuring")) add(R.string.tuning_collect) { confirmCollect(value) }
        if (currentContext && state in listOf("prepare", "results") && value.isNull("trial")) {
            add(R.string.tuning_recommend) { act(true) { store.recommend(); open() } }
        }
        add(R.string.tuning_evidence) { message(evidence(value)) }
        // CodexAstraLocal: Export text contains evidence only; Android's chooser leaves
        // destination and sharing to the owner. No profile or preference is applied.
        actions += "Export measured comparisons" to {
            act {
                val report = store.comparisonReport(value).toString(2)
                val intent = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                    type = "application/json"
                    putExtra(android.content.Intent.EXTRA_TEXT, report)
                }
                context.startActivity(android.content.Intent.createChooser(intent, "Measured scene comparisons"))
            }
        }
        add(R.string.tuning_end) {
            MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_end)
                .setMessage(R.string.tuning_end_description)
                .setPositiveButton(android.R.string.ok) { _, _ -> act { store.cancel() } }
                .setNegativeButton(android.R.string.cancel, null).show()
        }
        val summary = value.getString("scene") + "\n" + value.getString("conditions") + "\n" +
            context.getString(if (currentContext) R.string.tuning_advisory else R.string.tuning_stale) + "\n" + when (state) {
                "invalid" -> value.optString("invalidReason", context.getString(R.string.tuning_invalid))
                "recommended" -> recommendation(value)
                "arming", "measuring" -> context.getString(R.string.tuning_collect_before_launch)
                "prepare", "ready" -> context.getString(R.string.tuning_next_step, store.step(value))
                else -> context.getString(R.string.tuning_results_ready)
            }
        // CodexAstraLocal: Keep the title compact; long scene/condition details belong in
        // the scrollable action list and a separate message, not an oversized dialog header.
        actions.add(0, context.getString(R.string.tuning_session_details,
            if (currentContext) state else context.getString(R.string.tuning_stale_status)) to { message(summary); Unit })
        MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_title)
            .setItems(actions.map { it.first }.toTypedArray()) { _, which -> actions[which].second() }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun newPlan() {
        if (EmulationActivity.isRunning() || NativeLibrary.isRunning()) {
            message(context.getString(R.string.tuning_close_first)); return
        }
        if (!TestingSettings.enabled || TestingSettings.preset !in 0..2) {
            message(context.getString(R.string.tuning_enable_first)); return
        }
        val scene = EditText(context).apply {
            hint = context.getString(R.string.tuning_scene_hint)
            inputType = InputType.TYPE_CLASS_TEXT
            maxLines = 2
        }
        val conditions = EditText(context).apply {
            hint = context.getString(R.string.tuning_conditions_hint)
            inputType = InputType.TYPE_CLASS_TEXT
            maxLines = 3
        }
        val layout = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, 0, pad, 0)
            addView(scene); addView(conditions)
        }
        val dialog = MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_title)
            .setMessage(R.string.tuning_plan_description).setView(layout)
            .setPositiveButton(R.string.tuning_choose, null).setNegativeButton(android.R.string.cancel, null).create()
        dialog.setOnShowListener {
            dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val s = scene.text.toString().trim(); val c = conditions.text.toString().trim()
                if (s.isBlank() || s.length > 120) scene.error = context.getString(R.string.tuning_scene_required)
                else if (c.isBlank() || c.length > 240) conditions.error = context.getString(R.string.tuning_conditions_required)
                else { dialog.dismiss(); choose(s, c, limit = 3, selected = BooleanArray(catalog.size)) }
            }
        }
        dialog.show()
    }

    private fun choose(scene: String, conditions: String, limit: Int, selected: BooleanArray) {
        val dialog = MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_choose)
            .setMultiChoiceItems(catalog.map { names(it.features) }.toTypedArray(), selected) { _, i, yes -> selected[i] = yes }
            .setPositiveButton(R.string.tuning_review_plan, null)
            .setNegativeButton(android.R.string.cancel, null)
            .apply {
                if (limit == 3) setNeutralButton(R.string.tuning_expand) { _, _ ->
                    choose(scene, conditions, UberharTuningEngine.MAX_CANDIDATES, selected)
                }
            }.create()
        dialog.setOnShowListener {
            dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val choices = catalog.filterIndexed { i, _ -> selected[i] }
                if (choices.size !in 1..limit) message(context.getString(R.string.tuning_candidate_limit, limit))
                else {
                    dialog.dismiss()
                    MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_review_plan)
                        .setMessage(context.getString(R.string.tuning_budget, choices.size, 1 + 2 * choices.size, 4 * choices.size + 3))
                        .setPositiveButton(R.string.tuning_start_plan) { _, _ -> act { requireNotNull(session).start(scene, conditions, choices); open() } }
                        .setNegativeButton(android.R.string.cancel, null).show()
                }
            }
        }
        dialog.show()
    }

    private fun showNext(value: JSONObject, acknowledge: Boolean) {
        val store = requireNotNull(session)
        val wanted = store.nextFeatures(value)
        val lines = PerformanceTuningSession.featureKeys.joinToString("\n") {
            names(setOf(it)) + ": " + context.getString(if (it in wanted) R.string.tuning_on else R.string.tuning_off)
        }
        MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_next_profile)
            .setMessage(context.getString(R.string.tuning_manual_profile, store.step(value), lines))
            .setPositiveButton(android.R.string.ok) { _, _ ->
                if (acknowledge) act(true) { store.prepare() }
            }.setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun confirmBegin(value: JSONObject) {
        MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_begin)
            .setMessage(context.getString(R.string.tuning_begin_description, value.getString("scene"), value.getString("conditions")))
            .setPositiveButton(R.string.tuning_confirm_scene) { _, _ -> act(true) {
                requireNotNull(session).begin(); message(context.getString(R.string.tuning_started))
            } }.setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun confirmCollect(value: JSONObject) {
        MaterialAlertDialogBuilder(context).setTitle(R.string.tuning_collect)
            .setMessage(context.getString(R.string.tuning_collect_description, value.getString("scene")))
            .setPositiveButton(R.string.tuning_confirm_scene) { _, _ -> act(true) { message(requireNotNull(session).collect(true)) } }
            .setNeutralButton(R.string.tuning_scene_changed) { _, _ -> act { requireNotNull(session).invalidate("Owner reported a scene, conditions or graphics correctness problem.") } }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun recommendation(value: JSONObject): String {
        val r = value.getJSONObject("recommendation")
        if (!requireNotNull(session).isContextCurrent(value) || value.getString("state") == "invalid") {
            return context.getString(R.string.tuning_historical_recommendation,
                names(PerformanceTuningSession.strings(r.getJSONArray("features"))))
        }
        return context.getString(if (r.getBoolean("accepted")) R.string.tuning_recommendation else R.string.tuning_no_winner,
            names(PerformanceTuningSession.strings(r.getJSONArray("features"))))
    }

    private fun evidence(value: JSONObject): String {
        val controller = requireNotNull(session).engine(value)
        val lines = mutableListOf(context.getString(R.string.tuning_evidence_scope))
        if (!requireNotNull(session).isContextCurrent(value)) lines.add(0, context.getString(R.string.tuning_stale))
        controller.decisions().forEach { (id, decision) ->
            val candidate = controller.candidates.first { it.id == id }
            val s = decision.evidence.candidate
            lines += "\n${names(candidate.features)}: ${decision.status}\n" +
                String.format(Locale.ROOT, "low / mean / high %.2f / %.2f / %.2f%%; p95 / p99 %.2f / %.2f ms", s.lowSpeed, s.meanSpeed, s.highSpeed, s.p95FrameMs, s.p99FrameMs)
            decision.gain?.let { g ->
                lines += String.format(Locale.ROOT, "Baseline %.2f / %.2f / %.2f%%; change %+.2f / %+.2f / %+.2f%%", g.baselineLow, g.baselineMean, g.baselineHigh, g.low * 100, g.mean * 100, g.high * 100)
            }
            lines += decision.reasons.joinToString { it.name.lowercase().replace('_', ' ') }
        }
        if (value.has("recommendation")) {
            lines += "\n" + recommendation(value)
            value.getJSONObject("recommendation").optJSONObject("measurement")?.let { m ->
                lines += String.format(Locale.ROOT, "Final evidence %.2f / %.2f / %.2f%%; p95 / p99 %.2f / %.2f ms",
                    m.getDouble("low"), m.getDouble("mean"), m.getDouble("high"), m.getDouble("p95Ms"), m.getDouble("p99Ms"))
            }
            if (value.getString("state") == "invalid") lines += context.getString(R.string.tuning_stale)
        }
        // CodexAstraLocal: Primary changes always use the matching measured bracket.
        // Secondary changes appear only after explicit accepted recommendation.
        val report = requireNotNull(session).comparisonReport(value)
        val rows = report.getJSONArray("rows")
        for (i in 0 until rows.length()) {
            val row = rows.getJSONObject(i)
            for ((key, label) in listOf("primary" to "Matching baseline", "secondaryOptimal" to "Selected measured profile")) {
                row.optJSONObject(key)?.let { delta ->
                    val metrics = listOf("low", "overall", "high").map { delta.getJSONObject(it) }
                    lines += String.format(Locale.ROOT,
                        "\n%s vs %s (%s): Low / Overall / High\n%+.2f / %+.2f / %+.2f percentage points\n%+.2f / %+.2f / %+.2f%% relative",
                        row.getString("id"), label, delta.getString("referenceId"),
                        metrics[0].getDouble("percentagePoints"), metrics[1].getDouble("percentagePoints"), metrics[2].getDouble("percentagePoints"),
                        metrics[0].getDouble("relativePercent"), metrics[1].getDouble("relativePercent"), metrics[2].getDouble("relativePercent"))
                }
            }
        }
        return lines.joinToString("\n")
    }
}

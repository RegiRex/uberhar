// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Explicit session selection plus save/share actions with recoverable picker state.
package org.citra.citra_emu.fragments

import android.app.Dialog
import android.content.ClipData
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.core.content.FileProvider
import androidx.fragment.app.DialogFragment
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.radiobutton.MaterialRadioButton
import java.io.File
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.LogExportNames
import org.citra.citra_emu.utils.LogExporter

class LogExportDialogFragment : DialogFragment() {
    private var choices = emptyList<LogExporter.Choice>()
    private var selected = 0
    private var style = LogExportNames.Style.INITIALS
    private var pendingSnapshot: String? = null
    private var busy = true
    private var loading = false
    private var copying = false
    private lateinit var sessions: RadioGroup
    private lateinit var styles: RadioGroup
    private lateinit var preview: TextView
    private lateinit var progress: ProgressBar

    // AstraEH: CreateDocument lets the user choose Downloads or another provider without
    // adding storage permissions. Keep the staged path across rotation/process recreation.
    private val saveDocument =
        registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
            val relative = pendingSnapshot
            pendingSnapshot = null
            if (uri != null && relative != null) {
                val app = requireContext().applicationContext
                copying = true
                lifecycleScope.launch {
                    setBusy(true)
                    try {
                        withContext(Dispatchers.IO) {
                            LogExporter.save(app, LogExporter.restore(app, relative), uri)
                        }
                        Toast.makeText(app, R.string.log_export_saved, Toast.LENGTH_LONG).show()
                        dismiss()
                    } catch (e: CancellationException) {
                        throw e
                    } catch (e: Exception) {
                        showError()
                        if (choices.isEmpty()) loadChoices()
                    } finally {
                        copying = false
                        setBusy(loading)
                    }
                }
            } else {
                setBusy(false)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        selected = savedInstanceState?.getInt("log_selection") ?: 0
        style =
            LogExportNames.Style.entries.getOrElse(savedInstanceState?.getInt("log_style") ?: 0) {
                LogExportNames.Style.INITIALS
            }
        pendingSnapshot = savedInstanceState?.getString("log_snapshot")
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt("log_selection", selected)
        outState.putInt("log_style", style.ordinal)
        outState.putString("log_snapshot", pendingSnapshot)
    }

    // AstraEH: Two visible session choices avoid guessing from a stale application flag.
    // A scrollable native dialog works on both Thor displays and larger accessibility text.
    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val ctx = requireContext()
        val padding = (24 * resources.displayMetrics.density).toInt()
        val content = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding / 2, padding, padding / 2)
        }
        content.addView(TextView(ctx).apply { setText(R.string.log_export_choose) })
        progress = ProgressBar(ctx)
        content.addView(progress)
        sessions = RadioGroup(ctx)
        content.addView(sessions)
        content.addView(TextView(ctx).apply { setText(R.string.log_export_name_style) })
        styles = RadioGroup(ctx)
        val labels = listOf(R.string.log_export_initials, R.string.log_export_first_three)
        labels.forEachIndexed { index, label ->
            styles.addView(
                MaterialRadioButton(ctx).apply {
                    id = View.generateViewId()
                    tag = index
                    setText(label)
                    isChecked = index == style.ordinal
                }
            )
        }
        styles.setOnCheckedChangeListener { group, id ->
            val index =
                group.findViewById<View>(id)?.tag as? Int ?: return@setOnCheckedChangeListener
            style = LogExportNames.Style.entries[index]
            updatePreview()
        }
        content.addView(styles)
        preview =
            TextView(ctx).apply {
                setPadding(0, padding / 2, 0, 0)
                setTextIsSelectable(true)
            }
        content.addView(preview)
        return MaterialAlertDialogBuilder(ctx)
            .setTitle(R.string.log_export_title)
            .setView(ScrollView(ctx).apply { addView(content) })
            .setPositiveButton(R.string.log_export_download, null)
            .setNeutralButton(R.string.log_export_share, null)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
    }

    override fun onStart() {
        super.onStart()
        val alert = dialog as AlertDialog
        alert.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener { export(true) }
        alert.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener { export(false) }
        if (copying) {
            setBusy(true)
        } else if (choices.isEmpty()) {
            loadChoices()
        } else {
            setBusy(pendingSnapshot != null)
        }
    }

    private fun loadChoices() {
        if (loading) return
        loading = true
        val app = requireContext().applicationContext
        lifecycleScope.launch {
            setBusy(true)
            try {
                choices = withContext(Dispatchers.IO) { LogExporter.choices(app) }
                sessions.removeAllViews()
                selected = selected.coerceIn(0, (choices.size - 1).coerceAtLeast(0))
                choices.forEachIndexed { index, choice ->
                    val info = choice.metadata
                    val date =
                        info.date?.format(
                            DateTimeFormatter.ofPattern("M/d/yyyy HH:mm xxx", Locale.getDefault())
                        )
                            ?: getString(R.string.log_export_unknown_date)
                    val games = info.games.joinToString("; ").ifEmpty {
                        getString(R.string.log_export_no_games)
                    }
                    val kind =
                        getString(
                            if (choice.current) {
                                R.string.log_export_current
                            } else {
                                R.string.log_export_previous
                            }
                        )
                    val label = listOf(
                        "$kind — $date",
                        games.take(1200),
                        info.build.orEmpty().take(160)
                    ).filter { it.isNotEmpty() }.joinToString("\n")
                    sessions.addView(
                        MaterialRadioButton(requireContext()).apply {
                            id = View.generateViewId()
                            tag = index
                            text = label
                            isChecked = index == selected
                        }
                    )
                }
                sessions.setOnCheckedChangeListener { group, id ->
                    selected = group.findViewById<View>(id)?.tag as? Int ?: 0
                    updatePreview()
                }
                if (choices.isEmpty()) {
                    preview.setText(R.string.share_log_not_found)
                } else {
                    updatePreview()
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                preview.setText(R.string.log_export_failed)
            } finally {
                loading = false
                setBusy(pendingSnapshot != null || copying)
            }
        }
    }

    private fun updatePreview() {
        choices.getOrNull(selected)?.let {
            preview.text = LogExportNames.filename(it.metadata, style)
        }
    }

    // AstraEH: Disable repeated exports while staging/copying. Share grants temporary read
    // access only to the chosen snapshot, and both actions preserve the original log bytes.
    private fun export(download: Boolean) {
        if (busy) return
        val choice = choices.getOrNull(selected) ?: return
        val chosenStyle = style
        val app = requireContext().applicationContext
        lifecycleScope.launch {
            setBusy(true)
            try {
                val snapshot =
                    withContext(Dispatchers.IO) { LogExporter.snapshot(app, choice, chosenStyle) }
                if (download) {
                    pendingSnapshot =
                        snapshot.relativeTo(File(app.cacheDir, LogExporter.DIRECTORY)).path
                    saveDocument.launch(snapshot.name)
                } else {
                    val uri = FileProvider.getUriForFile(
                        app,
                        app.packageName + ".logexports",
                        snapshot
                    )
                    val intent = Intent(Intent.ACTION_SEND).apply {
                        type = "text/plain"
                        putExtra(Intent.EXTRA_STREAM, uri)
                        clipData = ClipData.newRawUri(snapshot.name, uri)
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    }
                    startActivity(
                        Intent.createChooser(intent, getString(R.string.log_export_share))
                    )
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                pendingSnapshot = null
                showError()
            } finally {
                setBusy(pendingSnapshot != null)
            }
        }
    }

    private fun showError() {
        context?.let { Toast.makeText(it, R.string.log_export_failed, Toast.LENGTH_LONG).show() }
    }

    private fun setBusy(value: Boolean) {
        busy = value
        if (!::progress.isInitialized) return
        progress.visibility = if (value) View.VISIBLE else View.GONE
        listOf(sessions, styles).forEach { group ->
            for (i in 0 until group.childCount) group.getChildAt(i).isEnabled = !value
        }
        (dialog as? AlertDialog)?.let {
            it.getButton(AlertDialog.BUTTON_POSITIVE)?.isEnabled = !value && choices.isNotEmpty()
            it.getButton(AlertDialog.BUTTON_NEUTRAL)?.isEnabled = !value && choices.isNotEmpty()
        }
    }
}

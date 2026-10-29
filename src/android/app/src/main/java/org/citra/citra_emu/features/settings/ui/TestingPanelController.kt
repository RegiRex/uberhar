// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.ui

import android.view.View
import android.text.InputType
import android.widget.EditText
import android.widget.Toast
import androidx.core.view.isVisible
import androidx.lifecycle.LifecycleOwner
import com.google.android.material.chip.Chip
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.databinding.PanelTestingBinding
import org.citra.citra_emu.features.settings.model.TestingSettings
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.GameDriverSettings

// CodexAstraLocal: Home and Settings share one editing path, including title inheritance,
// durable-save recovery and resolution-only reload while a renderer is alive.
class TestingPanelController(
    private val binding: PanelTestingBinding,
    private val lifecycleOwner: LifecycleOwner,
    titleId: String,
    private val onChanged: () -> Unit = {}
) {
    private val context get() = binding.root.context
    private val gameId = SettingsFile.normalizeGameId(titleId) ?: ""
    private var rendering = false
    private val presets = listOf(
        0 to R.string.uberhar_testing_native,
        1 to R.string.uberhar_testing_combo_cpu,
        2 to R.string.uberhar_testing_combo_gpu
    )
    private val chips = mutableMapOf<Int, Chip>()

    init {
        presets.forEach { (preset, label) ->
            val chip = Chip(context).apply {
                id = View.generateViewId()
                setText(label)
                isCheckable = true
                setOnClickListener { change("preset") { TestingSettings.preset = preset } }
            }
            chips[preset] = chip
            binding.testingPresets.addView(chip)
        }
        binding.testingEnabled.setOnCheckedChangeListener { _, enabled ->
            if (!rendering) change("enabled") { TestingSettings.enabled = enabled }
        }
        binding.testingResolution.addOnButtonCheckedListener { _, checkedId, checked ->
            if (checked && !rendering) change("resolution", liveResolution = true) {
                TestingSettings.resolution = if (checkedId == R.id.testing_resolution_4x) 4 else 2
            }
        }
        // CodexAstraLocal: Both global Options and title overrides share the preset's restart boundary.
        binding.testingDisableRightEyeRender.setOnCheckedChangeListener { _, checked ->
            if (!rendering) change("disable_right_eye_render") {
                TestingSettings.disableRightEyeRender = checked
            }
        }
        // CodexAstraLocal: Hardware requests work in either scope while preserving CPU-only admission.
        binding.testingHardwareShaders.setOnCheckedChangeListener { _, checked ->
            if (!rendering && !TestingSettings.hardwareShadersConflictsWithCpuVertices()) change("use_hw_shader") {
                TestingSettings.hardwareShaders = checked
            }
        }
        binding.testingRenderThreadDelay.setOnClickListener {
            if (TestingSettings.available && TestingSettings.enabled && !EmulationActivity.isRunning()) {
                val input = EditText(context).apply {
                    inputType = InputType.TYPE_CLASS_NUMBER
                    setText(TestingSettings.renderThreadDelayUs.toString())
                    selectAll()
                    hint = context.getString(R.string.uberhar_testing_delay_range)
                }
                val dialog = MaterialAlertDialogBuilder(context)
                    .setTitle(R.string.delay_render_thread)
                    .setMessage(R.string.uberhar_testing_delay_description)
                    .setView(input)
                    .setPositiveButton(android.R.string.ok, null)
                    .setNegativeButton(android.R.string.cancel, null).create()
                dialog.setOnShowListener {
                    dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                        val value = input.text.toString().toIntOrNull()
                        if (value == null || value !in 0..16000) {
                            input.error = context.getString(R.string.uberhar_testing_delay_range)
                        } else {
                            change("delay_game_render_thread_us") { TestingSettings.renderThreadDelayUs = value }
                            dialog.dismiss()
                        }
                    }
                }
                dialog.show()
            }
        }
        // CodexAstraLocal: Presentation filtering and guest slider input are independent launch controls.
        binding.testingLinearFiltering.setOnCheckedChangeListener { _, checked ->
            if (!rendering) change("filter_mode") {
                TestingSettings.linearFiltering = checked
            }
        }
        binding.testingStereoDepth.addOnButtonCheckedListener { _, checkedId, checked ->
            if (checked && !rendering) change("factor_3d") {
                TestingSettings.stereoDepth = if (checkedId == R.id.testing_stereo_depth_1) 1 else 0
            }
        }
        binding.testingUseGlobal.setOnClickListener {
            MaterialAlertDialogBuilder(context)
                .setMessage(R.string.uberhar_testing_reset_confirmation)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    if (!EmulationActivity.isRunning()) {
                        TestingSettings.load(gameId)
                        if (!TestingSettings.useGlobalChoices()) showSaveFailure()
                        render()
                        onChanged()
                    }
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }
        binding.gameDriver.setOnClickListener {
            if (!EmulationActivity.isRunning()) {
                GameDriverSettings.showPicker(context, lifecycleOwner, gameId) { render() }
            }
        }
        render()
    }

    // CodexAstraLocal: Save only the edited key so other title choices continue inheriting defaults.
    private fun change(key: String, liveResolution: Boolean = false, update: () -> Unit) {
        if (rendering || !TestingSettings.available) return
        val running = EmulationActivity.isRunning()
        if (running && !liveResolution) return
        TestingSettings.load(gameId)
        update()
        if (!TestingSettings.save(key)) {
            showSaveFailure()
        } else if (running && liveResolution && editsRunningTitle()) {
            // CodexAstraLocal: The native overlay freezes model/driver choices and reloads only resolution.
            NativeLibrary.reloadSettings()
            NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())
        }
        render()
        onChanged()
    }

    private fun showSaveFailure() =
        Toast.makeText(context, R.string.uberhar_testing_save_failed, Toast.LENGTH_LONG).show()

    // CodexAstraLocal: Global defaults and other titles must never reload the active title's renderer.
    private fun editsRunningTitle(): Boolean = gameId.isNotEmpty() && NativeLibrary.isRunning() &&
        gameId == SettingsFile.normalizeGameId("%016X".format(NativeLibrary.getRunningTitleId()))

    fun render() {
        rendering = true
        TestingSettings.load(gameId)
        val running = EmulationActivity.isRunning()
        // CodexAstraLocal: Hide alpha controls, not the ordinary per-title driver selector.
        val available = TestingSettings.available
        // CodexAstraLocal: Global Options is a working editor; only launch ownership and the
        // CPU-only preset restrict active controls, independently of whether a title is selected.
        val controlsEnabled = available && TestingSettings.enabled
        val launchControlsEnabled = controlsEnabled && !running
        binding.testingEnabled.isVisible = available
        binding.testingDescription.isVisible = available
        binding.testingEnabled.isChecked = TestingSettings.enabled
        binding.testingEnabled.isEnabled = available && !running
        binding.testingControls.isVisible = available && TestingSettings.enabled
        val description = context.getString(
            if (running) R.string.uberhar_testing_live_description else R.string.uberhar_testing_description
        )
        // CodexAstraLocal: Ordinary title values outside the quick choices stay visible and unchanged.
        binding.testingDescription.text = if (TestingSettings.resolution !in listOf(2, 4) ||
            TestingSettings.effectiveStereoDepth() !in 0..1) {
            description + "\n" + context.getString(R.string.uberhar_testing_other_values,
                TestingSettings.resolution, TestingSettings.effectiveStereoDepth())
        } else description
        chips.forEach { (preset, chip) ->
            chip.isChecked = TestingSettings.preset == preset
            chip.isEnabled = launchControlsEnabled
        }
        when (TestingSettings.resolution) {
            2 -> binding.testingResolution.check(R.id.testing_resolution_2x)
            4 -> binding.testingResolution.check(R.id.testing_resolution_4x)
            else -> binding.testingResolution.clearChecked()
        }
        binding.testingResolution2x.isEnabled = controlsEnabled
        binding.testingResolution4x.isEnabled = controlsEnabled
        // CodexAstraLocal: Global requests are editable before launch; running state stays native-latched.
        binding.testingDisableRightEyeRender.isChecked = TestingSettings.requestedRightEyeRenderDisabled()
        binding.testingDisableRightEyeRender.isEnabled = launchControlsEnabled
        binding.testingRightEyeDescription.setText(R.string.uberhar_testing_right_eye_description)
        binding.testingHardwareShaders.isChecked = TestingSettings.effectiveHardwareShaders()
        binding.testingHardwareShaders.isEnabled = launchControlsEnabled &&
            !TestingSettings.hardwareShadersConflictsWithCpuVertices()
        binding.testingHardwareDescription.setText(
            when {
                TestingSettings.hardwareShadersConflictsWithCpuVertices() -> R.string.uberhar_testing_hardware_cpu_only
                else -> R.string.uberhar_testing_hardware_description
            }
        )
        binding.testingRenderThreadDelay.text = context.getString(
            R.string.uberhar_testing_delay_value, TestingSettings.effectiveRenderThreadDelayUs()
        )
        binding.testingRenderThreadDelay.isEnabled = launchControlsEnabled
        // CodexAstraLocal: Resolution alone can change live; display the native launch values here.
        binding.testingLinearFiltering.isChecked = TestingSettings.effectiveLinearFiltering()
        binding.testingLinearFiltering.isEnabled = launchControlsEnabled
        when (TestingSettings.effectiveStereoDepth()) {
            0 -> binding.testingStereoDepth.check(R.id.testing_stereo_depth_0)
            1 -> binding.testingStereoDepth.check(R.id.testing_stereo_depth_1)
            else -> binding.testingStereoDepth.clearChecked()
        }
        binding.testingStereoDepth0.isEnabled = launchControlsEnabled
        binding.testingStereoDepth1.isEnabled = launchControlsEnabled
        binding.testingUseGlobal.isVisible = available && gameId.isNotEmpty()
        binding.testingUseGlobal.isEnabled = !running
        binding.gameDriver.isVisible = gameId.isNotEmpty()
        binding.gameDriver.isEnabled = !running
        binding.gameDriver.text = context.getString(
            R.string.uberhar_game_driver_selected, GameDriverSettings.selectionLabel(context, gameId)
        )
        rendering = false
    }
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.utils

import android.content.Context
import android.widget.Toast
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.model.DriverSettings

// CodexAstraLocal: Choose an already imported driver per title; library changes occur only at next launch.
object GameDriverSettings {
    fun selectionLabel(context: Context, titleId: String): String =
        DriverSettings.load(titleId)?.label ?: context.getString(R.string.uberhar_game_driver_inherit)

    fun showPicker(context: Context, owner: LifecycleOwner, titleId: String, changed: () -> Unit) {
        owner.lifecycleScope.launch {
            val choices = withContext(Dispatchers.IO) {
                runCatching { GpuDriverHelper.getDrivers() }.getOrDefault(mutableListOf())
            }
            if (EmulationActivity.isRunning()) return@launch
            val globalLabel = context.getString(R.string.uberhar_game_driver_inherit)
            val systemLabel = context.getString(R.string.system_gpu_driver)
            val labels = listOf(globalLabel) + choices.map { it.second.name ?: systemLabel }
            val saved = DriverSettings.load(titleId)?.driver
            val selected = if (saved == null) 0 else choices.indexOfFirst {
                (if (it.first.toString().isEmpty()) DriverSettings.SYSTEM else it.first.toString()) == saved
            }.let { if (it < 0) -1 else it + 1 }
            MaterialAlertDialogBuilder(context)
                .setTitle(R.string.uberhar_game_driver)
                .setSingleChoiceItems(labels.toTypedArray(), selected) { dialog, index ->
                    if (!EmulationActivity.isRunning()) {
                        val selection = if (index == 0) null else choices[index - 1].let {
                            DriverSettings.Selection(
                                if (it.first.toString().isEmpty()) DriverSettings.SYSTEM else it.first.toString(),
                                it.second.name ?: systemLabel
                            )
                        }
                        if (!DriverSettings.save(titleId, selection)) {
                            Toast.makeText(context, R.string.uberhar_testing_save_failed, Toast.LENGTH_LONG).show()
                        }
                        changed()
                    }
                    dialog.dismiss()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }
    }
}

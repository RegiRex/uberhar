// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.Intent
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.BuildConfig
import org.citra.citra_emu.R
import org.citra.citra_emu.fragments.CitraDirectoryDialogFragment
import org.citra.citra_emu.fragments.CopyDirProgressDialogFragment
import org.citra.citra_emu.model.SetupCallback
import org.citra.citra_emu.viewmodel.HomeViewModel

/**
 * Citra directory initialization ui flow controller.
 */
class CitraDirectoryHelper(
    private val fragmentActivity: FragmentActivity,
    private val lostPermission: Boolean
) {
    fun showCitraDirectoryDialog(
        result: Uri,
        callback: SetupCallback? = null,
        buttonState: () -> Unit
    ) {
        val citraDirectoryDialog = CitraDirectoryDialogFragment.newInstance(
            fragmentActivity,
            result.toString(),
            CitraDirectoryDialogFragment.Listener { moveData: Boolean, path: Uri ->
                val previous = PermissionsHandler.citraDirectory
                // Do noting if user select the previous path.
                if (path == previous && !lostPermission) {
                    return@Listener
                }

                val takeFlags = Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                    Intent.FLAG_GRANT_READ_URI_PERMISSION
                fragmentActivity.contentResolver.takePersistableUriPermission(
                    path,
                    takeFlags
                )
                // AstraEH: Isolate experimental saves/config/cache from a populated Azahar folder.
                // An existing Uberhar marker permits reusing this app's own directory.
                if (BuildConfig.FLAVOR == "uberhar" && path != previous) {
                    val directory = DocumentFile.fromTreeUri(fragmentActivity, path)
                    val marker = directory?.findFile("uberhar-data.txt")
                    val isEmpty = directory?.listFiles()?.isEmpty() == true
                    if (directory == null || (marker == null && !isEmpty) ||
                        (marker == null && directory.createFile("text/plain", "uberhar-data.txt") == null)
                    ) {
                        MaterialAlertDialogBuilder(fragmentActivity)
                            .setTitle(R.string.uberhar_separate_folder_title)
                            .setMessage(R.string.uberhar_separate_folder_description)
                            .setPositiveButton(android.R.string.ok, null)
                            .show()
                        ViewModelProvider(fragmentActivity)[HomeViewModel::class.java]
                            .setPickingUserDir(false)
                        buttonState()
                        return@Listener
                    }
                }
                if (!moveData || previous.toString().isEmpty()) {
                    initializeCitraDirectory(path)
                    buttonState()
                    val viewModel = ViewModelProvider(fragmentActivity)[HomeViewModel::class.java]
                    viewModel.setUserDir(fragmentActivity, path.path!!)
                    viewModel.setPickingUserDir(false)
                    return@Listener
                }

                // If user check move data, show copy progress dialog.
                CopyDirProgressDialogFragment.newInstance(
                    fragmentActivity,
                    previous,
                    path,
                    callback
                )
                    ?.show(
                        fragmentActivity.supportFragmentManager,
                        CopyDirProgressDialogFragment.TAG
                    )
            }
        )
        citraDirectoryDialog.show(
            fragmentActivity.supportFragmentManager,
            CitraDirectoryDialogFragment.TAG
        )
    }

    companion object {
        fun initializeCitraDirectory(path: Uri) {
            PermissionsHandler.setCitraDirectory(path.toString())
            DirectoryInitialization.resetCitraDirectoryState()
            DirectoryInitialization.start()
        }
    }
}

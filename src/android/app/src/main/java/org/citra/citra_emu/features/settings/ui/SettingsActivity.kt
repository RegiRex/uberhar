// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.view.ViewGroup.MarginLayoutParams
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.preference.PreferenceManager
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.IOException
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.databinding.ActivitySettingsBinding
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.FloatSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.ScaledFloatSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.model.SettingsViewModel
import org.citra.citra_emu.features.settings.model.StringSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.InsetsHelper
import org.citra.citra_emu.utils.RefreshRateUtil
import org.citra.citra_emu.utils.SystemSaveGame
import org.citra.citra_emu.utils.ThemeUtil

class SettingsActivity :
    AppCompatActivity(),
    SettingsActivityView {
    private val presenter = SettingsActivityPresenter(this)

    private lateinit var binding: ActivitySettingsBinding

    private val settingsViewModel: SettingsViewModel by viewModels()

    override val settings: Settings get() = settingsViewModel.settings

    override fun onCreate(savedInstanceState: Bundle?) {
        RefreshRateUtil.enforceRefreshRate(this)

        ThemeUtil.setTheme(this)

        super.onCreate(savedInstanceState)

        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        WindowCompat.setDecorFitsSystemWindows(window, false)

        val launcher = intent
        val gameID = launcher.getStringExtra(ARG_GAME_ID)
        val menuTag = launcher.getStringExtra(ARG_MENU_TAG)
        presenter.onCreate(savedInstanceState, menuTag!!, gameID!!)

        // Show "Back" button in the action bar for navigation
        setSupportActionBar(binding.toolbarSettings)
        supportActionBar!!.setDisplayHomeAsUpEnabled(true)

        if (InsetsHelper.getSystemGestureType(applicationContext) !=
            InsetsHelper.GESTURE_NAVIGATION
        ) {
            binding.navigationBarShade.setBackgroundColor(
                ThemeUtil.getColorWithOpacity(
                    MaterialColors.getColor(
                        binding.navigationBarShade,
                        com.google.android.material.R.attr.colorSurface
                    ),
                    ThemeUtil.SYSTEM_BAR_ALPHA
                )
            )
        }

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() = navigateBack()
            }
        )

        setInsets()
    }

    override fun onSupportNavigateUp(): Boolean {
        navigateBack()
        return true
    }

    private fun navigateBack() {
        if (supportFragmentManager.backStackEntryCount > 0) {
            supportFragmentManager.popBackStack()
        } else {
            finish()
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        // Critical: If super method is not called, rotations will be busted.
        super.onSaveInstanceState(outState)
        presenter.saveState(outState)
    }

    override fun onPause() {
        super.onPause()
        presenter.onPause()
    }

    override fun onResume() {
        super.onResume()
        presenter.onResume()
    }

    override fun onStart() {
        super.onStart()
        presenter.onStart()
    }

    /**
     * If this is called, the user has left the settings screen (potentially through the
     * home button) and will expect their changes to be persisted. So we kick off an
     * IntentService which will do so on a background thread.
     */
    override fun onStop() {
        super.onStop()
        presenter.onStop(isFinishing)
    }

    override fun showSettingsFragment(menuTag: String, addToStack: Boolean, gameId: String) {
        if (!addToStack && settingsFragment != null) {
            return
        }

        val transaction = supportFragmentManager.beginTransaction()
        if (addToStack) {
            if (areSystemAnimationsEnabled()) {
                transaction.setCustomAnimations(
                    R.anim.anim_settings_fragment_in,
                    R.anim.anim_settings_fragment_out,
                    0,
                    R.anim.anim_pop_settings_fragment_out
                )
            }
            transaction.addToBackStack(null)
        }
        transaction.replace(
            R.id.frame_content,
            SettingsFragment.newInstance(menuTag, gameId),
            FRAGMENT_TAG
        )
        transaction.commit()
    }

    private fun areSystemAnimationsEnabled(): Boolean {
        val duration = android.provider.Settings.Global.getFloat(
            contentResolver,
            android.provider.Settings.Global.ANIMATOR_DURATION_SCALE,
            1f
        )
        val transition = android.provider.Settings.Global.getFloat(
            contentResolver,
            android.provider.Settings.Global.TRANSITION_ANIMATION_SCALE,
            1f
        )
        return duration != 0f && transition != 0f
    }

    override fun onSettingsFileLoaded() {
        val fragment: SettingsFragmentView? = settingsFragment
        fragment?.loadSettingsList()
    }

    override fun onSettingsFileNotFound() {
        val fragment: SettingsFragmentView? = settingsFragment
        fragment?.loadSettingsList()
    }

    override fun showToastMessage(message: String, isLong: Boolean) {
        Toast.makeText(
            this,
            message,
            if (isLong) Toast.LENGTH_LONG else Toast.LENGTH_SHORT
        ).show()
    }

    override fun onSettingChanged() {
        presenter.onSettingChanged()
    }

    fun onSettingsReset() {
        // CodexAstraLocal: Even a restored legacy reset dialog cannot delete global
        // configuration, controllers or console data from a title-scoped activity.
        if (settings.isPerGame) {
            resetPerGameSettings()
            return
        }
        // Prevents saving to a non-existent settings file
        presenter.onSettingsReset()

        val controllerKeys = Settings.buttonKeys + Settings.circlePadKeys + Settings.cStickKeys +
            Settings.dPadAxisKeys + Settings.dPadButtonKeys + Settings.triggerKeys
        val editor =
            PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext).edit()
        controllerKeys.forEach { editor.remove(it) }
        editor.apply()

        // Reset the static memory representation of each setting
        BooleanSetting.clear()
        FloatSetting.clear()
        ScaledFloatSetting.clear()
        IntSetting.clear()
        StringSetting.clear()

        // Delete settings file because the user may have changed values that do not exist in the UI
        val settingsFile = SettingsFile.getSettingsFile(SettingsFile.FILE_NAME_CONFIG)
        if (!settingsFile.delete()) {
            throw IOException("Failed to delete $settingsFile")
        }

        // Set the root of the document tree before we create a new config file or the native code
        // will fail when creating the file.
        if (DirectoryInitialization.setCitraUserDirectory()) {
            CitraApplication.documentsTree.setRoot(Uri.parse(DirectoryInitialization.userPath))
            NativeLibrary.createConfigFile()
        } else {
            throw IllegalStateException("Azahar directory unavailable when accessing config file!")
        }

        // Set default values for system config file
        SystemSaveGame.apply {
            setUsername("AZAHAR")
            setBirthday(11, 7)
            setSystemLanguage(1)
            setSoundOutputMode(1)
            setCountryCode(49)
            setPlayCoins(42)
        }

        showToastMessage(getString(R.string.settings_reset), true)
        finish()
    }

    fun setToolbarTitle(title: String) {
        // CodexAstraLocal: Preserve title scope on every submenu, not just the entry page.
        binding.toolbarSettingsLayout.title = if (settings.isPerGame) {
            getString(R.string.uberhar_game_settings_title, title, settings.gameId)
        } else {
            title
        }
    }

    // CodexAstraLocal: A destructive-looking whole-title action always asks before
    // changing the pending overrides; cancellation leaves both files and values intact.
    fun confirmResetPerGameSettings() {
        if (!settings.isPerGame) return
        if (!canResetPerGameSettings()) return
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.uberhar_game_reset)
            .setMessage(R.string.uberhar_game_reset_confirm)
            .setPositiveButton(android.R.string.ok) { _, _ -> resetPerGameSettings() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun resetPerGameSettings() {
        if (!settings.isPerGame || !canResetPerGameSettings()) return
        settings.resetPerGameOverrides()
        presenter.onSettingChanged()
        settingsFragment?.loadSettingsList()
    }

    // CodexAstraLocal: Recheck at confirmation time as well as row binding, because
    // a restored dialog must not reset frozen renderer options in a running game.
    private fun canResetPerGameSettings(): Boolean {
        if (!EmulationActivity.isRunning()) return true
        showToastMessage(getString(R.string.uberhar_game_reset_restart_required), true)
        return false
    }

    private val settingsFragment: SettingsFragment?
        get() = supportFragmentManager.findFragmentByTag(FRAGMENT_TAG) as SettingsFragment?

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.frameContent
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            view.updatePadding(
                left = barInsets.left + cutoutInsets.left,
                right = barInsets.right + cutoutInsets.right
            )

            val mlpAppBar = binding.appbarSettings.layoutParams as MarginLayoutParams
            mlpAppBar.leftMargin = barInsets.left + cutoutInsets.left
            mlpAppBar.rightMargin = barInsets.right + cutoutInsets.right
            binding.appbarSettings.layoutParams = mlpAppBar

            val mlpShade = binding.navigationBarShade.layoutParams as MarginLayoutParams
            mlpShade.height = barInsets.bottom
            binding.navigationBarShade.layoutParams = mlpShade

            windowInsets
        }
    }

    companion object {
        private const val ARG_MENU_TAG = "menu_tag"
        private const val ARG_GAME_ID = "game_id"
        private const val FRAGMENT_TAG = "settings"

        @JvmStatic
        fun launch(context: Context, menuTag: String?, gameId: String?) {
            val settings = Intent(context, SettingsActivity::class.java)
            settings.putExtra(ARG_MENU_TAG, menuTag)
            settings.putExtra(ARG_GAME_ID, gameId)
            context.startActivity(settings)
        }

        fun launch(
            context: Context,
            launcher: ActivityResultLauncher<Intent>,
            menuTag: String?,
            gameId: String?
        ) {
            val settings = Intent(context, SettingsActivity::class.java)
            settings.putExtra(ARG_MENU_TAG, menuTag)
            settings.putExtra(ARG_GAME_ID, gameId)
            launcher.launch(settings)
        }
    }
}

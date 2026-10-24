// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.LinearLayoutManager
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.databinding.FragmentSettingsBinding
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.TestingSettings
import org.citra.citra_emu.features.settings.model.view.SettingsItem
import org.citra.citra_emu.features.settings.utils.SettingsFile

class SettingsFragment :
    Fragment(),
    SettingsFragmentView {
    override var activityView: SettingsActivityView? = null

    private val fragmentPresenter = SettingsFragmentPresenter(this)
    private var settingsAdapter: SettingsAdapter? = null

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    // CodexAstraLocal: Keep temporary test selections separate from the edited title/global enums.
    private var testingPanelController: TestingPanelController? = null

    override fun onAttach(context: Context) {
        super.onAttach(context)
        activityView = requireActivity() as SettingsActivityView
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val menuTag = requireArguments().getString(ARGUMENT_MENU_TAG)
        val gameId = requireArguments().getString(ARGUMENT_GAME_ID)
        fragmentPresenter.onCreate(menuTag!!, gameId!!)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSettingsBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        settingsAdapter = SettingsAdapter(this, requireActivity())
        binding.listSettings.apply {
            adapter = settingsAdapter
            layoutManager = LinearLayoutManager(activity)
        }
        setupTestingPanel()
        fragmentPresenter.onViewCreated(settingsAdapter!!)

        setInsets()
    }

    override fun onDetach() {
        super.onDetach()
        activityView = null
        if (settingsAdapter != null) {
            settingsAdapter!!.closeDialog()
        }
    }

    override fun showSettingsList(settingsList: ArrayList<SettingsItem>) {
        settingsAdapter!!.setSettingsList(settingsList)
        renderTestingPanel()
    }

    // CodexAstraLocal: Use the same title-aware panel on home Options and in-game Settings.
    private fun setupTestingPanel() {
        val isRoot = requireArguments().getString(ARGUMENT_MENU_TAG) == SettingsFile.FILE_NAME_CONFIG
        binding.testingPanel.root.isVisible = isRoot
        TestingSettings.load(requireArguments().getString(ARGUMENT_GAME_ID) ?: "")
        if (!isRoot) return
        testingPanelController = TestingPanelController(
            binding.testingPanel, viewLifecycleOwner,
            requireArguments().getString(ARGUMENT_GAME_ID) ?: ""
        ) {
            renderTestingPanel()
            fragmentPresenter.loadSettingsList()
        }
    }

    private fun renderTestingPanel() {
        testingPanelController?.render()
        val scope = getString(
            if (activityView!!.settings.isPerGame) R.string.uberhar_game_settings_scope
            else R.string.uberhar_global_settings_scope
        )
        binding.settingsScope.text = if (TestingSettings.enabled) {
            val presetName = getString(TESTING_PRESETS.first { it.first == TestingSettings.preset }.second)
            scope + "\n" + getString(R.string.uberhar_testing_active_summary, presetName, TestingSettings.resolution)
        } else scope
        binding.settingsScope.isVisible = true
    }

    override fun loadSettingsList() {
        fragmentPresenter.loadSettingsList()
    }

    override fun loadSubMenu(menuKey: String) {
        activityView!!.showSettingsFragment(
            menuKey,
            true,
            requireArguments().getString(ARGUMENT_GAME_ID)!!
        )
    }

    override fun showToastMessage(message: String?, is_long: Boolean) {
        activityView!!.showToastMessage(message!!, is_long)
    }

    override fun putSetting(setting: AbstractSetting) {
        fragmentPresenter.putSetting(setting)
    }

    override fun onSettingChanged() {
        activityView!!.onSettingChanged()
    }

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            binding.listSettings.updatePadding(
                left = barInsets.left + cutoutInsets.left,
                right = barInsets.right + cutoutInsets.right,
                bottom = barInsets.bottom
            )
            windowInsets
        }
    }

    companion object {
        private const val ARGUMENT_MENU_TAG = "menu_tag"
        private const val ARGUMENT_GAME_ID = "game_id"

        // CodexAstraLocal: These IDs match TestingSettings/native overlay serialization.
        private val TESTING_PRESETS = listOf(
            0 to R.string.uberhar_testing_native,
            1 to R.string.uberhar_testing_combo_cpu,
            2 to R.string.uberhar_testing_combo_gpu
        )

        fun newInstance(menuTag: String?, gameId: String?): Fragment {
            val fragment = SettingsFragment()
            val arguments = Bundle()
            arguments.putString(ARGUMENT_MENU_TAG, menuTag)
            arguments.putString(ARGUMENT_GAME_ID, gameId)
            fragment.arguments = arguments
            return fragment
        }
    }
}

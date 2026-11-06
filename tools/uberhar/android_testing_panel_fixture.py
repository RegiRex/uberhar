"""CodexAstraLocal: Host UI boundaries for the real Testing panel interaction fixture.

These substitutes expose listeners and enabled/visible state. They do not qualify
Android widget rendering, RecyclerView layout, native execution, or gameplay.
"""


def panel_stubs():
    return {
        "AndroidView.kt": '''package android.view
import android.content.Context
open class View(val context: Context = Context()) {
    var id = 0
    var isEnabled = true
    var visible = true
    private var click: ((View) -> Unit)? = null
    fun setOnClickListener(listener: (View) -> Unit) { click = listener }
    fun performClick() { if (isEnabled && visible) click?.invoke(this) }
    companion object {
        private var nextId = 10000
        fun generateViewId() = nextId++
    }
}
''',
        "Visibility.kt": '''package androidx.core.view
var android.view.View.isVisible: Boolean
    get() = visible
    set(value) { visible = value }
''',
        "Lifecycle.kt": '''package androidx.lifecycle
interface LifecycleOwner
''',
        "Widgets.kt": '''package android.widget
import android.content.Context
import android.view.View
// CodexAstraLocal: Listener callbacks include render-time changes to catch persistence recursion.
open class PanelWidget(context: Context = Context()) : View(context) {
    var text: CharSequence = ""
    var isCheckable = false
    var isChecked = false
        set(value) {
            if (field != value) { field = value; checked?.invoke(this, value) }
        }
    val children = mutableListOf<View>()
    var checkedId = -1
    private var checked: ((PanelWidget, Boolean) -> Unit)? = null
    private var selected: ((PanelWidget, Int, Boolean) -> Unit)? = null
    fun setText(value: Int) { text = context.getString(value) }
    fun setText(value: String) { text = value }
    fun setOnCheckedChangeListener(listener: (PanelWidget, Boolean) -> Unit) { checked = listener }
    fun addOnButtonCheckedListener(listener: (PanelWidget, Int, Boolean) -> Unit) { selected = listener }
    fun addView(view: View) { children.add(view) }
    fun check(id: Int) {
        if (checkedId == id) return
        val previous = checkedId
        checkedId = id
        if (previous != -1) selected?.invoke(this, previous, false)
        selected?.invoke(this, id, true)
    }
    fun clearChecked() { check(-1) }
    fun userCheck(value: Boolean) { if (isEnabled && visible) isChecked = value }
}
class TextView(context: Context) : PanelWidget(context)
class EditText(context: Context) : PanelWidget(context) {
    var inputType = 0
    var hint = ""
    var error: String? = null
    fun selectAll() {}
}
class Toast {
    fun show() {}
    companion object {
        const val LENGTH_LONG = 1
        fun makeText(context: Context, message: Int, length: Int) = Toast()
    }
}
''',
        "InputType.kt": '''package android.text
object InputType { const val TYPE_CLASS_NUMBER = 2 }
''',
        "MaterialSwitch.kt": '''package com.google.android.material.materialswitch
class MaterialSwitch(context: android.content.Context) : android.widget.PanelWidget(context)
''',
        "Chip.kt": '''package com.google.android.material.chip
class Chip(context: android.content.Context) : android.widget.PanelWidget(context)
''',
        "AndroidDialog.kt": '''package android.app
import android.widget.EditText
import android.widget.PanelWidget
class AlertDialog(val input: EditText?, positive: ((Any?, Int) -> Unit)?) {
    var dismissed = false
    private var onShow: ((AlertDialog) -> Unit)? = null
    private val positiveButton = PanelWidget().apply {
        setOnClickListener { positive?.invoke(this@AlertDialog, BUTTON_POSITIVE); dismiss() }
    }
    private val negativeButton = PanelWidget().apply { setOnClickListener { dismiss() } }
    fun setOnShowListener(listener: (AlertDialog) -> Unit) { onShow = listener }
    fun getButton(which: Int) = if (which == BUTTON_POSITIVE) positiveButton else negativeButton
    fun show() { lastShown = this; onShow?.invoke(this) }
    fun dismiss() { dismissed = true }
    companion object {
        const val BUTTON_POSITIVE = -1
        const val BUTTON_NEGATIVE = -2
        var lastShown: AlertDialog? = null
    }
}
''',
        "DialogBuilder.kt": '''package com.google.android.material.dialog
import android.app.AlertDialog
import android.content.Context
import android.widget.EditText
class MaterialAlertDialogBuilder(context: Context) {
    private var input: EditText? = null
    private var positive: ((Any?, Int) -> Unit)? = null
    fun setTitle(id: Int) = this
    fun setMessage(id: Int) = this
    fun setView(view: EditText) = apply { input = view }
    fun setPositiveButton(id: Int, listener: ((Any?, Int) -> Unit)?) = apply { positive = listener }
    fun setNegativeButton(id: Int, listener: ((Any?, Int) -> Unit)?) = this
    fun create() = AlertDialog(input, positive)
    fun show() = create().apply { show() }
}
''',
        "AndroidR.kt": '''package android
object R { object string { const val ok = 1; const val cancel = 2 } }
''',
        "EmulationActivity.kt": '''package org.citra.citra_emu.activities
object EmulationActivity {
    var running = false
    fun isRunning() = running
}
''',
        "GameDriverSettings.kt": '''package org.citra.citra_emu.utils
object GameDriverSettings {
    fun selectionLabel(context: android.content.Context, gameId: String) = "global"
    fun showPicker(context: android.content.Context, owner: androidx.lifecycle.LifecycleOwner,
                   gameId: String, changed: () -> Unit) {}
}
''',
        "PanelTestingBinding.kt": '''package org.citra.citra_emu.databinding
import android.widget.PanelWidget
import org.citra.citra_emu.R
class PanelTestingBinding {
    val root = PanelWidget()
    val testingPresets = PanelWidget()
    val testingEnabled = PanelWidget()
    val testingDescription = PanelWidget()
    val testingControls = PanelWidget()
    val testingResolution = PanelWidget()
    val testingResolution2x = PanelWidget().apply {
        setOnClickListener { testingResolution.check(R.id.testing_resolution_2x) }
    }
    val testingResolution4x = PanelWidget().apply {
        setOnClickListener { testingResolution.check(R.id.testing_resolution_4x) }
    }
    val testingDisableRightEyeRender = PanelWidget()
    val testingRightEyeDescription = PanelWidget()
    val testingExperiments = PanelWidget()
    val testingRenderThreadDelay = PanelWidget()
    val testingStereoDepth = PanelWidget()
    val testingStereoDepth0 = PanelWidget().apply {
        setOnClickListener { testingStereoDepth.check(R.id.testing_stereo_depth_0) }
    }
    val testingStereoDepth1 = PanelWidget().apply {
        setOnClickListener { testingStereoDepth.check(R.id.testing_stereo_depth_1) }
    }
    val testingUseGlobal = PanelWidget()
    val gameDriver = PanelWidget()
}
''',
        "TestingPanelCheck.kt": r'''import java.io.File
import android.app.AlertDialog
import org.ini4j.Wini
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.databinding.PanelTestingBinding
import org.citra.citra_emu.features.settings.model.TestingSettings
import org.citra.citra_emu.features.settings.ui.TestingPanelController
import org.citra.citra_emu.utils.DirectoryInitialization

// CodexAstraLocal: Drive all production experiment listeners, sparse saves and launch locks.
fun checkTestingPanel(folder: File) {
    val directory = File(folder, "panel-controls").apply { mkdirs() }
    DirectoryInitialization.userDirectory = directory.path
    val normal = File(directory, "config/config.ini").apply {
        parentFile.mkdirs(); writeText("[Renderer]\nuse_hw_shader=true\nfilter_mode=true\n")
    }
    val savedNormal = normal.readBytes()
    val testing = File(directory, "config/testing.ini")
    fun readTesting() = testing.inputStream().use(::Wini)
    val owner = object : androidx.lifecycle.LifecycleOwner {}
    var binding = PanelTestingBinding()
    var panel = TestingPanelController(binding, owner, "")
    fun toggles() = binding.testingExperiments.children.filterIsInstance<
        com.google.android.material.materialswitch.MaterialSwitch>()
    val choices = org.citra.citra_emu.features.settings.model.TestingExperiment.entries
    fun openPanel(title: String) {
        binding = PanelTestingBinding(); panel = TestingPanelController(binding, owner, title)
    }
    check(toggles().size == choices.size && toggles().none { it.isEnabled })
    binding.testingEnabled.userCheck(true)
    check(toggles().all { it.isEnabled })
    for ((choice,toggle) in choices.zip(toggles())) {
        toggle.userCheck(!toggle.isChecked)
        check(readTesting().get("Testing",choice.key) == (!choice.defaultValue).toString())
    }
    val globalBefore = readTesting()["Testing"]!!.toMap()
    openPanel("000400000F700100")
    for ((choice,toggle) in choices.zip(toggles())) {
        toggle.userCheck(false)
        if (!choice.defaultValue) check(readTesting().get("Testing.000400000F700100",choice.key) == "false")
    }
    check(readTesting()["Testing"]!!.toMap() == globalBefore)
    binding.testingUseGlobal.performClick()
    checkNotNull(AlertDialog.lastShown).getButton(AlertDialog.BUTTON_NEGATIVE).performClick()
    check(toggles().all { !it.isChecked })
    binding.testingUseGlobal.performClick()
    checkNotNull(AlertDialog.lastShown).getButton(AlertDialog.BUTTON_POSITIVE).performClick()
    check(choices.zip(toggles()).all { (choice,toggle) -> toggle.isChecked == !choice.defaultValue })
    NativeLibrary.running = true; EmulationActivity.running = true
    NativeLibrary.effectiveExperiments[choices.first().key] = true
    panel.render()
    check(toggles().none { it.isEnabled } && toggles().first().isChecked)
    val locked = testing.readBytes()
    toggles().forEach { it.userCheck(!it.isChecked) }
    check(testing.readBytes().contentEquals(locked))
    binding.testingResolution4x.performClick()
    check(NativeLibrary.reloadCount == 1 && NativeLibrary.framebufferCount == 1)
    openPanel("")
    binding.testingResolution2x.performClick()
    check(NativeLibrary.reloadCount == 1) { "Global Options reloaded a running title" }
    check(toggles().none { it.isEnabled })
    NativeLibrary.running = false; EmulationActivity.running = false
    openPanel("0004000000123400")
    binding.testingRenderThreadDelay.performClick()
    checkNotNull(AlertDialog.lastShown).apply {
        checkNotNull(input).setText("16001");getButton(AlertDialog.BUTTON_POSITIVE).performClick()
        check(!dismissed)
        checkNotNull(input).setText("0");getButton(AlertDialog.BUTTON_POSITIVE).performClick()
    }
    check(readTesting().get("Testing.0004000000123400","delay_game_render_thread_us") == "0")
    val alphaBytes=testing.readBytes()
    NativeLibrary.alphaTestingAvailable=false;panel.render()
    check(!binding.testingControls.visible && toggles().none { it.isEnabled })
    toggles().forEach { it.userCheck(!it.isChecked) }
    check(testing.readBytes().contentEquals(alphaBytes))
    NativeLibrary.alphaTestingAvailable=true
    check(normal.readBytes().contentEquals(savedNormal))
    println("PASS: production quick experiment controls, scope, reset confirmation, launch snapshots/locks and alpha gating")
}

''',
    }

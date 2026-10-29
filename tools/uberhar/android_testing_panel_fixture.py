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
    var checkedId = 0
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
        if (previous != 0) selected?.invoke(this, previous, false)
        selected?.invoke(this, id, true)
    }
    fun userCheck(value: Boolean) { if (isEnabled && visible) isChecked = value }
}
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
    val testingHardwareShaders = PanelWidget()
    val testingHardwareDescription = PanelWidget()
    val testingRenderThreadDelay = PanelWidget()
    val testingLinearFiltering = PanelWidget()
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

// CodexAstraLocal: Drive production listeners from global Options and a title, then inspect disk.
fun checkTestingPanel(folder: File) {
    val directory = File(folder, "panel-controls").apply { mkdirs() }
    DirectoryInitialization.userDirectory = directory.path
    val normal = File(directory, "config/config.ini").apply {
        parentFile.mkdirs(); writeText("[Renderer]\nuse_hw_shader=true\nfilter_mode=false\n")
    }
    val savedNormal = normal.readBytes()
    val testing = File(directory, "config/testing.ini")
    fun readTesting() = testing.inputStream().use(::Wini)
    val owner = object : androidx.lifecycle.LifecycleOwner {}
    val binding = PanelTestingBinding()
    val panel = TestingPanelController(binding, owner, "")
    val launchControls = listOf(binding.testingDisableRightEyeRender, binding.testingHardwareShaders,
        binding.testingRenderThreadDelay, binding.testingLinearFiltering,
        binding.testingStereoDepth0, binding.testingStereoDepth1)
    check(!binding.testingControls.visible && launchControls.none { it.isEnabled })
    binding.testingEnabled.userCheck(true)
    check(binding.testingControls.visible && launchControls.all { it.isEnabled }) {
        "Global Options Testing controls are inaccessible"
    }
    check(!binding.testingUseGlobal.visible && !binding.gameDriver.visible)
    binding.testingDisableRightEyeRender.userCheck(true)
    binding.testingHardwareShaders.userCheck(true)
    binding.testingLinearFiltering.userCheck(false)
    binding.testingStereoDepth1.performClick()
    binding.testingRenderThreadDelay.performClick()
    val delay = checkNotNull(AlertDialog.lastShown)
    checkNotNull(delay.input).setText("16001")
    delay.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
    check(delay.input.error != null && !delay.dismissed)
    check(readTesting().get("Testing", "delay_game_render_thread_us") == "0")
    delay.input.setText("100")
    delay.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
    check(delay.dismissed)
    for ((key, value) in mapOf("disable_right_eye_render" to "true", "use_hw_shader" to "true",
        "delay_game_render_thread_us" to "100", "filter_mode" to "false", "factor_3d" to "1")) {
        check(readTesting().get("Testing", key) == value) { "Global $key edit did not persist" }
    }
    panel.render()
    check(binding.testingDisableRightEyeRender.isChecked && binding.testingHardwareShaders.isChecked)
    check(!binding.testingLinearFiltering.isChecked)
    check(binding.testingStereoDepth.checkedId == R.id.testing_stereo_depth_1)
    check(binding.testingRenderThreadDelay.text.toString().endsWith("100"))

    // CodexAstraLocal: The explicit CPU preset veto remains visible and preserves the stored request.
    binding.testingPresets.children[1].performClick()
    check(!binding.testingHardwareShaders.isEnabled && !binding.testingHardwareShaders.isChecked)
    check(readTesting().get("Testing", "use_hw_shader") == "true")
    binding.testingPresets.children[2].performClick()
    check(binding.testingHardwareShaders.isEnabled && binding.testingHardwareShaders.isChecked)

    panel.setGame("000400000F700100")
    check(launchControls.all { it.isEnabled } && binding.testingUseGlobal.visible)
    check(binding.testingDisableRightEyeRender.isChecked && binding.testingHardwareShaders.isChecked)
    val globalBefore = checkNotNull(readTesting()["Testing"]).toMap()
    binding.testingDisableRightEyeRender.userCheck(false)
    binding.testingHardwareShaders.userCheck(false)
    binding.testingLinearFiltering.userCheck(true)
    binding.testingStereoDepth0.performClick()
    binding.testingRenderThreadDelay.performClick()
    checkNotNull(AlertDialog.lastShown).apply {
        checkNotNull(input).setText("0"); getButton(AlertDialog.BUTTON_POSITIVE).performClick()
    }
    check(readTesting().get("Testing.000400000F700100", "disable_right_eye_render") == "false")
    check(checkNotNull(readTesting()["Testing"]).toMap() == globalBefore) { "Title edits changed global defaults" }
    panel.setGame("0004000000123400")
    check(binding.testingDisableRightEyeRender.isChecked && binding.testingHardwareShaders.isChecked)
    panel.setGame("000400000F700100")
    check(!binding.testingDisableRightEyeRender.isChecked && !binding.testingHardwareShaders.isChecked)
    binding.testingUseGlobal.performClick()
    checkNotNull(AlertDialog.lastShown).getButton(AlertDialog.BUTTON_NEGATIVE).performClick()
    check(!binding.testingDisableRightEyeRender.isChecked)
    binding.testingUseGlobal.performClick()
    checkNotNull(AlertDialog.lastShown).getButton(AlertDialog.BUTTON_POSITIVE).performClick()
    check(binding.testingDisableRightEyeRender.isChecked && binding.testingHardwareShaders.isChecked)
    check(readTesting().get("Testing.000400000F700100", "disable_right_eye_render") == null)

    // CodexAstraLocal: Running title owns edits even if Options was previously showing globals.
    panel.setGame("")
    NativeLibrary.running = true
    EmulationActivity.running = true
    NativeLibrary.requestedRightEye = false
    NativeLibrary.effectiveHardware = false
    NativeLibrary.effectiveDelay = 17
    NativeLibrary.effectiveFiltering = true
    NativeLibrary.effectiveDepth = 0
    panel.render()
    check(launchControls.none { it.isEnabled } && !binding.testingEnabled.isEnabled)
    check(binding.testingPresets.children.none { it.isEnabled })
    check(!binding.testingDisableRightEyeRender.isChecked && !binding.testingHardwareShaders.isChecked)
    check(binding.testingLinearFiltering.isChecked)
    check(binding.testingStereoDepth.checkedId == R.id.testing_stereo_depth_0)
    check(binding.testingRenderThreadDelay.text.toString().endsWith("17"))
    val runningBytes = testing.readBytes()
    binding.testingHardwareShaders.userCheck(true)
    binding.testingDisableRightEyeRender.userCheck(true)
    binding.testingRenderThreadDelay.performClick()
    binding.testingLinearFiltering.userCheck(false)
    binding.testingStereoDepth1.performClick()
    binding.testingPresets.children[0].performClick()
    binding.testingEnabled.userCheck(false)
    check(testing.readBytes().contentEquals(runningBytes)) { "Launch controls edited while running" }
    check(binding.testingResolution4x.isEnabled)
    binding.testingResolution4x.performClick()
    check(readTesting().get("Testing.000400000F700100", "resolution") == "4")
    check(readTesting().get("Testing", "resolution") == "2")
    check(NativeLibrary.reloadCount == 1 && NativeLibrary.framebufferCount == 1)
    check(!binding.testingHardwareShaders.isChecked && binding.testingLinearFiltering.isChecked)
    check(binding.testingStereoDepth.checkedId == R.id.testing_stereo_depth_0)
    NativeLibrary.running = false
    EmulationActivity.running = false

    // CodexAstraLocal: Alpha gating and Testing OFF hide/disable experiments, preserving all files.
    val alphaBytes = testing.readBytes()
    NativeLibrary.alphaTestingAvailable = false
    panel.render()
    check(!binding.testingEnabled.visible && !binding.testingControls.visible)
    check(launchControls.none { it.isEnabled } && !binding.testingResolution4x.isEnabled)
    binding.testingDisableRightEyeRender.userCheck(true)
    check(testing.readBytes().contentEquals(alphaBytes))
    NativeLibrary.alphaTestingAvailable = true
    panel.render()
    binding.testingEnabled.userCheck(false)
    check(!binding.testingControls.visible && launchControls.none { it.isEnabled })
    check(normal.readBytes().contentEquals(savedNormal)) { "Testing changed saved Custom preferences" }
    println("PASS: real TestingPanelController global/title interactions, reset cancel/confirm, launch locks, CPU veto, live resolution and alpha gating (host UI/JNI boundaries)")
}
''',
    }

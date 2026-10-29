#!/usr/bin/env python3
"""CodexAstraLocal: Execute the production settings fragment's view lifecycle on the JVM.

Platform/binding boundaries model the observed RecyclerView layout-params contract;
this is not an Android rendering or device test. An optional committed baseline
must compile and fail at that exact contract before the repaired source may pass.
Requires a local kotlinc and compatible JAVA_HOME; uses no SDK, network or device.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


# CodexAstraLocal: Compile the whole production fragment and its real view interface;
# only Android, generated bindings, presenter/data and sibling UI boundaries are fakes.
STUBS = {
    "Context.kt": """package android.content
open class Context
""",
    "Bundle.kt": """package android.os
class Bundle {
    private val values = mutableMapOf<String, String?>()
    fun putString(key: String, value: String?) { values[key] = value }
    fun getString(key: String) = values[key]
}
""",
    "Views.kt": """package android.view
open class View {
    var visible = true
    var layoutParams: ViewGroup.LayoutParams? = null
    var insets: ((View, androidx.core.view.WindowInsetsCompat) -> androidx.core.view.WindowInsetsCompat)? = null
    var padding = listOf(0, 0, 0)
}
open class ViewGroup : View() {
    data class LayoutParams(val width: Int, val height: Int)
}
class LayoutInflater
""",
    "CoreViews.kt": """package androidx.core.view
import android.view.View
var View.isVisible: Boolean
    get() = visible
    set(value) { visible = value }
fun View.updatePadding(left: Int, right: Int, bottom: Int) { padding = listOf(left, right, bottom) }
class WindowInsetsCompat {
    data class Insets(val left: Int, val right: Int, val bottom: Int)
    object Type { fun systemBars() = 1; fun displayCutout() = 2 }
    fun getInsets(type: Int) = if (type == 1) Insets(2, 3, 4) else Insets(5, 6, 0)
}
object ViewCompat {
    fun setOnApplyWindowInsetsListener(view: View, listener: (View, WindowInsetsCompat) -> WindowInsetsCompat) {
        view.insets = listener
    }
}
""",
    "Fragment.kt": """package androidx.fragment.app
import android.content.Context
import android.os.Bundle
import android.view.*
open class Fragment {
    var arguments: Bundle? = null
    var activity: Context? = null
    val layoutInflater = LayoutInflater()
    val viewLifecycleOwner = Any()
    fun requireArguments() = arguments!!
    fun requireActivity() = activity!!
    fun getString(id: Int, vararg args: Any?) = "$id ${args.joinToString()}".trim()
    open fun onAttach(context: Context) { activity = context }
    open fun onCreate(savedInstanceState: Bundle?) {}
    open fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View = View()
    open fun onViewCreated(view: View, savedInstanceState: Bundle?) {}
    open fun onDestroyView() {}
    open fun onDetach() { activity = null }
}
""",
    "RecyclerView.kt": """package androidx.recyclerview.widget
import android.content.Context
import android.view.*
class LinearLayoutManager(context: Context?)
class RecyclerView : ViewGroup() {
    var layoutManager: LinearLayoutManager? = null
    var adapter: Adapter<*>? = null
    var layoutParamsRequests = 0
    // CodexAstraLocal: Android's LayoutInflater requests parent params even without attachment.
    fun generateLayoutParams(width: Int, height: Int): LayoutParams {
        check(layoutManager != null) { "RecyclerView has no LayoutManager" }
        layoutParamsRequests++
        return LayoutParams(width, height)
    }
    open class ViewHolder(val itemView: View)
    abstract class Adapter<VH : ViewHolder> {
        abstract fun getItemCount(): Int
        abstract fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH
        abstract fun onBindViewHolder(holder: VH, position: Int)
    }
}
class ConcatAdapter(vararg children: RecyclerView.Adapter<*>) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
    val adapters = children.toList()
    override fun getItemCount() = adapters.sumOf { it.getItemCount() }
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) = error("Not part of this boundary")
    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) = Unit
}
""",
    "Bindings.kt": """package org.citra.citra_emu.databinding
import android.view.*
import androidx.recyclerview.widget.RecyclerView
class ScopeText : View() { var text: CharSequence = "" }
class PanelTestingBinding(val root: View = View())
class FragmentSettingsBinding(val root: View = View(), val listSettings: RecyclerView = RecyclerView()) {
    companion object {
        lateinit var latest: FragmentSettingsBinding
        fun inflate(inflater: LayoutInflater) = FragmentSettingsBinding().also { latest = it }
    }
}
class ListItemSettingsTestingBinding(val root: View = View(), val testingPanel: PanelTestingBinding = PanelTestingBinding(), val settingsScope: ScopeText = ScopeText()) {
    companion object {
        lateinit var latest: ListItemSettingsTestingBinding
        fun inflate(inflater: LayoutInflater, parent: RecyclerView, attachToRoot: Boolean): ListItemSettingsTestingBinding {
            check(!attachToRoot) { "The adapter must own header attachment" }
            return ListItemSettingsTestingBinding().also {
                it.root.layoutParams = parent.generateLayoutParams(HEADER_WIDTH, HEADER_HEIGHT)
                latest = it
            }
        }
    }
}
""",
    "Model.kt": """package org.citra.citra_emu.features.settings.model
interface AbstractSetting
class Settings(var isPerGame: Boolean = false)
object TestingSettings {
    var enabled = false
    var preset = 0
    var resolution = 2
    var loadedId = ""
    fun load(gameId: String) { loadedId = gameId }
}
""",
    "SettingsItem.kt": """package org.citra.citra_emu.features.settings.model.view
class SettingsItem(val key: String)
""",
    "SettingsFile.kt": """package org.citra.citra_emu.features.settings.utils
object SettingsFile {
    const val FILE_NAME_CONFIG = "config"
    fun normalizeGameId(value: String): String? = value.takeIf { it.length == 16 && it != "0000000000000000" }
}
""",
    "Activity.kt": """package org.citra.citra_emu.activities
class EmulationActivity
""",
    "Resources.kt": """package org.citra.citra_emu
object NativeLibrary {
    var running = false
    var titleId = 0L
    fun isRunning() = running
    fun getRunningTitleId() = titleId
}
object R { object string {
    const val uberhar_game_settings_scope = 1
    const val uberhar_global_settings_scope = 2
    const val uberhar_testing_active_summary = 3
    const val uberhar_testing_native = 4
    const val uberhar_testing_combo_cpu = 5
    const val uberhar_testing_combo_gpu = 6
    const val uberhar_testing_custom = 7
} }
""",
    "SettingsUi.kt": """package org.citra.citra_emu.features.settings.ui
import android.content.Context
import android.view.*
import androidx.recyclerview.widget.RecyclerView
import org.citra.citra_emu.databinding.PanelTestingBinding
import org.citra.citra_emu.features.settings.model.*
import org.citra.citra_emu.features.settings.model.view.SettingsItem
interface SettingsActivityView {
    val settings: Settings
    fun showSettingsFragment(menuTag: String, addToStack: Boolean, gameId: String)
    fun showToastMessage(message: String, isLong: Boolean)
    fun onSettingChanged()
}
class SettingsAdapter(view: SettingsFragmentView, context: Context) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
    var rows = arrayListOf<SettingsItem>()
    var closed = false
    fun setSettingsList(value: ArrayList<SettingsItem>) { rows = value }
    fun closeDialog() { closed = true }
    override fun getItemCount() = rows.size
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) = RecyclerView.ViewHolder(View())
    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) = Unit
}
class SettingsFragmentPresenter(private val view: SettingsFragmentView) {
    fun onCreate(menuTag: String, gameId: String) {}
    fun onViewCreated(adapter: SettingsAdapter) = loadSettingsList()
    fun loadSettingsList() {
        lastBuiltScope = TestingSettings.loadedId
        view.showSettingsList(arrayListOf(SettingsItem("graphics"), SettingsItem("audio")))
    }
    fun putSetting(setting: AbstractSetting) {}
    companion object { var lastBuiltScope = "" }
}
class TestingPanelController(binding: PanelTestingBinding, owner: Any, gameId: String, val changed: () -> Unit) {
    var renders = 0
    init { instances.add(this) }
    fun render() { renders++ }
    companion object { val instances = mutableListOf<TestingPanelController>() }
}
""",
    "FragmentCheck.kt": """import android.content.Context
import androidx.core.view.WindowInsetsCompat
import androidx.recyclerview.widget.ConcatAdapter
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.databinding.*
import org.citra.citra_emu.features.settings.model.*
import org.citra.citra_emu.features.settings.ui.*

class Activity(val title: String) : Context(), SettingsActivityView {
    override val settings = Settings(title.isNotEmpty())
    var navigation: Triple<String, Boolean, String>? = null
    override fun showSettingsFragment(menuTag: String, addToStack: Boolean, gameId: String) {
        navigation = Triple(menuTag, addToStack, gameId)
    }
    override fun showToastMessage(message: String, isLong: Boolean) {}
    override fun onSettingChanged() {}
}

fun open(menu: String, title: String): Pair<SettingsFragment, Activity> {
    val fragment = SettingsFragment.newInstance(menu, title) as SettingsFragment
    val activity = Activity(title)
    fragment.onAttach(activity)
    fragment.onCreate(null)
    val view = fragment.onCreateView(fragment.layoutInflater, null, null)
    fragment.onViewCreated(view, null)
    return fragment to activity
}

fun main(args: Array<String>) {
    // CodexAstraLocal: A compile error or unrelated exception cannot qualify the old-tree control.
    if (args.singleOrNull() == "--expect-inflation-failure") {
        try { open("config", "") } catch (e: IllegalStateException) {
            check(e.message == "RecyclerView has no LayoutManager") { "Wrong negative-control failure: $e" }
            println("PASS: pre-fix production fragment reproduces RecyclerView layout-params failure")
            return
        }
        error("Pre-fix fragment unexpectedly reached its settings rows")
    }

    // CodexAstraLocal: Exercise both scopes, submenu navigation and view recreation with real methods.
    for (title in listOf("", "000400000F700100")) for (menu in listOf("config", "graphics")) {
        TestingPanelController.instances.clear()
        TestingSettings.enabled = title.isNotEmpty()
        val (fragment, activity) = open(menu, title)
        val first = FragmentSettingsBinding.latest
        val firstHeader = ListItemSettingsTestingBinding.latest
        val concatenated = first.listSettings.adapter as ConcatAdapter
        check(first.listSettings.layoutParamsRequests == 1)
        check(firstHeader.root.layoutParams == android.view.ViewGroup.LayoutParams(-1, -2))
        check(concatenated.adapters.size == 2 && concatenated.getItemCount() == 3)
        val headerAdapter = concatenated.adapters[0]
        check(headerAdapter.getItemCount() == 1)
        check(headerAdapter.onCreateViewHolder(first.listSettings, 0).itemView === firstHeader.root)
        val rows = concatenated.adapters[1] as SettingsAdapter
        check(rows.rows.map { it.key } == listOf("graphics", "audio"))
        check(firstHeader.testingPanel.root.visible == (menu == "config"))
        check(firstHeader.settingsScope.visible && firstHeader.settingsScope.text.isNotEmpty())
        check(TestingSettings.loadedId == title)
        check(SettingsFragmentPresenter.lastBuiltScope == title)
        check(TestingPanelController.instances.size == if (menu == "config") 1 else 0)
        // CodexAstraLocal: Resume must restore this fragment's scope before presenter row construction.
        TestingSettings.load("0004000000123400")
        fragment.loadSettingsList()
        check(SettingsFragmentPresenter.lastBuiltScope == title)
        fragment.loadSubMenu("audio")
        check(activity.navigation == Triple("audio", true, title))
        first.root.insets!!(first.root, WindowInsetsCompat())
        check(first.listSettings.padding == listOf(7, 9, 4))
        val firstController = TestingPanelController.instances.lastOrNull()
        firstController?.changed?.invoke()
        val oldRenders = firstController?.renders
        fragment.onDestroyView()
        check(first.listSettings.adapter == null)
        val recreated = fragment.onCreateView(fragment.layoutInflater, null, null)
        fragment.onViewCreated(recreated, null)
        val second = FragmentSettingsBinding.latest
        check(second !== first && ListItemSettingsTestingBinding.latest !== firstHeader)
        check(second.listSettings.layoutParamsRequests == 1)
        check((second.listSettings.adapter as ConcatAdapter).getItemCount() == 3)
        check(firstController?.renders == oldRenders) { "Recreated fragment retained its old controller" }
        val lastRows = (second.listSettings.adapter as ConcatAdapter).adapters[1] as SettingsAdapter
        fragment.onDestroyView()
        fragment.onDetach()
        check(lastRows.closed)
    }
    NativeLibrary.running = true
    NativeLibrary.titleId = 0x000400000F700100L
    // CodexAstraLocal: Active emulation cannot retarget global or another title's editor.
    for (title in listOf("", "000400000F700100", "0004000000123400")) {
        for (menu in listOf("config", "graphics")) {
            val (running, _) = open(menu, title)
            check(SettingsFragmentPresenter.lastBuiltScope == title)
            TestingSettings.load("0004000000000001")
            running.loadSettingsList()
            check(SettingsFragmentPresenter.lastBuiltScope == title)
            running.onDestroyView()
            running.onDetach()
        }
    }
    println("PASS: production fragment root/submenu and global/title creation, parent layout params, scrolling-header adapter composition, ordinary-row navigation, insets, recreation and explicit scope during resume/emulation")
}
""",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kotlinc", required=True)
    parser.add_argument("--output", default="build/android-settings-fragment-check")
    parser.add_argument("--baseline-ref", help="Optional committed pre-fix source for a runtime negative control")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = (root / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    relative = "src/android/app/src/main/java/org/citra/citra_emu/features/settings/ui/SettingsFragment.kt"
    production = root / relative
    xml = ET.parse(root / "src/android/app/src/main/res/layout/list_item_settings_testing.xml").getroot()
    dimension = {"match_parent": -1, "wrap_content": -2}
    android = "{http://schemas.android.com/apk/res/android}"
    width = dimension[xml.attrib[android + "layout_width"]]
    height = dimension[xml.attrib[android + "layout_height"]]
    files = [production.with_name("SettingsFragmentView.kt")]
    for name, content in STUBS.items():
        target = output / name
        target.write_text(content.replace("HEADER_WIDTH", str(width)).replace("HEADER_HEIGHT", str(height)))
        files.append(target)
    java = Path(os.environ["JAVA_HOME"]) / "bin/java"
    results = {}

    # CodexAstraLocal: Retain compile/run evidence for each independently compiled source tree.
    def run(label, source, runtime_args):
        jar = output / f"{label}.jar"
        command = [args.kotlinc, str(source), *map(str, files), "-include-runtime", "-d", str(jar)]
        with (output / f"{label}-compile.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        if result.returncode:
            raise RuntimeError(f"{label} compile failed; see {output / (label + '-compile.log')}")
        result = subprocess.run([str(java), "-jar", str(jar), *runtime_args], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / f"{label}-run.log").write_text(result.stdout)
        print(result.stdout, end="", flush=True)
        if result.returncode:
            raise RuntimeError(f"{label} execution failed")
        results[label] = "passed"

    try:
        if args.baseline_ref:
            old_source = subprocess.run(["git", "show", f"{args.baseline_ref}:{relative}"], cwd=root,
                                        text=True, stdout=subprocess.PIPE, check=True).stdout
            baseline = output / "BaselineSettingsFragment.kt"
            baseline.write_text(old_source)
            run("baseline", baseline, ["--expect-inflation-failure"])
        run("repaired", production, [])
        results["status"] = "passed"
    except Exception as error:
        results.update(status="failed", error=str(error))
        raise
    finally:
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()

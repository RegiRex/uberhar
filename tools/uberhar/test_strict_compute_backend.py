#!/usr/bin/env python3
"""CodexAstraLocal: Execute renderer selection and terminal startup boundaries.

The factory translation unit is unchanged; renderer/platform constructors are
recording endpoints. Actual Init, restore and Libretro exception boundaries are
extracted. These tests prove route/containment decisions, not driver execution.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path.cwd()


# CodexAstraLocal: Match a complete existing braced section, refusing missing or
# unbalanced source rather than silently testing a handwritten replacement.
def block(source, anchor):
    start = source.index(anchor)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise ValueError("Unbalanced source: " + anchor)


def catch_pair(source, anchor):
    first = block(source, anchor)
    start = source.index(anchor) + len(first)
    tail = source[start:]
    return first + tail[:tail.index("catch")] + block(tail, "catch")


# CodexAstraLocal: Small endpoints expose constructed backends and ownership
# release while keeping the real terminal exception and factory code under test.
STUB = r'''
#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "video_core/shader_recovery_error.h"
using u8 = unsigned char;
using u32 = unsigned;
namespace Frontend { struct EmuWindow {}; }
namespace Pica { struct PicaCore {}; }
namespace Core { struct System; }
namespace Settings {
enum class GraphicsAPI { Software, OpenGL, Vulkan, Unknown };
enum class UberharTestMode { Custom, Native, Compute, Automatic, ComboGeneric };
template<class T> struct Setting { T value{}; T GetValue() const {return value;} };
inline struct Values {
    Setting<GraphicsAPI> graphics_api;
    Setting<UberharTestMode> uberhar_test_mode;
} values;
inline GraphicsAPI GetWorkingGraphicsAPI() {return values.graphics_api.GetValue();}
}
inline unsigned constructed{}, selected{}, live{}, checks{}, accessed{};
inline unsigned gpu_failure{}, recreating{}, state_failure{}, messages{}, frontend_stops{};
inline void Check(bool condition, const char* text) {
    ++checks; if (!condition) throw std::runtime_error{text};
}
namespace VideoCore {
struct RendererBase {virtual ~RendererBase() = default;};
struct Owner {Owner(){++live;} ~Owner(){--live;}};
struct GPU {
    Owner owner;
    std::unique_ptr<RendererBase> renderer;
    GPU(Core::System&, Frontend::EmuWindow&, Frontend::EmuWindow*);
    void RecreateRenderer(Frontend::EmuWindow&, Frontend::EmuWindow*) {
        if (recreating) throw ShaderRecoveryError{"strict recreate refused"};
    }
};
}
#define BACKEND(NS, NAME, ID) namespace NS { \
struct NAME final : VideoCore::RendererBase { \
    template<class... T> explicit NAME(T&&...) {++constructed; selected=ID;} }; }
BACKEND(SwRenderer, RendererSoftware, 0)
BACKEND(OpenGL, RendererOpenGL, 1)
BACKEND(Vulkan, RendererVulkan, 2)
'''


# CodexAstraLocal: Exercise actual copied exception boundaries with explicit
# backend, constructor, restore and context failures; other exceptions propagate.
MAIN = r'''
#include <iostream>
#include "stub.h"
#include "common/scope_exit.h"
#include "common/logging/log.h"
#include "video_core/video_core.h"
namespace Core {
struct System {
    enum class ResultStatus {Success, ErrorRendererRecovery, ErrorSavestate, ShutdownRequested};
    std::unique_ptr<VideoCore::GPU> gpu;
    Frontend::EmuWindow window;
    Frontend::EmuWindow* m_emu_window{&window};
    Frontend::EmuWindow* m_secondary_window{};
    bool powered{};
    ResultStatus next_step{ResultStatus::Success};
    ResultStatus SingleStep() {return next_step;}
    unsigned shutdowns{};
    std::string status_details, m_filepath{"title"}, m_chainloadpath;
    struct Loader {bool DoingInitialSetup() const {return false;}};
    Loader* app_loader{};
    void SetStatus(ResultStatus, const char* s) {status_details=s;}
    const std::string& GetStatusDetails() const {return status_details;}
    bool IsPoweredOn() const {return powered;}
    void Shutdown() {++shutdowns; powered=false; gpu.reset();}
    VideoCore::GPU& GPU() {return *gpu;}
    static System& GetInstance() {static System system; return system;}
    ResultStatus Init(Frontend::EmuWindow& emu_window,
                      Frontend::EmuWindow* secondary_window, int=0, unsigned=2) {
        INIT_BOUNDARY
        powered=true; return ResultStatus::Success;
    }
    ResultStatus Load() {
        auto& emu_window=window; Frontend::EmuWindow* secondary_window=nullptr;
        int system_mem_mode=0; unsigned num_cores=2;
        LOAD_BOUNDARY
        return ResultStatus::Success;
    }
    ResultStatus Load(Frontend::EmuWindow&, const std::string&, Frontend::EmuWindow*) {
        return Load();
    }
    ResultStatus Reset() { RESET_TAIL }
    ResultStatus ResetSignal() { RESET_SIGNAL }
    void Restore() {
        Shutdown(); int mem_mode=0; unsigned num_cores=2;
        RESTORE_BOUNDARY
        ++accessed;
    }
    bool LoadStateBuffer(std::vector<u8>) {
        if (state_failure==1) throw std::runtime_error{"invalid state before shutdown"};
        Restore(); return true;
    }
    void LoadState(u32) {LoadStateBuffer({});}
    ResultStatus LoadStateSignal() {
        const u32 slot=0;
        LOAD_SIGNAL
        return ResultStatus::Success;
    }
};
}
VideoCore::GPU::GPU(Core::System& system, Frontend::EmuWindow& window,
                    Frontend::EmuWindow* secondary) {
    if (gpu_failure==1) throw ShaderRecoveryError{"strict initialization refused"};
    if (gpu_failure==2) throw std::logic_error{"unrelated constructor failure"};
    Pica::PicaCore pica;
    renderer=VideoCore::CreateRenderer(window, secondary, pica, system);
}
struct Emulator {
    bool game_loaded{true};
    std::unique_ptr<Frontend::EmuWindow> emu_window{std::make_unique<Frontend::EmuWindow>()};
} emulator;
auto* emu_instance=&emulator;
namespace LibRetro {
bool DisplayMessage(const char*) {++messages; return true;}
bool Shutdown() {++frontend_stops; return true;}
}
bool DrainAsyncOperations(Core::System&) {return true;}
void ContextReset() { CONTEXT_BOUNDARY }
UNSERIALIZE

// CodexAstraLocal: Real frontend terminal branches execute against observable
// endpoints. A powered ordinary save error must still reach its existing UI.
unsigned continue_dialogs{}, worker_iterations{}, error_signals{}, retro_continues{};
unsigned step_notifications{};
unsigned frontend_cleanups{};
bool stop_run{}, pause_emulation{}, startup{};
using Status=Core::System::ResultStatus;
namespace Common::UberharActivity {
void SetStartup(bool value) {startup=value;}
}
void TryShutdown() {++frontend_cleanups; Core::System::GetInstance().Shutdown();}
Status AndroidLoadDecision(Status load_result) {
    ANDROID_LOAD_EXIT
    Check(!stop_run && !pause_emulation,"successful load did not start session");
    return Status::Success;
}
Status AndroidDecision(Status result) {
    auto& system=Core::System::GetInstance();
    ANDROID_TERMINAL
    ++continue_dialogs; return Status::Success;
}
void ErrorThrown(Status, const std::string&) {++error_signals;}
#define emit
void QtWorkerDecision(Status result) {
    auto& system=Core::System::GetInstance();
    for(unsigned guard=0;guard<2;++guard) {
        QT_TERMINAL
        ++worker_iterations;
    }
}
void DebugModeEntered() {++step_notifications;}
void yieldCurrentThread() {}
void QtStepDecision(Status status) {
    auto& system=Core::System::GetInstance();
    system.next_step=status;
    do { QT_STEP_BOUNDARY } while(false);
}
void RetroDecision(Status result) {
    RETRO_TERMINAL
    ++retro_continues;
}

int main() {
    try {
        using namespace Settings;
        auto& system=Core::System::GetInstance();
        Pica::PicaCore pica;
        for(unsigned mode=0;mode<5;++mode) for(unsigned api=0;api<4;++api) {
            values.uberhar_test_mode.value=static_cast<UberharTestMode>(mode);
            values.graphics_api.value=static_cast<GraphicsAPI>(api);
            constructed=0; bool refused=false;
            try {auto renderer=VideoCore::CreateRenderer(system.window,nullptr,pica,system);}
            catch(const VideoCore::ShaderRecoveryError& error) {
                refused=true;
                Check(std::string{error.what()}.find("fallback is disabled")!=std::string::npos,
                      "strict refusal lost purpose");
            }
            const bool must_refuse=mode==2 && !(HAS_VULKAN && api==2);
            Check(refused==must_refuse,"strict backend route changed");
            Check(constructed==(must_refuse?0U:1U),"substitute constructed on refusal");
            if(!refused) {
                const unsigned expected=(api==0 && HAS_SOFTWARE)?0:
                    (api==1 && HAS_OPENGL)?1:(api==2 && HAS_VULKAN)?2:
                    HAS_OPENGL?1:HAS_VULKAN?2:0;
                Check(selected==expected,"non-strict backend selection changed");
            }
        }
        values.uberhar_test_mode.value=UberharTestMode::Native;
        values.graphics_api.value=GraphicsAPI::Unknown;
        gpu_failure=1; unsigned before=system.shutdowns;
        Check(system.Load()==Core::System::ResultStatus::ErrorRendererRecovery,
              "constructor refusal did not return terminal status");
        Check(!system.gpu && !system.powered && live==0 && system.shutdowns==before+1,
              "initial refusal did not unwind and clean up");
        Check(system.status_details=="strict initialization refused","failure detail lost");
        gpu_failure=2; bool unrelated=false;
        try {system.Load();} catch(const std::logic_error&) {unrelated=true;}
        Check(unrelated && !live,"unrelated failure swallowed");
        gpu_failure=0;
        Check(system.Load()==Core::System::ResultStatus::Success,"normal load failed");
        gpu_failure=1; bool restore_refused=false; accessed=0;
        try {system.Restore();} catch(const VideoCore::ShaderRecoveryError&) {restore_refused=true;}
        Check(restore_refused && !accessed && !system.gpu && !system.powered && !live,
              "restore continued without a renderer");
        gpu_failure=0; system.Load(); recreating=1; emulator.game_loaded=true;
        ContextReset();
        Check(!emulator.game_loaded && !system.powered && !live && messages==1 && frontend_stops==1,
              "context callback retained failed session");
        recreating=0; system.Load(); emulator.game_loaded=true; state_failure=1;
        const u8 byte=0; before=system.shutdowns;
        Check(!retro_unserialize(&byte,1) && emulator.game_loaded && system.powered &&
              system.shutdowns==before && frontend_stops==1,
              "ordinary invalid state stopped intact session");
        state_failure=0; gpu_failure=1;
        Check(!retro_unserialize(&byte,1) && !emulator.game_loaded && !system.powered &&
              !system.gpu && !live && frontend_stops==2,
              "failed restore left frontend running");
        gpu_failure=0; system.Load(); state_failure=1;
        Check(system.LoadStateSignal()==Status::ErrorSavestate && system.powered,
              "ordinary save error became terminal");
        state_failure=0; gpu_failure=1;
        Check(system.LoadStateSignal()==Status::ErrorRendererRecovery && !system.powered,
              "restore renderer failure became a recoverable save error");
        Check(system.status_details=="strict initialization refused","restore detail lost");
        gpu_failure=0; system.Load(); gpu_failure=1;
        Check(system.ResetSignal()==Status::ErrorRendererRecovery && !system.powered && !live,
              "reset swallowed reload failure");
        gpu_failure=0;
        Check(system.ResetSignal()==Status::Success && system.powered,
              "successful reset changed");
        for(const bool powered : {false,true}) {
            system.powered=powered;
            for(const auto status : {Status::ErrorRendererRecovery,Status::ErrorSavestate}) {
                const bool terminal=!powered || status==Status::ErrorRendererRecovery;
                continue_dialogs=worker_iterations=error_signals=retro_continues=0;
                AndroidDecision(status);
                QtWorkerDecision(status);
                Check(continue_dialogs==(terminal?0U:1U),"Android continued terminal session");
                Check(worker_iterations==(terminal?0U:2U) && error_signals==(terminal?1U:0U),
                      "Qt worker continued terminal session");
                error_signals=step_notifications=0;
                QtStepDecision(status);
                Check(step_notifications==(terminal?0U:1U) && error_signals==(terminal?1U:0U),
                      "Qt single step continued terminal session");
                emulator.game_loaded=true;
                before=frontend_stops;
                RetroDecision(status);
                Check(retro_continues==(terminal?0U:1U) &&
                      frontend_stops==before+(terminal?1U:0U) &&
                      emulator.game_loaded==!terminal,"Libretro continued terminal session");
                system.powered=powered;
            }
        }
        system.Shutdown();
        for(const auto result : {Status::Success, Status::ErrorRendererRecovery}) {
            system.Load(); stop_run=pause_emulation=startup=true;
            before=frontend_cleanups;
            Check(AndroidLoadDecision(result)==result && frontend_cleanups==before+1 &&
                  stop_run && !startup && !system.powered && !system.gpu && !live,
                  "Android initial-load exit retained frontend owners");
        }
        VideoCore::ShaderRecoveryError historical;
        Check(std::string{historical.what()}=="Uberhar native recovery shader unavailable",
              "historical shader failure text changed");
        std::cout << "PASS strict backend and terminal boundaries checks="<<checks<<'\n';
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n'; return 1;}
}
'''


def main():
    paths = [ROOT / p for p in (
        "src/video_core/video_core.cpp", "src/video_core/shader_recovery_error.h",
        "src/core/core.cpp", "src/citra_libretro/citra_libretro.cpp",
        "src/core/core.h", "src/android/app/src/main/jni/native.cpp",
        "src/citra_qt/bootmanager.cpp", "src/citra_qt/citra_qt.cpp",
        "tools/uberhar/test_strict_compute_backend.py",
        "src/common/scope_exit.h", "src/common/common_funcs.h", "src/common/common_types.h")]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    factory, _, core, retro, _, android, qt_worker, qt_ui = [p.read_text() for p in paths[:8]]
    init = catch_pair(core, "try {\n        gpu =")
    load_start = core.index("    ResultStatus init_result{Init(")
    load = core[load_start:core.index("\n    kernel->UpdateCPUAndMemoryState", load_start)]
    restore_start = core.index("        const System::ResultStatus result =\n            Init(")
    restore_end = core.index("\n    }\n\n    // Flush on save", restore_start)
    restore = core[restore_start:restore_end]
    context = catch_pair(retro, "try {\n                Core::System::GetInstance().GPU().RecreateRenderer")
    unserialize = block(retro, "bool retro_unserialize(")
    # CodexAstraLocal: Include the actual RunLoop load catches and reset return,
    # preventing direct-Restore tests from missing recoverable-error conversion.
    load_signal = catch_pair(core, "try {\n            System::LoadState(slot);")
    tail = core[core.index(load_signal) + len(load_signal):]
    load_signal += tail[:tail.index("catch")] + block(tail, "catch")
    reset = block(core, "System::ResultStatus System::Reset()")
    reset_tail = reset[reset.index("    Shutdown();"):].rsplit("}",1)[0]
    reset_signal = block(core, "case Signal::Reset:").split("{",1)[1].rsplit("}",1)[0]
    terminal_anchor = "if (result == Core::System::ResultStatus::ShutdownRequested ||"
    android_terminal = block(android, terminal_anchor)
    android_start = android.index("    surface_lock.unlock();") + len("    surface_lock.unlock();")
    android_load_exit = android[android_start:android.index(
        "    // AstraEH: This frontend progress screen", android_start)]
    qt_terminal = block(qt_worker, terminal_anchor)
    # CodexAstraLocal: The debugger step caller reaches the same RunLoop signals;
    # retain its actual result assignment through notification/yield as a unit.
    step_start = qt_worker.index("const Core::System::ResultStatus result = system.SingleStep();")
    qt_step = qt_worker[step_start:qt_worker.index("\n\n            was_active", step_start)]
    retro_terminal = block(retro, "if (result == Core::System::ResultStatus::ErrorRendererRecovery ||")
    # CodexAstraLocal: UI continuation is also constrained in the actual dialog;
    # the compiled worker check below is the lifetime/loop regression proof.
    assert "bool can_continue = system.IsPoweredOn();" in qt_ui
    assert "can_continue = false;" in block(qt_ui, "if (result == Core::System::ResultStatus::ErrorRendererRecovery)")
    assert "if (error_severity_icon == QMessageBox::Icon::Critical || !can_continue)" in qt_ui
    source = MAIN
    for key, value in {"INIT_BOUNDARY": init, "LOAD_BOUNDARY": load,
                       "RESTORE_BOUNDARY": restore, "CONTEXT_BOUNDARY": context,
                       "UNSERIALIZE": unserialize, "LOAD_SIGNAL": load_signal,
                       "RESET_TAIL": reset_tail, "RESET_SIGNAL": reset_signal,
                       "ANDROID_TERMINAL": android_terminal, "QT_TERMINAL": qt_terminal,
                       "QT_STEP_BOUNDARY": qt_step,
                       "ANDROID_LOAD_EXIT": android_load_exit,
                       "RETRO_TERMINAL": retro_terminal}.items():
        source = source.replace(key, value)
    parent = ROOT / "build/uberhar-probe/strict-compute-backend"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    (out / "stub.h").write_text(STUB)
    for name in ("common/settings.h", "video_core/gpu.h",
                 "video_core/renderer_vulkan/renderer_vulkan.h",
                 "video_core/renderer_opengl/renderer_opengl.h",
                 "video_core/renderer_software/renderer_software.h"):
        target = out / name; target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "stub.h"\n')
    (out / "common/logging").mkdir(parents=True, exist_ok=True)
    (out / "common/logging/log.h").write_text(
        '#pragma once\n#define LOG_CRITICAL(...) ((void)0)\n#define LOG_ERROR(...) ((void)0)\n'
        '#define LOG_INFO(...) ((void)0)\n')
    strict = block(factory, "if (Settings::values.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Compute)")
    # CodexAstraLocal: Deleting each new critical gate must fail actual executed
    # boundaries, not merely a source-string assertion. Compile four backend
    # configurations; modeled endpoints do not execute JNI, Qt events or Vulkan.
    cases = [
        ("vulkan-gl", factory, source, (1, 1, 0), True),
        ("vulkan", factory, source, (1, 0, 0), True),
        ("gl", factory, source, (0, 1, 0), True),
        ("software", factory, source, (0, 0, 1), True),
        ("removed-factory-guard", factory.replace(strict, ""), source, (1, 1, 0), False),
        ("false-init-success", factory, source.replace(
            "return ResultStatus::ErrorRendererRecovery;", "return ResultStatus::Success;"), (1, 1, 0), False),
        ("ignored-restore-status", factory, source.replace(
            block(restore, "if (result != ResultStatus::Success)"), ""), (1, 1, 0), False),
        ("live-failed-frontend", factory, source.replace(
            "emu_instance->game_loaded = false;", ""), (1, 1, 0), False),
        ("recoverable-restore-error", factory, source.replace(load_signal,
            load_signal.replace("return ResultStatus::ErrorRendererRecovery;",
                                "return ResultStatus::ErrorSavestate;")), (1, 1, 0), False),
        ("false-reset-success", factory, source.replace("return Reset();",
            "Reset(); return ResultStatus::Success;"), (1, 1, 0), False),
        ("android-continue", factory, source.replace(android_terminal, ""), (1, 1, 0), False),
        ("qt-continue", factory, source.replace(qt_terminal, ""), (1, 1, 0), False),
        ("qt-step-continue", factory, source.replace(qt_step,
            qt_step.replace(block(qt_step, terminal_anchor), "")), (1, 1, 0), False),
        ("retro-spin", factory, source.replace(retro_terminal, ""), (1, 1, 0), False),
        ("android-late-cleanup", factory, source.replace(android_load_exit,
            block(android_load_exit, "if (load_result !=") + android_load_exit.replace(
                block(android_load_exit, "if (load_result !="), "")), (1, 1, 0), False),
    ]
    # CodexAstraLocal: Require each injected defect's intended diagnostic so an
    # unrelated earlier failure cannot be mistaken for a working regression test.
    failures = {
        "removed-factory-guard": "strict backend route changed",
        "false-init-success": "constructor refusal did not return terminal status",
        "ignored-restore-status": "restore continued without a renderer",
        "live-failed-frontend": "context callback retained failed session",
        "recoverable-restore-error": "restore renderer failure became a recoverable save error",
        "false-reset-success": "reset swallowed reload failure",
        "android-continue": "Android continued terminal session",
        "qt-continue": "Qt worker continued terminal session",
        "qt-step-continue": "Qt single step continued terminal session",
        "retro-spin": "Libretro continued terminal session",
        "android-late-cleanup": "Android initial-load exit retained frontend owners",
    }
    results = []
    for name, factory_code, fixture, flags, success in cases:
        d = out / name; d.mkdir()
        (d / "factory.cpp").write_text(factory_code)
        (d / "main.cpp").write_text(fixture)
        command = [os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-I"+str(out), "-Isrc"]
        for key, value in zip(("VULKAN", "OPENGL", "SOFTWARE_RENDERER"), flags):
            if value: command.append("-DENABLE_" + key)
        command += [f"-DHAS_VULKAN={flags[0]}", f"-DHAS_OPENGL={flags[1]}",
                    f"-DHAS_SOFTWARE={flags[2]}", str(d / "factory.cpp"), str(d / "main.cpp"),
                    "-o", str(d / "test")]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=45)
        (d / "compile.log").write_text(compiled.stdout + compiled.stderr)
        compiled.check_returncode()
        run = subprocess.run([str(d / "test")], capture_output=True, text=True, timeout=10)
        (d / "execute.log").write_text(run.stdout + run.stderr)
        expected = 0 if success else 1
        if run.returncode != expected or (
                not success and run.stderr != "FAIL " + failures[name] + "\n"):
            raise RuntimeError(f"Unexpected {name}: {run.returncode}: {run.stdout}{run.stderr}")
        results.append({"name": name, "command": command, "expected": expected,
                        "returncode": run.returncode, "output": run.stdout + run.stderr})
    if hashes != {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}:
        raise RuntimeError("Source changed during backend proof")
    (out / "provenance.json").write_text(json.dumps({"author": "CodexAstraLocal",
        "source_sha256": hashes, "cases": results,
        "scope": "Actual factory and extracted exception boundaries; modeled constructors/frontend endpoints"}, indent=2)+"\n")
    print("PASS strict backend: four compiled-backend matrices and eleven required failure controls; " + str(out))


if __name__ == "__main__":
    main()

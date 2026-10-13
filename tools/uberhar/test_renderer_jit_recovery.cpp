// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Execute the extracted production CallSVC/Run/Step bodies on
// real generated A32 code. Only the SVC service itself injects a typed failure;
// no modeled JIT halt substitutes for the actual backend's CheckHalt boundary.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include "testenv.h"
#include "common/scope_exit.h"
#include "video_core/shader_recovery_error.h"

static unsigned checks;
static void Require(bool good, const char* message) {
    ++checks;
    if (!good) {
        std::fprintf(stderr, "FAIL %s check=%u\n", message, checks);
        std::exit(2);
    }
}
// CodexAstraLocal: Replace the test environment's unrelated assert macro only
// for the extracted owner preconditions, keeping diagnostics bounded and named.
#undef ASSERT
#define ASSERT(condition) Require((condition), "production assertion")
#define MICROPROFILE_SCOPE(...) ((void)0)

struct MemoryStub {
    void* GetCurrentPageTable() const { return nullptr; }
};
struct SvcStub {
    std::function<void(u32)> call;
    void CallSVC(u32 value) { call(value); }
};
// CodexAstraLocal: The driver inserts exact production source plus individually
// named intended defects here. Assertions/profiling are the only host adapters.
#include "recovery_actual.inc"

template<class Parent, class Callback>
static void Exercise() {
    for (bool step : {false, true}) for (bool switch_current : {false, true}) {
        Parent parent;
        Callback env{parent};
        Dynarmic::A32::Jit first{Dynarmic::A32::UserConfig{&env}};
        Dynarmic::A32::Jit other{Dynarmic::A32::UserConfig{&env}};
        parent.jit = &first;
        env.code_mem = {0xef0001ee, 0xe30a0071, 0xef000099, 0xeafffffe};
        first.SetCpsr(0x1d0);
        env.ticks_left = 16;
        unsigned callbacks = 0;
        env.svc_context.call = [&](u32 swi) {
            ++callbacks;
            if (swi == 0x99) { first.HaltExecution(); return; }
            Require(swi == 0x1ee, "guest SVC identity");
            if (switch_current) parent.jit = &other;
            throw VideoCore::ShaderRecoveryError{"synthetic renderer failure"};
        };
        const auto invoke = [&] { if (step) parent.Step(); else parent.Run(); };
        bool caught = false;
        try { invoke(); } catch (const VideoCore::ShaderRecoveryError& error) {
            caught = true;
            Require(std::string{error.what()} == "synthetic renderer failure", "preserved typed payload");
        }
        Require(caught, "immediate C++ delivery");
        Require(first.Regs()[0] == 0 && callbacks == 1, "no post-error guest sentinel or SVC");
        Require(parent.executing_jit == nullptr, "execution scope cleared after rethrow");
        const auto failure = parent.renderer_failure;
        Require(bool(failure), "terminal error retained");
        // A debugger break and a changed current JIT must not strand the error.
        for (bool break_requested : {true, false}) {
            parent.break_flag = break_requested;
            caught = false;
            try { invoke(); } catch (const VideoCore::ShaderRecoveryError&) { caught = true; }
            Require(caught, "pending delivered before break/new guest progress");
            Require(callbacks == 1 && other.Regs()[0] == 0, "no retry guest execution");
            Require(parent.renderer_failure == failure, "first terminal error ownership");
        }
    }
    // CodexAstraLocal: Success still executes the sentinel; generic C++ errors
    // are tested by direct callback invocation, never unwound through JIT code.
    Parent parent;
    Callback env{parent};
    Dynarmic::A32::Jit jit{Dynarmic::A32::UserConfig{&env}};
    parent.jit = &jit;
    env.code_mem = {0xef0001ee, 0xe30a0071, 0xef000099, 0xeafffffe};
    jit.SetCpsr(0x1d0); env.ticks_left = 16;
    env.svc_context.call = [&](u32 swi) { if (swi == 0x99) jit.HaltExecution(); };
    parent.Run();
    Require(jit.Regs()[0] == 41073 && !parent.renderer_failure, "successful guest unchanged");
    env.svc_context.call = [](u32) { throw std::logic_error{"unrelated"}; };
    bool unrelated = false;
    try { env.CallSVC(1); } catch (const std::logic_error&) { unrelated = true; }
    Require(unrelated && !parent.renderer_failure, "unrelated exception not relabeled");
}

int main(int argc, char** argv) {
    Require(argc == 2, "named test route");
    const std::string mode = argv[1];
    if (mode == "baseline") Exercise<baseline::ARM_Dynarmic, baseline::DynarmicUserCallbacks>();
    else if (mode == "omit_halt") Exercise<omit_halt::ARM_Dynarmic, omit_halt::DynarmicUserCallbacks>();
    else if (mode == "mutable_target") Exercise<mutable_target::ARM_Dynarmic, mutable_target::DynarmicUserCallbacks>();
    else if (mode == "omit_delivery") Exercise<omit_delivery::ARM_Dynarmic, omit_delivery::DynarmicUserCallbacks>();
    else if (mode == "omit_pending") Exercise<omit_pending::ARM_Dynarmic, omit_pending::DynarmicUserCallbacks>();
    else Require(false, "unknown route");
    std::printf("PASS real JIT recovery checks=%u\n", checks);
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

// CodexAstraLocal: Only ownership/dependency shells are modeled. Every predicate
// and counter method below is extracted verbatim from the current production file.
#include <atomic>
#include <cstdint>
#include <vector>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
struct DspInterface { virtual ~DspInterface()=default; @DSP@ };
struct DspHle final : DspInterface { @HLE@ };
struct DspLle final : DspInterface {
    struct Impl { const bool multithread; };
    explicit DspLle(bool mode) : impl(std::make_unique<Impl>(mode)) {}
    bool MayWriteMemoryConcurrently() const override;
    std::unique_ptr<Impl> impl;
};
@LLE@
struct KernelSystem {
    @BEGIN_END@
    @PENDING@
    std::atomic<int> pending_async_operations{};
};
// CodexAstraLocal: Only containing types/architecture selection are modeled;
// the CPU/emitter capability methods and selected contract enum are verbatim.
#define UBERHAR_FP_HOST 0
#define CITRA_ARCH(NAME) UBERHAR_FP_HOST
using u8 = std::uint8_t;
@CONTRACT@
struct ARM_Interface { virtual ~ARM_Interface()=default; @CPU_DEFAULT@ };
struct ARM_Dynarmic final : ARM_Interface { @CPU_JIT@ };
struct ShaderEngine { virtual ~ShaderEngine()=default; @SHADER_DEFAULT@ };
struct JitEngine final : ShaderEngine { @SHADER_JIT@ };
// CodexAstraLocal: The production sink capabilities and Pica guard are extracted
// below; modeled aliased memory demonstrates why submission cannot be deferred.
struct RasterizerInterface { virtual ~RasterizerInterface()=default; @RASTER_DEFAULT@ };
struct RasterizerAccelerated final : RasterizerInterface { @RASTER_ACCELERATED@ };
static void Need(bool ok, const char* label);
static bool AllowsDeferredLoad([[maybe_unused]] const RasterizerInterface* rasterizer) {
    [[maybe_unused]] struct { unsigned submission_writes{}; } parallel;
    @RASTER_GUARD@
    return true;
}
static void CheckSubmissionAlias() {
    RasterizerInterface software;
    RasterizerAccelerated accelerated;
    Need(!AllowsDeferredLoad(&software), "immediate submission must refuse");
    Need(AllowsDeferredLoad(&accelerated), "append-only submission must admit");
    // CodexAstraLocal: First-triangle rasterization changes the next vertex's
    // input byte. Serial loading observes 99; unsafe plan-ahead observes 4.
    int later_input = 4;
    const bool deferred = AllowsDeferredLoad(&software);
    const int prepared = deferred ? later_input : 0;
    later_input = 99;
    Need((deferred ? prepared : later_input) == 99, "aliased submission input changed");
}
struct System {
    bool IsHostFpStatusIsolated() const;
    std::vector<ARM_Interface*> cpu_cores;
    ARM_Interface* running_core{};
    bool HasConcurrentGuestMemoryWriters() const;
    std::unique_ptr<DspInterface> dsp_core;
    std::unique_ptr<KernelSystem> kernel;
#ifdef ENABLE_SCRIPTING
    std::unique_ptr<int> rpc_server;
#endif
};
@CORE@
@FP_SYSTEM@
static unsigned checks;
static void Need(bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
// CodexAstraLocal: Execute the actual selector expression with only its global
// singleton lookup rebound to the modeled owner, including empty/null/teardown.
// CodexAstraLocal: Deliberate omission mutants may stop consuming one argument;
// keep those binaries compilable so only the runtime oracle can detect them.
static ParallelVertexContract Select([[maybe_unused]] System& system,
                                    [[maybe_unused]] const ShaderEngine* shader_engine) {
    @SELECTOR@
    return contract;
}
static void CheckFpBoundaries() {
    ARM_Interface unknown_cpu;
    ARM_Dynarmic jit_cpu;
    ShaderEngine unknown_shader;
    JitEngine jit_shader;
    System system;
    const auto full = ParallelVertexContract::FullArithmeticReads;
    const auto selected = ParallelVertexContract::SelectedOutputValues;
    Need(!unknown_cpu.IsHostFpStatusIsolated(), "unknown CPU status must refuse");
    Need(!unknown_shader.SupportsObservableVertexContract(), "unknown shader contract must refuse");
    Need(jit_cpu.IsHostFpStatusIsolated() == bool(UBERHAR_FP_HOST), "CPU host scope changed");
    Need(jit_shader.SupportsObservableVertexContract() == bool(UBERHAR_FP_HOST), "shader host scope changed");
    Need(!system.IsHostFpStatusIsolated(), "empty CPU owners must refuse");
    Need(Select(system, &jit_shader) == full, "empty CPU selector must refuse");
    system.cpu_cores.push_back(&jit_cpu);
    Need(!system.IsHostFpStatusIsolated(), "null running CPU must refuse");
    Need(Select(system, &jit_shader) == full, "null CPU selector must refuse");
    for (ARM_Interface* cpu : {&unknown_cpu, static_cast<ARM_Interface*>(&jit_cpu)}) {
        system.running_core = cpu;
        for (const ShaderEngine* shader : {static_cast<const ShaderEngine*>(&unknown_shader), static_cast<const ShaderEngine*>(&jit_shader)}) {
            const bool both = UBERHAR_FP_HOST && cpu == &jit_cpu && shader == &jit_shader;
            Need(Select(system, shader) == (both ? selected : full), "both actual engines must opt in");
        }
    }
    system.running_core = &jit_cpu;
    system.cpu_cores.clear();
    Need(!system.IsHostFpStatusIsolated(), "cleared CPU owners must refuse");
    Need(Select(system, &jit_shader) == full, "teardown selector must refuse");
}
static void Check() {
    CheckFpBoundaries();
    CheckSubmissionAlias();
    System s;
    Need(s.HasConcurrentGuestMemoryWriters(), "absent runtime must refuse");
    s.kernel=std::make_unique<KernelSystem>();
    Need(s.HasConcurrentGuestMemoryWriters(), "absent DSP must refuse");
    s.dsp_core=std::make_unique<DspInterface>();
    Need(s.HasConcurrentGuestMemoryWriters(), "unknown DSP must refuse");
    s.dsp_core=std::make_unique<DspHle>();
    Need(!s.HasConcurrentGuestMemoryWriters(), "HLE idle should admit");
    s.kernel.reset();
    Need(s.HasConcurrentGuestMemoryWriters(), "absent kernel must refuse");
    s.kernel=std::make_unique<KernelSystem>();
    s.dsp_core=std::make_unique<DspLle>(false);
    Need(!s.HasConcurrentGuestMemoryWriters(), "serial LLE should admit");
    s.dsp_core=std::make_unique<DspLle>(true);
    Need(s.HasConcurrentGuestMemoryWriters(), "parallel LLE must refuse");
    // CodexAstraLocal: A mutable settings surrogate changes after construction;
    // the actual engine query must continue to reflect its immutable mode.
    bool setting=true;
    s.dsp_core=std::make_unique<DspLle>(setting);
    setting=false;
    Need(s.HasConcurrentGuestMemoryWriters(), "live settings hid existing worker");
    s.dsp_core=std::make_unique<DspLle>(setting);
    setting=true;
    Need(!s.HasConcurrentGuestMemoryWriters(), "live settings invented a worker");
    s.dsp_core=std::make_unique<DspHle>();
#ifdef ENABLE_SCRIPTING
    s.rpc_server=std::make_unique<int>(1);
    Need(s.HasConcurrentGuestMemoryWriters(), "actual RPC must refuse");
    s.rpc_server.reset();
    Need(!s.HasConcurrentGuestMemoryWriters(), "removed RPC should admit");
#endif
    // CodexAstraLocal: Two independent jobs and a finished-but-not-delivered job
    // prove the actual atomic count is neither a boolean nor a worker-only span.
    s.kernel->ReportAsyncState(true);
    s.kernel->ReportAsyncState(true);
    Need(s.HasConcurrentGuestMemoryWriters(), "active jobs must refuse");
    s.kernel->ReportAsyncState(false);
    Need(s.HasConcurrentGuestMemoryWriters(), "one remaining job must refuse");
    s.kernel->ReportAsyncState(false);
    Need(!s.HasConcurrentGuestMemoryWriters(), "all callbacks delivered should admit");
    std::promise<void> begin, complete;
    auto allowed=begin.get_future();
    auto done=complete.get_future();
    s.kernel->ReportAsyncState(true);
    int shared_value=0;
    std::jthread worker([&] { allowed.wait(); shared_value=17; complete.set_value(); });
    Need(s.HasConcurrentGuestMemoryWriters(), "queued writer must refuse");
    begin.set_value();
    done.wait();
    Need(s.HasConcurrentGuestMemoryWriters(), "completed worker awaits owner callback");
    worker.join();
    Need(shared_value==17, "actual worker write was not observed");
    s.kernel->ReportAsyncState(false);
    Need(!s.HasConcurrentGuestMemoryWriters(), "delivered completion should admit");
    std::cout << "PASS " << checks << " runtime-query controls\n";
}

// CodexAstraLocal: Expected mutants return a diagnostic failure without aborting
// the process or creating an unrelated host crash dump.
int main() {
    try { Check(); }
    catch (const std::exception& error) {
        std::cerr << "parallel memory FAIL: " << error.what() << '\n';
        return 1;
    }
}

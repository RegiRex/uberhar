// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

// CodexAstraLocal: Only ownership/dependency shells are modeled. Every predicate
// and counter method below is extracted verbatim from the current production file.
#include <atomic>
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
struct System {
    bool HasConcurrentGuestMemoryWriters() const;
    std::unique_ptr<DspInterface> dsp_core;
    std::unique_ptr<KernelSystem> kernel;
#ifdef ENABLE_SCRIPTING
    std::unique_ptr<int> rpc_server;
#endif
};
@CORE@
static unsigned checks;
static void Need(bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
static void Check() {
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

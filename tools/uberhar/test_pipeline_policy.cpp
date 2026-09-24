// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Exercise production selection with successful/failed completions and
// the CPU admission boundaries. Timeout this test in CI to detect stranded waits.
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>
#include "video_core/renderer_vulkan/uberhar_pipeline_policy.h"

struct Pipeline : Common::AsyncHandle {
    explicit Pipeline(Common::AsyncCompletion& completion) : AsyncHandle{false, &completion} {}
    std::atomic_bool failed{};
    bool HasFailed() const {
        return failed.load(std::memory_order_acquire);
    }
    void Fail() {
        failed.store(true, std::memory_order_release);
        MarkDone();
    }
};

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    using namespace Vulkan;
    using Topology = Pica::PipelineRegs::TriangleTopology;
    for (u32 n : {0U, 1U, 2U, 4096U, 4097U, 4098U, 0xffffffffU})
        Check(!CpuBridgeEligible(Topology::List, n), "Unsafe CPU batch admitted");
    for (u32 n : {3U, 6U, 4095U})
        Check(CpuBridgeEligible(Topology::List, n), "Eligible CPU list batch rejected");
    for (auto topology : {Topology::Strip, Topology::Fan, Topology::Shader})
        Check(!CpuBridgeEligible(topology, 6), "Stateful topology admitted to CPU bridge");

    // AstraEH: All precompleted combinations, including forced mode, preserve the
    // preference policy while excluding an already-failed experimental handle.
    for (bool force : {false, true}) {
        Common::AsyncCompletion completion;
        Pipeline preferred{completion}, fallback{completion};
        preferred.MarkDone();
        fallback.MarkDone();
        Check(!PipelineWaitRequired(preferred, &fallback, force), "Ready pair would wait");
        Check(SelectUsablePipeline(completion, preferred, &fallback, force) ==
                  (force ? &fallback : &preferred),
              "Wrong ready-pair winner");
        fallback.Fail();
        Check(SelectUsablePipeline(completion, preferred, &fallback, force) == &preferred,
              "Failed fallback selected");
    }
    {
        Common::AsyncCompletion completion;
        Pipeline preferred{completion}, fallback{completion};
        fallback.MarkDone();
        Check(!PipelineWaitRequired(preferred, &fallback, false), "Ready fallback would wait");
        Check(SelectUsablePipeline(completion, preferred, &fallback, false) == &fallback,
              "Ready fallback not selected");
    }

    // AstraEH: A failed fallback is completed, but callers still wait for the
    // accurate pending pipeline. This is the error case the original waiter lacked.
    for (bool force : {false, true}) {
        Common::AsyncCompletion completion;
        Pipeline preferred{completion}, fallback{completion};
        fallback.Fail();
        Check(PipelineWaitRequired(preferred, &fallback, force), "Failure hid a required wait");
        auto selection = std::async(std::launch::async, [&] {
            return SelectUsablePipeline(completion, preferred, &fallback, force);
        });
        Check(selection.wait_for(std::chrono::milliseconds{2}) == std::future_status::timeout,
              "Returned before a usable pipeline existed");
        preferred.MarkDone();
        Check(selection.get() == &preferred, "Did not recover after fallback failure");
    }
    {
        Common::AsyncCompletion completion;
        Pipeline preferred{completion}, fallback{completion};
        preferred.MarkDone();
        auto selection = std::async(std::launch::async, [&] {
            return SelectUsablePipeline(completion, preferred, &fallback, true);
        });
        Check(selection.wait_for(std::chrono::milliseconds{2}) == std::future_status::timeout,
              "Forced fallback did not wait");
        fallback.Fail();
        Check(selection.get() == &preferred, "Forced fallback failure stranded a waiter");
    }
    std::puts("PASS: CPU admission boundaries and normal/forced failure-aware pipeline selection");
}

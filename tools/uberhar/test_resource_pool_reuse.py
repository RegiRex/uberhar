#!/usr/bin/env python3
"""CodexAstraUlt: Execute production pool reuse against controlled GPU completion.

The real ResourcePool declaration and extracted allocation/reuse methods exercise
completion during refresh, wraparound search and genuinely busy resources. Only
the semaphore and Vulkan allocation operation are modeled; this proves the reuse
defect without claiming a measured driver allocation or Dark Moon memory cause.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("src/video_core/renderer_vulkan/vk_resource_pool.cpp"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/resource-pool-reuse"))
    args = parser.parse_args()
    source = args.source.read_text()
    start = source.index("ResourcePool::ResourcePool(")
    end = source.index("constexpr std::size_t COMMAND_BUFFER_POOL_SIZE", start)
    methods = source[start:end]
    header = r'''
#include <cstdio>
#include <optional>
#include <utility>
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace Vulkan {
// CodexAstraUlt: Completion on Refresh models a timeline query or a fence worker
// advancing between the producer's two reads. No Vulkan driver is required.
class MasterSemaphore {
public:
    u64 known{}, completion{}, current{2};
    unsigned refreshes{};
    u64 KnownGpuTick() const { return known; }
    u64 CurrentTick() const { return current; }
    void Refresh() { ++refreshes; known = completion; }
};
'''
    tests = r'''
// CodexAstraUlt: Seed the resource owner's protected retirement ticks so boundary
// cases do not duplicate production search or allocation arithmetic.
class TestPool final : public ResourcePool {
public:
    TestPool(MasterSemaphore& semaphore, std::size_t step = 1)
        : ResourcePool{&semaphore, step} {}
    using ResourcePool::CommitResource;
    unsigned allocations{};
    void Allocate(std::size_t, std::size_t) override { ++allocations; }
    void Seed(std::vector<u64> values, std::size_t hint = 0) {
        ticks = std::move(values);
        hint_iterator = hint;
    }
    std::size_t Size() const { return ticks.size(); }
    u64 Tick(std::size_t index) const { return ticks[index]; }
};
} // namespace Vulkan

unsigned checks{}, failures{};
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main() {
    using namespace Vulkan;
    // CodexAstraUlt: Regression: completion learned during this very Commit must
    // reuse the existing slot instead of growing permanently retained capacity.
    {
        MasterSemaphore sem;
        sem.completion = 1;
        TestPool pool{sem};
        pool.Seed({1});
        const auto index = pool.CommitResource();
        Require(index == 0, "newly completed slot reused after refresh");
        Require(pool.Size() == 1 && pool.allocations == 0,
                "refresh completion does not allocate another batch");
        Require(sem.refreshes == 1 && sem.known == 1,
                "completion became visible during the call");
        Require(pool.Tick(0) == 2, "reused slot protects the current submission");
    }
    // CodexAstraUlt: The refreshed value must also reach the wrapped search.
    {
        MasterSemaphore sem;
        sem.completion = 1;
        sem.current = 5;
        TestPool pool{sem};
        pool.Seed({1, 4, 4, 4}, 3);
        Require(pool.CommitResource() == 0, "wrapped search sees refreshed completion");
        Require(pool.Size() == 4 && pool.allocations == 0,
                "wrapped free slot prevents allocation");
        Require(pool.Tick(0) == 5 && pool.Tick(3) == 4,
                "reuse preserves busy slots and retags only the selected slot");
    }
    // CodexAstraUlt: Positive controls retain the no-wait fast path and preserve
    // genuinely unfinished resources, including completion one tick too early.
    {
        MasterSemaphore sem;
        sem.known = sem.completion = 1;
        TestPool pool{sem};
        pool.Seed({1});
        Require(pool.CommitResource() == 0, "already completed slot reused");
        Require(sem.refreshes == 0 && pool.allocations == 0,
                "already known completion needs no refresh or allocation");
    }
    for (u64 completion : {u64{0}, u64{1}}) {
        MasterSemaphore sem;
        sem.completion = completion;
        sem.current = 3;
        TestPool pool{sem};
        pool.Seed({2});
        Require(pool.CommitResource() == 1, "unfinished slot is never reused");
        Require(pool.Size() == 2 && pool.allocations == 1,
                "genuinely busy pool still grows");
        Require(pool.Tick(0) == 2 && pool.Tick(1) == 3,
                "growth preserves the pending resource tick");
    }
    // CodexAstraUlt: Allocate one batch and consume its untouched slots before
    // requiring another batch, matching real descriptor/command growth steps.
    {
        MasterSemaphore sem;
        TestPool pool{sem, 4};
        for (std::size_t index = 0; index < 4; ++index) {
            Require(pool.CommitResource() == index, "new batch slots are reusable");
        }
        Require(pool.Size() == 4 && pool.allocations == 1, "one allocation fills one batch");
        Require(pool.CommitResource() == 4, "exhausted batch grows without early reuse");
        Require(pool.Size() == 8 && pool.allocations == 2, "second batch has exact growth");
    }
    std::printf("Resource pool reuse: %u checks, %u failures; production methods, modeled completion\n",
                checks, failures);
    return failures ? 1 : 0;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cpp = args.output.with_suffix(".cpp")
    cpp.write_text(header + methods + tests)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] \
        if args.sanitize else ["-O2"]
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", *flags,
                    "-Isrc", "-Iexternals/vulkan-headers/include", str(cpp),
                    "-o", str(args.output)], check=True)
    subprocess.run([str(args.output)], check=True)


if __name__ == "__main__":
    main()

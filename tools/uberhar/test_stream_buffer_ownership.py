#!/usr/bin/env python3
"""CodexAstraUlt: Fault-inject extracted production stream allocation/cleanup.

The Vulkan device is modeled; actual constructor, CreateBuffers, DestroyBuffers
and destructor bodies run. This checks ownership, not GPU behavior or Dark Moon.
"""
import argparse
import os
from pathlib import Path
import subprocess


def extract(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        return ""  # CodexAstraUlt: Pre-fix source has no shared cleanup helper.
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("src/video_core/renderer_vulkan/vk_stream_buffer.cpp"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/stream-buffer-ownership"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.read_text()
    production = "\n".join(extract(source, signature) for signature in (
        "StreamBuffer::StreamBuffer(", "StreamBuffer::~StreamBuffer()",
        "void StreamBuffer::DestroyBuffers()",
        "void StreamBuffer::CreateBuffers(u64 preferred_size)"))
    fixture = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
using u8=std::uint8_t;
using u32=std::uint32_t;
using u64=std::uint64_t;
constexpr u64 operator""_KiB(unsigned long long value) { return value*1024; }
constexpr u64 VK_NULL_HANDLE=0;
// CodexAstraUlt: A formatting/allocation exception is not a Vulkan retry error.
void TestLog();
#define LOG_INFO(...) TestLog()
#define UNREACHABLE_MSG(...) throw std::runtime_error("allocation exhausted")
void Check(bool value,const char* why) {
    if (!value) throw std::runtime_error(why);
}

// CodexAstraUlt: Model driver failures at each ownership transition, including
// successful allocation followed by bind/map failure and post-map naming failure.
enum class Failure { None, Create, Requirements, Allocation, Bind, Map, Naming, Logging };
struct Driver {
    struct Allocation { u64 bytes{}; bool mapped{}; };
    std::vector<Failure> failures;
    std::unordered_map<u64,u64> buffers;
    std::unordered_map<u64,Allocation> allocations;
    std::vector<u64> attempts;
    u64 next_id=1, attempt{}, allocation_count{}, free_count{}, unmaps{}, destroys{};
    u32 reserve_calls{}, fail_reserve{};
    bool dedicated{}, debug{};
    Failure CurrentFailure() const {
        return attempt<failures.size() ? failures[attempt] : Failure::None;
    }
};
Driver* active_driver{};
void TestLog() {
    if (active_driver->CurrentFailure()==Failure::Logging) throw std::bad_alloc{};
}
namespace vk {
using DeviceSize=u64;
using Buffer=u64;
using DeviceMemory=u64;
using BufferUsageFlags=u32;
using MemoryPropertyFlags=u32;
enum class MemoryPropertyFlagBits : u32 { eHostCoherent=1 };
u32 operator&(u32 left,MemoryPropertyFlagBits right) { return left&static_cast<u32>(right); }
struct SystemError : std::runtime_error { using std::runtime_error::runtime_error; };
struct MemoryType { MemoryPropertyFlags propertyFlags=1; u32 heapIndex{}; };
struct MemoryHeap { u64 size=1024*1024; };
struct PhysicalDeviceMemoryProperties {
    std::array<MemoryType,1> memoryTypes;
    std::array<MemoryHeap,1> memoryHeaps;
};
struct BufferCreateInfo { u64 size; BufferUsageFlags usage; };
struct BufferMemoryRequirementsInfo2 { Buffer buffer; };
struct MemoryRequirements { u64 size{}; };
struct MemoryRequirements2 { MemoryRequirements memoryRequirements; };
struct MemoryDedicatedRequirements { bool prefersDedicatedAllocation{}; };
struct MemoryAllocateInfo { u64 allocationSize{}; u32 memoryTypeIndex{}; };
struct MemoryDedicatedAllocateInfo { Buffer buffer{}; };
template<typename... T> struct StructureChain {
    std::tuple<T...> entries;
    template<typename U> U& get() { return std::get<U>(entries); }
    template<typename U> const U& get() const { return std::get<U>(entries); }
    auto& get() { return std::get<0>(entries); }
};
struct Device {
    Driver* state;
    void Fail(Failure phase) const {
        if (state->CurrentFailure()==phase) throw SystemError("injected failure");
    }
    Buffer createBuffer(BufferCreateInfo info) const {
        active_driver=state;
        state->attempt=state->attempts.size();
        state->attempts.push_back(info.size);
        Fail(Failure::Create);
        const auto handle=state->next_id++;
        state->buffers.emplace(handle,0);
        return handle;
    }
    template<typename... T> StructureChain<T...> getBufferMemoryRequirements2(
            BufferMemoryRequirementsInfo2) const {
        Fail(Failure::Requirements);
        StructureChain<T...> chain;
        chain.template get<MemoryRequirements2>().memoryRequirements.size=state->attempts.back()+4096;
        chain.template get<MemoryDedicatedRequirements>().prefersDedicatedAllocation=state->dedicated;
        return chain;
    }
    DeviceMemory allocateMemory(MemoryAllocateInfo info) const {
        Fail(Failure::Allocation);
        const auto handle=state->next_id++;
        state->allocations.emplace(handle,Driver::Allocation{info.allocationSize,false});
        ++state->allocation_count;
        return handle;
    }
    void bindBufferMemory(Buffer buffer,DeviceMemory memory,u64) const {
        Fail(Failure::Bind);
        state->buffers.at(buffer)=memory;
    }
    void* mapMemory(DeviceMemory memory,u64,u64 bytes) const {
        Fail(Failure::Map);
        auto& allocation=state->allocations.at(memory);
        Check(allocation.bytes==bytes,"Mapped size differs from allocation requirement");
        allocation.mapped=true;
        static u8 data;
        return &data;
    }
    void unmapMemory(DeviceMemory memory) const {
        auto& allocation=state->allocations.at(memory);
        Check(allocation.mapped,"Unmapped unsuccessful or already unmapped allocation");
        allocation.mapped=false;
        ++state->unmaps;
    }
    void destroyBuffer(Buffer buffer) const {
        Check(state->buffers.erase(buffer)==1,"Double or null buffer destruction");
        ++state->destroys;
    }
    void freeMemory(DeviceMemory memory) const {
        for (auto [buffer,bound] : state->buffers)
            Check(bound!=memory,"Freed memory before destroying its bound buffer");
        Check(!state->allocations.at(memory).mapped,"Freed memory without ordered unmap");
        Check(state->allocations.erase(memory)==1,"Double or null memory release");
        ++state->free_count;
    }
};
const char* to_string(MemoryPropertyFlags) { return "test"; }
}
namespace Vulkan {
constexpr u64 WATCHES_INITIAL_RESERVE=0x4000;
struct Scheduler {};
Scheduler fixture_scheduler;
enum class BufferType { Stream };
const char* BufferTypeName(BufferType) { return "Stream"; }
u32 GetMemoryType(const vk::PhysicalDeviceMemoryProperties&,BufferType) { return 0; }
struct Instance {
    Driver& driver;
    mutable u64 current_bytes{}, peak_bytes{}, allocations{}, frees{}, failures{};
    vk::Device GetDevice() const { return {&driver}; }
    struct Physical {
        vk::PhysicalDeviceMemoryProperties getMemoryProperties() const { return {}; }
    };
    Physical GetPhysicalDevice() const { return {}; }
    bool HasDebuggingToolAttached() const { return driver.debug; }
    void RecordRawStreamAllocation(u64 bytes) const noexcept {
        current_bytes+=bytes; peak_bytes=std::max(peak_bytes,current_bytes); ++allocations;
    }
    void RecordRawStreamFree(u64 bytes) const noexcept { current_bytes-=bytes; ++frees; }
    void RecordRawStreamFailure() const noexcept { ++failures; }
};
template<typename... T> void SetObjectName(vk::Device device,T&&...) {
    device.Fail(Failure::Naming);
}
struct StreamBuffer {
    const Instance& instance;
    Scheduler& scheduler;
    vk::Device device;
    vk::Buffer buffer{};
    vk::DeviceMemory memory{};
    u8* mapped{};
    u64 stream_buffer_size{}, allocation_bytes{};
    vk::BufferUsageFlags usage{};
    BufferType type=BufferType::Stream;
    bool is_coherent{};
    struct Watch {};
    std::vector<Watch> current_watches,previous_watches;
    // CodexAstraUlt: Direct-method tests avoid construction; separate cases below
    // invoke the exact production constructor, including its watch reserves.
    explicit StreamBuffer(const Instance& instance_)
        : instance{instance_},scheduler{fixture_scheduler},device{instance.GetDevice()} {}
    StreamBuffer(const Instance& instance_,Scheduler& scheduler_,vk::BufferUsageFlags usage_,
                 u64 size,BufferType type_);
    ~StreamBuffer();
    void CreateBuffers(u64 preferred_size);
    void DestroyBuffers() noexcept;
    void ReserveWatches(std::vector<Watch>&,std::size_t) {
        auto& driver=instance.driver;
        if (++driver.reserve_calls==driver.fail_reserve) throw std::bad_alloc{};
    }
};
'''
    tests = r'''
} // namespace Vulkan

// CodexAstraUlt: Validate actual handle ownership before checking telemetry, so
// pre-fix production code fails for the leak rather than merely missing counters.
void Scenario(Failure failure,bool dedicated) {
    Driver driver;
    driver.failures={failure}; driver.dedicated=dedicated; driver.debug=failure==Failure::Naming;
    Vulkan::Instance instance{driver};
    {
        Vulkan::StreamBuffer buffer{instance};
        buffer.CreateBuffers(16_KiB);
        const u64 expected_attempts=failure==Failure::None ? 1 : 2;
        Check(driver.attempts.size()==expected_attempts,"Unexpected allocation retry count");
        Check(driver.attempts.front()==16_KiB && driver.attempts.back()==(16_KiB>>(expected_attempts-1)),
              "Retry no longer halves requested capacity");
        Check(driver.buffers.size()==1 && driver.allocations.size()==1,
              "Failed attempt retained buffer or device-memory ownership");
        const u64 actual_bytes=driver.attempts.back()+4096;
        Check(instance.current_bytes==actual_bytes && buffer.allocation_bytes==actual_bytes,
              "Raw stream ledger used requested capacity instead of actual requirement bytes");
        Check(instance.allocations==driver.allocation_count && instance.frees==driver.free_count &&
              instance.failures==expected_attempts-1,"Allocation/free/failure ledger mismatch");
        Check(instance.peak_bytes<=20_KiB,"Failed attempt overlapped the next allocation");
    }
    Check(driver.buffers.empty() && driver.allocations.empty(),"Teardown leaked owned resources");
    Check(instance.current_bytes==0 && instance.allocations==instance.frees,
          "Teardown left raw stream accounting outstanding");
}
int main() {
    u32 scenarios=0;
    // CodexAstraUlt: Bind/map first ensure archived production code exposes the
    // original memory leak even though it predates allocation telemetry.
    for (auto failure : {Failure::Bind,Failure::Map,Failure::None,Failure::Create,
                         Failure::Requirements,Failure::Allocation,Failure::Naming}) {
        for (bool dedicated : {false,true}) { Scenario(failure,dedicated); ++scenarios; }
    }
    for (auto failure : {Failure::Allocation,Failure::Bind,Failure::Map}) {
        Driver driver;
        driver.failures={failure,failure};
        Vulkan::Instance instance{driver};
        {
            Vulkan::StreamBuffer buffer{instance};
            bool exhausted=false;
            try { buffer.CreateBuffers(16_KiB); }
            catch (const std::runtime_error& error) { exhausted=std::string_view{error.what()}=="allocation exhausted"; }
            Check(exhausted && driver.attempts==std::vector<u64>{16_KiB,8_KiB},
                  "Minimum size/exhaustion behavior changed");
            Check(driver.buffers.empty() && driver.allocations.empty() && instance.current_bytes==0,
                  "Exhaustion retained a partial allocation");
            Check(instance.failures==2 && instance.allocations==instance.frees,
                  "Exhausted attempts were not fully accounted");
        }
        ++scenarios;
    }
    // CodexAstraUlt: A constructor that throws never calls its destructor. Cover
    // both watch-reserve failures and a non-Vulkan exception after mapping.
    for (u32 fail_reserve : {0U,1U,2U}) {
        Driver driver;
        driver.fail_reserve=fail_reserve;
        driver.failures={fail_reserve ? Failure::None : Failure::Logging};
        Vulkan::Instance instance{driver};
        bool failed=false;
        try {
            Vulkan::StreamBuffer buffer{instance,Vulkan::fixture_scheduler,0,16_KiB,
                                        Vulkan::BufferType::Stream};
        } catch (const std::bad_alloc&) { failed=true; }
        Check(failed,"Constructor lost the original non-Vulkan allocation exception");
        Check(driver.attempts.size()==1 && driver.reserve_calls==fail_reserve,
              "Non-Vulkan failure unexpectedly retried or reached later reserves");
        Check(driver.buffers.empty() && driver.allocations.empty() && instance.current_bytes==0,
              "Failed constructor leaked raw Vulkan ownership");
        Check(instance.allocations==1 && instance.frees==1 && instance.failures==0,
              "Failed constructor release or Vulkan retry accounting changed");
        ++scenarios;
    }
    {
        // CodexAstraUlt: Successful construction still performs both reserves and
        // leaves exactly one owned allocation for the normal destructor.
        Driver driver;
        Vulkan::Instance instance{driver};
        {
            Vulkan::StreamBuffer buffer{instance,Vulkan::fixture_scheduler,0,16_KiB,
                                        Vulkan::BufferType::Stream};
            Check(driver.reserve_calls==2 && driver.allocations.size()==1 &&
                  instance.current_bytes==20_KiB,"Successful constructor changed ownership");
        }
        Check(driver.allocations.empty() && instance.current_bytes==0,
              "Successfully constructed buffer leaked during destruction");
        ++scenarios;
    }
    std::printf("PASS: %u production stream ownership scenarios; retry halving, actual bytes, "
                "partial/constructor failures, cleanup ordering and exhaustion retained\n",scenarios);
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cpp=args.output.with_suffix(".cpp")
    cpp.write_text(fixture+production+tests)
    flags=["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", *flags,
                    str(cpp), "-o", str(args.output)], check=True)
    subprocess.run([str(args.output)], check=True)


if __name__ == "__main__":
    main()

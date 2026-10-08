// CodexAstraLocal: This synthetic regression executes the unmodified extracted optional
// cache function on the actual ThreadWorker/AsyncHandle; compiler/GPU/log endpoints
// are controlled. It performs no Vulkan calls, device work, or performance timings.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "common/async_handle.h"
#include "common/logging/log.h"
#include "common/thread_worker.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/renderer_vulkan/uberhar_fragment_policy.h"
#include "video_core/renderer_vulkan/uberhar_shader_compile_policy.h"

using Pica::Shader::FSConfig;
using Pica::Shader::Profile;
using Pica::Shader::UserConfig;
namespace Probe {
unsigned checks{};
std::atomic<unsigned> generate_calls{}, compile_calls{}, module_calls{}, log_calls{};
std::atomic<unsigned> shader_births{}, shader_deaths{};
std::atomic<int> failure{};
std::atomic<bool> logger_throws{}, optimizer_disabled{};
std::mutex mutex;
std::condition_variable condition;
bool pause{}, entered{}, released{};
std::optional<FSConfig> observed_config;
std::optional<Profile> observed_profile;
thread_local long fail_allocation_after = -1;
thread_local unsigned intercepted_allocations{};

// CodexAstraLocal: Every assertion runs on the owner thread after synchronization.
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void Reset() {
    failure = 0; logger_throws = false; generate_calls = 0; compile_calls = 0;
    module_calls = 0; log_calls = 0; optimizer_disabled = true;
    std::lock_guard lock(mutex);
    pause = entered = released = false;
    observed_config.reset(); observed_profile.reset();
}
void Release() {
    std::lock_guard lock(mutex);
    released = true;
    condition.notify_all();
}
void AwaitEntry() {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(3), [] { return entered; }),
            "real worker did not reach generator within bounded wait");
}
void Log() {
    ++log_calls;
    if (logger_throws) throw 831;
}
}

// CodexAstraLocal: A main-thread-only allocation countdown injects genuine
// std::bad_alloc into entry/map/UniqueFunction allocation; worker allocations
// are untouched. This tests optional admission, not general process OOM recovery.
void* operator new(std::size_t size) {
    if (Probe::fail_allocation_after >= 0) {
        ++Probe::intercepted_allocations;
        if (Probe::fail_allocation_after-- == 0) {
            Probe::fail_allocation_after = -1;
            throw std::bad_alloc();
        }
    }
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

// CodexAstraLocal: Naming is the only platform thread service replaced. Queue,
// jthread dispatch, work accounting, cancellation and draining remain production.
namespace Common { void SetCurrentThreadName(const char*) {} }
// CodexAstraLocal: Incidental assertion/log linkage from real register config
// construction remains an explicit failure; it is not a second logging backend.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*, fmt::string_view,
                       const fmt::format_args&) { throw std::runtime_error("unexpected config log"); }
}
#undef LOG_ERROR
#define LOG_ERROR(...) ::Probe::Log()

namespace Pica::Shader::Generator::GLSL {
// CodexAstraLocal: Record the immutable generator inputs before returning a tiny
// sentinel source. The independent selection/ABI suites exercise real generation.
std::string GenerateFragmentShader(const FSConfig& config, const UserConfig&,
                                  const Profile& profile) {
    ++Probe::generate_calls;
    {
        std::unique_lock lock(Probe::mutex);
        Probe::entered = true;
        Probe::condition.notify_all();
        if (Probe::pause && !Probe::condition.wait_for(lock, std::chrono::seconds(3),
                                                     [] { return Probe::released; }))
            throw std::runtime_error("test generator release deadline");
        Probe::observed_config = config;
        Probe::observed_profile = profile;
    }
    if (Probe::failure == 5) throw std::runtime_error("generator standard");
    if (Probe::failure == 6) throw 612;
    return "private shader sentinel";
}
}

namespace Vulkan {
namespace GLSL = Pica::Shader::Generator::GLSL;
class Instance { public: vk::Device GetDevice() const { return {}; } };

// CodexAstraLocal: Extract the real Shader completion/failed-state declaration;
// fake nonzero handles are never submitted or destroyed through Vulkan.
#include "shader.inc"
Shader::Shader(const Instance&) { ++Probe::shader_births; }
Shader::~Shader() { ++Probe::shader_deaths; }

// CodexAstraLocal: Injection before queue admission complements real allocator
// failures. All accepted closures run on the real production worker thread.
struct Queue {
    Common::ThreadWorker worker{1, "optional-fs-test"};
    bool throw_queue{};
    unsigned attempts{}, accepted{};
    template <typename F> void QueueWork(F&& function) {
        ++attempts;
        if (throw_queue) throw std::bad_alloc();
        worker.QueueWork(std::forward<F>(function));
        ++accepted;
    }
    void WaitForRequests() { worker.WaitForRequests(); }
};
struct Parent { Instance instance; Profile profile{}; Queue shader_workers; };
class ShaderDiskCache {
public:
    Parent& parent;
    // CodexAstraLocal: Key, owned entry, demand gate, counters and snapshots below
    // are extracted exactly from the candidate header, not reimplemented.
#include "members.inc"
    explicit ShaderDiskCache(Parent& owner) : parent(owner) {}
    std::optional<std::pair<u64, Shader* const>> UseReadyFragmentShader(
        const FSConfig&, const UserConfig&, bool allow_build = true);
};

// CodexAstraLocal: Fail at each external optional compilation stage, including
// empty/null outputs. No shader compiler or Vulkan driver is invoked here.
std::vector<u32> CompileGLSL(const std::string&, vk::ShaderStageFlagBits, const char*,
                            bool disabled) {
    ++Probe::compile_calls; Probe::optimizer_disabled = disabled;
    if (Probe::failure == 1) throw std::runtime_error("GLSL standard");
    if (Probe::failure == 2) throw 231;
    if (Probe::failure == 3) return {};
    return {0x07230203};
}
vk::ShaderModule CompileSPV(const std::vector<u32>&, vk::Device) {
    ++Probe::module_calls;
    if (Probe::failure == 7) throw std::runtime_error("SPV standard");
    if (Probe::failure == 8) throw 812;
    if (Probe::failure == 4) return {};
    return vk::ShaderModule{reinterpret_cast<VkShaderModule>(std::uintptr_t{0x1234})};
}
#ifndef EXTRACTED_FUNCTION
#define EXTRACTED_FUNCTION "normal.inc"
#endif
#include EXTRACTED_FUNCTION
}

using namespace Vulkan;
using Probe::Require;

// CodexAstraLocal: The fixture drains before owned entries die, and unblocks any
// held generator even on an assertion exception. Process timeout bounds defects.
struct Fixture {
    Parent parent;
    ShaderDiskCache cache{parent};
    Fixture() { parent.profile.is_vulkan = true; parent.profile.vk_disable_spirv_optimizer = true; }
    ~Fixture() { Probe::Release(); parent.shader_workers.WaitForRequests(); }
};
// CodexAstraLocal: Synthetic raw TEV values distinguish exact cache keys without guest code.
FSConfig Config(unsigned value = 0) {
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    FSConfig result{regs};
    result.texture.tev_stages[0].sources_raw = value;
    return result;
}
// CodexAstraLocal: Repeat only the real admission call; no test-owned warmup policy.
void Demand(Fixture& fixture, const FSConfig& config, unsigned count = 16,
            bool allow_build = true) {
    for (unsigned i = 0; i < count; ++i)
        (void)fixture.cache.UseReadyFragmentShader(config, {}, allow_build);
}

// CodexAstraLocal: A paused real task proves pending lookup is nonblocking and
// queued config/profile are owned snapshots despite producer mutation/rehash.
void SnapshotAndPending() {
    Probe::Reset(); Fixture f; auto config = Config(4); const auto original = config;
    const auto profile = f.parent.profile;
    { std::lock_guard lock(Probe::mutex); Probe::pause = true; }
    Demand(f, config, 15);
    Require(f.parent.shader_workers.accepted == 0 && f.cache.ready_fragments.empty(),
            "cold demand must not queue before16");
    Demand(f, config, 1); Probe::AwaitEntry();
    auto* entry = f.cache.ready_fragments.at(config.Hash()).get();
    auto* shader = &entry->shader;
    Require(!shader->IsDone(), "held optional task must remain pending");
    for (unsigned n = 0; n < 10; ++n)
        Require(!f.cache.UseReadyFragmentShader(config, {}, false), "pending lookup must fall back");
    auto other = Config(8); Demand(f, other);
    Require(f.parent.shader_workers.accepted == 1 && f.cache.ready_fragments.size() == 1,
            "one optional module task pending");
    f.cache.ready_fragments.reserve(256);
    config.texture.tev_stages[0].sources_raw ^= 1;
    f.parent.profile.enable_accurate_mul = true;
    Probe::Release(); f.parent.shader_workers.WaitForRequests();
    Require(shader->IsDone() && !shader->HasFailed() && shader->Handle(), "successful module published");
    Require(*Probe::observed_config == original && *Probe::observed_profile == profile,
            "actual worker must consume immutable config/profile");
    Require(!Probe::optimizer_disabled, "optional optimizer must remain enabled");
    Require(!f.cache.UseReadyFragmentShader(original, {}, false), "profile mismatch must reject");
    f.parent.profile = profile;
    auto result = f.cache.UseReadyFragmentShader(original, {}, false);
    Require(result && result->second == shader && result->first == original.Hash(),
            "ready owner identity survives map rehash and lookup-only");
    entry->key.config.texture.tev_stages[0].sources_raw ^= 2;
    Require(!f.cache.UseReadyFragmentShader(original, {}, false), "hash bucket cannot hide unequal config");
    entry->key.config = original;
    UserConfig custom{}; custom.raw = 1;
    Require(!f.cache.UseReadyFragmentShader(original, custom), "noncacheable user cannot borrow ready owner");
}

// CodexAstraLocal: Lookup-only must not silently warm demand. Module capacity is
// shared and exact:128 retained objects, terminal failures included, no eviction.
void LookupAndCapacity() {
    Probe::Reset(); Fixture f; auto config = Config(12);
    for (unsigned n = 0; n < 32; ++n)
        Require(!f.cache.UseReadyFragmentShader(config, {}, false), "lookup-only absent must fall back");
    Require(f.cache.ready_fragments.empty() && f.parent.shader_workers.accepted == 0,
            "lookup-only must allocate/queue nothing");
    Demand(f, config, 15);
    Require(f.parent.shader_workers.accepted == 0, "lookup-only must not advance demand");
    Demand(f, config, 1); f.parent.shader_workers.WaitForRequests();
    auto* original = f.cache.ready_fragments.at(config.Hash()).get();
    for (unsigned n = 1; f.cache.ready_fragments.size() < 128; ++n) {
        auto c = Config(100 + n);
        auto entry = std::make_unique<ShaderDiskCache::ReadyFragmentEntry>(
            f.parent.instance, ShaderDiskCache::ReadyFragmentKey{c, f.parent.profile});
        entry->shader.MarkFailed();
        f.cache.ready_fragments.emplace(c.Hash(), std::move(entry));
    }
    auto missing = Config(999); Demand(f, missing, 32);
    Require(f.cache.ready_fragments.size() == 128 && f.parent.shader_workers.accepted == 1,
            "shared module128 cap includes failed owners");
    auto hit = f.cache.UseReadyFragmentShader(config, {}, false);
    Require(hit && hit->second == &original->shader, "full module bank still permits exact ready hit");
    original->shader.module = nullptr;
    Require(!f.cache.UseReadyFragmentShader(config, {}), "done null module cannot be selected");
    auto failed = Config(101);
    const auto attempts = f.parent.shader_workers.attempts;
    Demand(f, failed, 20);
    Require(f.parent.shader_workers.attempts == attempts, "failed owner must never retry");
}

// CodexAstraLocal: Unknown/compiler/logger exceptions must return through the
// real worker, increment its completion accounting and leave terminal failed entry.
void Failure(unsigned kind, bool logging_failure) {
    Probe::Reset(); Fixture f; auto config = Config(20);
    Probe::failure = kind; Probe::logger_throws = logging_failure;
    Demand(f, config); f.parent.shader_workers.WaitForRequests();
    auto* entry = f.cache.ready_fragments.at(config.Hash()).get();
    Require(entry->shader.IsDone() && entry->shader.HasFailed(), "compile failure terminal failed+done");
    // CodexAstraLocal: Prove the selected generator/GLSL/SPV failure seam really ran;
    // a failure in an earlier unrelated endpoint cannot satisfy this case.
    const bool generator_failure = kind == 5 || kind == 6;
    const bool module_stage_reached = kind == 4 || kind == 7 || kind == 8;
    Require(Probe::generate_calls == 1 && Probe::compile_calls == !generator_failure &&
            Probe::module_calls == module_stage_reached, "failure reached its intended compiler stage");
    Require(f.cache.ready_fragment_failures == 1 && f.cache.ready_fragment_builds == 1,
            "one admitted failing job accounted exactly once");
    const auto attempts = f.parent.shader_workers.attempts;
    for (unsigned n = 0; n < 20; ++n)
        Require(!f.cache.UseReadyFragmentShader(config, {}), "failed optional owner falls back");
    Require(f.parent.shader_workers.attempts == attempts &&
            f.cache.ready_fragments.at(config.Hash()).get() == entry,
            "failed owner retained without retry");
    Require(Probe::log_calls == 1, "first failure must retain one bounded diagnostic attempt");
}
// CodexAstraLocal: A rejected queue owns a failed entry but must have no accepted task.
void QueueFailure() {
    Probe::Reset(); Fixture f; auto config = Config(30); f.parent.shader_workers.throw_queue = true;
    Demand(f, config);
    auto* entry = f.cache.ready_fragments.at(config.Hash()).get();
    Require(entry->shader.IsDone() && entry->shader.HasFailed(), "queue failure terminal failed+done");
    Require(f.parent.shader_workers.accepted == 0 && Probe::generate_calls == 0 &&
            f.cache.ready_fragment_builds == 0 && f.cache.ready_fragment_failures == 1,
            "queue failure must create no ghost task or build");
    Demand(f, config, 20);
    Require(f.parent.shader_workers.attempts == 1, "queue failure must not retry");
}

// CodexAstraLocal: Allocation failures before owner insertion may propagate to
// the CPU caller's existing catch; after insertion, queue allocation must publish
// a terminal owner. No dangling warming pointer or leaked Shader is permitted.
void Allocations() {
    unsigned escaped_before_owner{}, contained_after_owner{}, success{};
    for (long countdown = 0; countdown < 8; ++countdown) {
        Probe::Reset(); Fixture f; auto config = Config(40);
        Demand(f, config, 15); Probe::intercepted_allocations = 0;
        Probe::fail_allocation_after = countdown;
        bool escaped = false;
        try { (void)f.cache.UseReadyFragmentShader(config, {}); }
        catch (const std::bad_alloc&) { escaped = true; }
        Probe::fail_allocation_after = -1;
        // CodexAstraLocal: Every armed case must observe real owner-thread allocation;
        // the countdown cannot silently become an unused fault-injection control.
        Require(Probe::intercepted_allocations > 0 &&
                Probe::intercepted_allocations <= static_cast<unsigned>(countdown + 1),
                "allocator countdown observed bounded actual allocations");
        f.parent.shader_workers.WaitForRequests();
        if (escaped) {
            ++escaped_before_owner;
            Require(f.cache.ready_fragments.empty() && !f.cache.warming_ready_fragment &&
                    f.parent.shader_workers.accepted == 0,
                    "pre-owner allocation failure leaves no pending pointer/task");
        } else {
            Require(f.cache.ready_fragments.size() == 1, "contained/successful admission owns one entry");
            auto* entry = f.cache.ready_fragments.begin()->second.get();
            Require(entry->shader.IsDone(), "retained allocation outcome must be terminal");
            if (entry->shader.HasFailed()) ++contained_after_owner; else ++success;
        }
    }
    Require(escaped_before_owner > 0 && contained_after_owner > 0 && success > 0,
            "allocator sweep must exercise preowner, queued-allocation and success cases");
}

// CodexAstraLocal: Twelve distinct failing modules prove one shared eight-detail
// budget while every failed closure still drains and publishes its terminal state.
void BoundedLogging() {
    Probe::Reset(); Fixture f; Probe::failure = 2; Probe::logger_throws = true;
    for (unsigned n = 0; n < 12; ++n) {
        auto config = Config(2000 + n); Demand(f, config);
        f.parent.shader_workers.WaitForRequests();
        Require(f.cache.ready_fragments.at(config.Hash())->shader.HasFailed(), "bounded failure recorded");
    }
    Require(Probe::log_calls == 8 && f.cache.ready_fragment_failures == 12 &&
            f.cache.ready_fragment_builds == 12, "failure diagnostic budget eight, all jobs accounted");
}

// CodexAstraLocal: Select one bounded mutation witness or the fixed complete positive corpus.
int main(int argc, char** argv) {
    try {
        const std::string which = argc > 1 ? argv[1] : "all";
        if (which == "opaque") Failure(2, false);
        else if (which == "logger") Failure(1, true);
        else if (which == "queue") QueueFailure();
        else if (which == "lookup") LookupAndCapacity();
        else if (which == "profile") SnapshotAndPending();
        else {
            SnapshotAndPending(); LookupAndCapacity(); QueueFailure(); Allocations(); BoundedLogging();
            for (unsigned failure = 1; failure <= 8; ++failure)
                for (bool logger : {false, true}) Failure(failure, logger);
        }
        Require(Probe::shader_births == Probe::shader_deaths, "all shader owners destroyed after drain");
        std::cout << "PASS checks=" << Probe::checks << " actual_thread_worker=true gpu_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        Probe::fail_allocation_after = -1;
        std::cerr << "FAIL " << error.what() << " checks=" << Probe::checks << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL unexpected owner-thread exception\n";
        return 2;
    }
}

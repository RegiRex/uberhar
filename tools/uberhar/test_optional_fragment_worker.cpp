// CodexAstraLocal: This synthetic regression executes the unmodified extracted optional
// cache function on the actual ThreadWorker/AsyncHandle; compiler/GPU/log endpoints
// are controlled. It performs no Vulkan calls, device work, or performance timings.
#include <atomic>
#include <functional>
#include <span>
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
#include "video_core/renderer_vulkan/uberhar_static_tev_policy.h"
#include "video_core/renderer_vulkan/uberhar_compile_budget.h"
#include "video_core/renderer_vulkan/uberhar_optional_failure_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/uberhar_shader_compile_policy.h"

using Pica::Shader::FSConfig;
using Pica::Shader::Profile;
using Pica::Shader::UserConfig;
namespace Probe {
unsigned checks{};
std::atomic<unsigned> generate_calls{}, compile_calls{}, module_calls{}, log_calls{};
// CodexAstraLocal: Distinct recording endpoints make generator selection observable.
std::atomic<unsigned> glsl_calls{}, spirv_calls{};
std::atomic<unsigned> shader_births{}, shader_deaths{};
std::atomic<int> failure{};
std::atomic<bool> logger_throws{}, optimizer_disabled{};
std::mutex mutex;
std::condition_variable condition;
bool pause{}, entered{}, released{};
std::optional<FSConfig> observed_config;
std::optional<Profile> observed_profile;
#ifdef UBERHAR_STATIC_TEV_WORKER
std::optional<Pica::Shader::Generator::GLSL::StaticTevPlan> observed_plan;
#endif
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
    glsl_calls = 0; spirv_calls = 0;
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

namespace Probe {
// CodexAstraLocal: Record the immutable generator inputs before returning a tiny
// sentinel source. Independent ABI suites exercise GLSL generation and its
// compiled SPIR-V, not direct SPIRV::GenerateFragmentShader module/link/pixels.
void RecordGeneration(const FSConfig& config, const Profile& profile) {
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
}
}

namespace Pica::Shader::Generator::GLSL {
std::string GenerateFragmentShader(const FSConfig& config, const UserConfig&,
                                  const Profile& profile) {
    ++Probe::glsl_calls;
    Probe::RecordGeneration(config, profile);
    return "private shader sentinel";
}
}

// CodexAstraLocal: This is a distinct direct-generator endpoint, not a call to
// GLSL. Empty/throwing generation must retain the same terminal fallback policy.
namespace Pica::Shader::Generator::SPIRV {
std::vector<u32> GenerateFragmentShader(const FSConfig& config, const Profile& profile) {
    ++Probe::spirv_calls;
    Probe::RecordGeneration(config, profile);
    if (Probe::failure == 9) return {};
    return {0x07230203, 0x535056};
}
}

#ifdef UBERHAR_STATIC_TEV_WORKER
namespace Pica::Shader::Generator::GLSL {
// CodexAstraLocal: Actual support guards precede a recording generator endpoint;
// pixel/depth parity belongs to the separate real-generator execution gate.
#include "static_support.inc"
std::string GenerateStaticTevFragmentShader(const FSConfig& family,const UserConfig& user,
        const Profile& profile,const StaticTevPlan& plan) {
    { std::lock_guard lock(Probe::mutex); Probe::observed_plan=plan; }
    return GenerateFragmentShader(family,user,profile);
}
}
#endif

namespace Vulkan {
namespace GLSL = Pica::Shader::Generator::GLSL;
namespace SPIRV = Pica::Shader::Generator::SPIRV;
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
// CodexAstraLocal: The existing real worker is now the shared optional lane;
// keep the original failure controls attached to that same queue instance.
struct Parent {
    Instance instance; Profile profile{}; Queue shader_workers;
    Queue* ready_vertex_worker=&shader_workers;
    Shader* warming_ready_vertex{};
    bool allow_static_cpu_tev=true;
    // CodexAstraLocal: No pipeline is modeled here; explicit references can
    // still hold a retry owner until the production reuse guard releases it.
    const Shader* referenced_shader{};
    bool OptionalShaderReferenced(const Shader* shader) const { return shader == referenced_shader; }
};
class ShaderDiskCache {
public:
    Parent& parent;
    // CodexAstraLocal: Key, owned entry, demand gate, counters and snapshots below
    // are extracted exactly from the candidate header, not reimplemented.
#include "members.inc"
    explicit ShaderDiskCache(Parent& owner) : parent(owner) {}
    // CodexAstraLocal: This worker fixture has no disk backend. Real serialization
    // is extracted; separate codec tests cover persistence and warm records.
    bool WarmDemand(u64, bool) const;
    void ReportOptionalFailure(const char*, const char*, unsigned) noexcept;
    std::vector<u32> LoadOrCompileOptional(std::span<const u32>, bool,
                                         const std::function<std::vector<u32>()>& compile) {
        return compile();
    }
    std::optional<std::pair<u64, Shader* const>> UseReadyFragmentShader(
        const FSConfig&, const UserConfig&, bool allow_build = true);
#ifdef UBERHAR_STATIC_TEV_WORKER
    std::optional<std::pair<u64, Shader* const>> UseStaticTevFragmentShader(
        const FSConfig&,const GLSL::StaticTevPlan&,const UserConfig&,bool allow_build=true);
#endif
};

// CodexAstraLocal: Fail at each external optional compilation stage, including
// empty/null outputs. No shader compiler or Vulkan driver is invoked here.
std::vector<u32> CompileGLSL(std::string_view, vk::ShaderStageFlagBits, std::string_view,
                            bool disabled) {
    ++Probe::compile_calls; Probe::optimizer_disabled = disabled;
    if (Probe::failure == 1) throw std::runtime_error("GLSL standard");
    if (Probe::failure == 2) throw 231;
    if (Probe::failure == 3) return {};
    return {0x07230203};
}
vk::ShaderModule CompileOptionalSPV(std::span<const u32>, vk::Device) {
    ++Probe::module_calls;
    if (Probe::failure == 7) throw std::runtime_error("SPV standard");
    if (Probe::failure == 8) throw 812;
    if (Probe::failure == 4) return {};
    // CodexAstraLocal: Typed endpoints exercise policy classification without
    // fabricating real Vulkan resource exhaustion or driver device loss.
    if (Probe::failure == 10) throw OptionalCompileError(OptionalCompileFailure::Resource, "resource");
    if (Probe::failure == 11) throw OptionalCompileError(OptionalCompileFailure::DeviceLost, "device");
    if (Probe::failure == 12) throw OptionalCompileError(OptionalCompileFailure::Source, "source");
    if (Probe::failure == 13) throw std::bad_alloc();
    return vk::ShaderModule{reinterpret_cast<VkShaderModule>(std::uintptr_t{0x1234})};
}
// CodexAstraLocal: Identity preparation stays production code even with disk I/O modeled.
#include "identity.inc"
#include "helpers.inc"
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
void SnapshotAndPending(bool direct = false) {
    Probe::Reset(); Fixture f; auto config = Config(4); const auto original = config;
    // CodexAstraLocal: Hold either generator on the real worker and mutate its
    // producer settings; both must finish from the immutable queued snapshot.
    f.parent.profile.vk_use_spirv_generator = direct;
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
    // CodexAstraLocal: The queued generator must not follow a later UI/profile change.
    f.parent.profile.vk_use_spirv_generator = !direct;
    Probe::Release(); f.parent.shader_workers.WaitForRequests();
    Require(shader->IsDone() && !shader->HasFailed() && shader->Handle(), "successful module published");
    Require(*Probe::observed_config == original && *Probe::observed_profile == profile,
            "actual worker must consume immutable config/profile");
    Require(direct ? Probe::compile_calls == 0 && Probe::spirv_calls == 1
                   : !Probe::optimizer_disabled,
            "optional optimizer applies only to GLSL; direct SPIRV bypasses frontend");
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

// CodexAstraLocal: Exercise the existing public choice and ordinary compatibility
// guard independently, without changing ready admission or testing driver pixels.
void GeneratorChoice(bool requested, bool incompatible) {
    Probe::Reset(); Fixture f; auto config = Config(80);
    f.parent.profile.vk_use_spirv_generator = requested;
    config.framebuffer.shadow_rendering.Assign(incompatible);
    Demand(f, config); f.parent.shader_workers.WaitForRequests();
    const bool direct = requested && !incompatible;
    Require(Probe::spirv_calls == direct && Probe::glsl_calls == !direct,
            "generator choice and compatibility guard select exact endpoint");
    Require(Probe::compile_calls == !direct && Probe::module_calls == 1,
            "direct SPIRV bypasses GLSL frontend only");
    Require(f.cache.ready_fragment_spirv_builds == direct &&
            f.cache.ready_fragment_glsl_builds == !direct,
            "effective generator attempt diagnostics match actual endpoint");
    Require(f.cache.UseReadyFragmentShader(config, {}, false).has_value(),
            "selected generator publishes ready module");
    f.parent.profile.vk_use_spirv_generator = !requested;
    Require(!f.cache.UseReadyFragmentShader(config, {}, false),
            "generator profile mismatch cannot reuse prior module");
}

void DirectFailure(unsigned failure) {
    Probe::Reset(); Fixture f; const auto config = Config(81);
    f.parent.profile.vk_use_spirv_generator = true;
    Probe::failure = failure;
    Demand(f, config); f.parent.shader_workers.WaitForRequests();
    const auto& entry = *f.cache.ready_fragments.at(config.Hash());
    Require(entry.shader.IsDone() && entry.shader.HasFailed(),
            "direct generator failure publishes terminal fallback");
    Require(Probe::spirv_calls == 1 && Probe::glsl_calls == 0 && Probe::compile_calls == 0,
            "direct failure reached SPIRV endpoint without GLSL retry");
    Require(Probe::module_calls == (failure == 4 || failure == 7 || failure == 8),
            "direct failure reached intended module boundary");
    Require(!f.cache.UseReadyFragmentShader(config, {}, false),
            "failed direct module cannot be borrowed");
}

// CodexAstraLocal: Lookup-only must not silently warm demand. Module capacity is
// shared and exact:128 retained objects, terminal failures included, no eviction.
void LookupAndCapacity() {
    // CodexAstraLocal: Renewal must not lower the inherited initial cold/warm
    // full-module envelope. Read the actual extracted production initializer.
    { Fixture f; Require(f.cache.full_module_credit.Credits() == ReadyFragmentPolicy::MaxModules,
                         "full initial credits preserve inherited capacity"); }
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
    // CodexAstraLocal: Empty output has typed source evidence and the same
    // bounded error delivery guarantee as unclassified compiler failures.
    Require(Probe::log_calls == 1 && (kind != 3 || f.cache.optional_source_failures == 1),
            "first failure must retain typed or bounded diagnostic evidence");
}
// CodexAstraLocal: Retry policy must follow typed failure, exact ownership and
// time boundaries. Advance a deadline directly rather than sleeping in this
// deterministic fixture; compile/queue/publication bodies remain unmodified.
void TypedFailures() {
    for (unsigned failure : {10U, 11U, 12U, 13U}) {
        Probe::Reset(); Fixture f; const auto config = Config(3000 + failure);
        const bool resource = failure == 10 || failure == 13;
        Probe::failure = failure; Probe::logger_throws = true;
        Demand(f, config); f.parent.shader_workers.WaitForRequests();
        auto* entry = f.cache.ready_fragments.at(config.Hash()).get();
        Require(entry->shader.IsDone() && entry->shader.HasFailed() &&
                entry->compiler_released.load(), "typed failure publishes released terminal owner");
        Require(entry->retryable == resource && Probe::log_calls == 1,
                "typed resource-only retry and throwing diagnostics are contained");
        Require(f.cache.optional_resource_failures == resource &&
                f.cache.optional_device_failures == (failure == 11) &&
                f.cache.optional_source_failures == (failure == 12),
                "typed failure diagnostics preserve exact classification");
        Demand(f, config, 32);
        Require(f.parent.shader_workers.accepted == 1, "typed failure backoff blocks immediate retry");
        entry->retry_after_ms = 0;
        Demand(f, config, 1, false);
        Require(f.parent.shader_workers.accepted == 1, "read-only lookup never retries expired failure");
        if (resource) {
            // CodexAstraLocal: An open circuit must refuse before erasing the
            // failed owner or resetting its bounded retry history.
            f.cache.optional_circuit_until_ms.store(CompileBudget::NowMs() + 30000);
            Demand(f, config, 1);
            const auto retained = f.cache.ready_fragments.find(config.Hash());
            Require(retained != f.cache.ready_fragments.end() && retained->second.get() == entry &&
                    entry->retries == 0 && f.parent.shader_workers.accepted == 1,
                    "open circuit preserves failed owner and retry history");
            f.cache.optional_circuit_until_ms.store(0);
            f.parent.referenced_shader = &entry->shader;
            Demand(f, config, 1);
            Require(f.parent.shader_workers.accepted == 1, "referenced failed owner cannot be retried");
            f.parent.referenced_shader = nullptr;
        }
        Probe::failure = 0; Demand(f, config, 1); f.parent.shader_workers.WaitForRequests();
        Require(f.parent.shader_workers.accepted == (resource ? 2U : 1U),
                "only expired unreferenced resource failure retries");
        Require(resource ? f.cache.UseReadyFragmentShader(config, {}, false).has_value()
                         : !f.cache.UseReadyFragmentShader(config, {}, false),
                "resource retry recovers while permanent failure stays negative");
    }
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

// CodexAstraLocal: Owner allocation failure is contained with bounded negative
// history; queue allocation publishes terminal failure. No dangling warming
// pointer or leaked Shader is permitted, and repeated demand must honor backoff.
void Allocations() {
    unsigned contained_before_owner{}, contained_after_owner{}, success{};
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
        Require(!escaped, "optional owner allocation never escapes render caller");
        if (f.cache.ready_fragments.empty()) {
            ++contained_before_owner;
            Require(!f.cache.warming_ready_fragment && f.parent.shader_workers.accepted == 0 &&
                    f.cache.full_failure_history.Find(config.Hash(), {config, f.parent.profile}),
                    "pre-owner allocation failure leaves bounded history and no pending task");
            Demand(f, config, 32);
            Require(f.cache.ready_fragments.empty() && f.parent.shader_workers.accepted == 0,
                    "pre-owner resource history blocks immediate repeated demand");
        } else {
            Require(f.cache.ready_fragments.size() == 1, "contained/successful admission owns one entry");
            auto* entry = f.cache.ready_fragments.begin()->second.get();
            Require(entry->shader.IsDone(), "retained allocation outcome must be terminal");
            if (entry->shader.HasFailed()) ++contained_after_owner; else ++success;
        }
    }
    Require(contained_before_owner > 0 && contained_after_owner > 0 && success > 0,
            "allocator sweep must exercise preowner, queued-allocation and success cases");
}

// CodexAstraLocal: Twelve distinct failing modules prove one shared eight-detail
// budget while every failed closure still drains and publishes its terminal state.
void BoundedLogging() {
    Probe::Reset(); Fixture f; Probe::failure = 2; Probe::logger_throws = true;
    for (unsigned n = 0; n < 12; ++n) {
        auto config = Config(2000 + n);
        if (n >= 8) {
            Demand(f, config);
            Require(f.parent.shader_workers.accepted == n,
                    "failure circuit blocks immediate new optional work");
            // CodexAstraLocal: Advance only the circuit deadline to exercise
            // all twelve failures without sleeping thirty seconds per case.
            f.cache.optional_circuit_until_ms.store(0);
        }
        Demand(f, config);
        f.parent.shader_workers.WaitForRequests();
        Require(f.cache.ready_fragments.at(config.Hash())->shader.HasFailed(), "bounded failure recorded");
    }
    Require(Probe::log_calls == 8 && f.cache.ready_fragment_failures == 12 &&
            f.cache.ready_fragment_builds == 12, "failure diagnostic budget eight, all jobs accounted");
}

#ifdef UBERHAR_STATIC_TEV_WORKER
#include "static_tev_worker_cases.h"
#endif

// CodexAstraLocal: Select one bounded mutation witness or the fixed complete positive corpus.
int main(int argc, char** argv) {
    try {
        const std::string which = argc > 1 ? argv[1] : "all";
        if (which == "typed") TypedFailures();
        else if (which == "opaque") Failure(2, false);
        else if (which == "logger") Failure(1, true);
        else if (which == "queue") QueueFailure();
        else if (which == "lookup") LookupAndCapacity();
        else if (which == "profile") SnapshotAndPending();
        else if (which == "generator") GeneratorChoice(true, false);
        else if (which == "incompatible") GeneratorChoice(true, true);
        else {
            SnapshotAndPending(); LookupAndCapacity(); QueueFailure(); Allocations(); BoundedLogging(); TypedFailures();
            SnapshotAndPending(true);
            for (bool requested : {false, true})
                for (bool incompatible : {false, true}) GeneratorChoice(requested, incompatible);
            for (unsigned failure : {4U, 5U, 6U, 7U, 8U, 9U}) DirectFailure(failure);
#ifdef UBERHAR_STATIC_TEV_WORKER
            PartialSnapshot(); PartialLimits(); PartialFailures(); SharedLane();
#endif
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

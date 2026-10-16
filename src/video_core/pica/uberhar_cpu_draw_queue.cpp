// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#include "video_core/pica/uberhar_cpu_draw_queue.h"

#include <atomic>
#include <condition_variable>
#include <chrono> // CodexAstraLocal: Sampled owner-only feature timing.
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
#include "common/uberhar_cpu_work_budget.h"
#include "video_core/pica/uberhar_parallel_vertex.h"
#include "video_core/shader_recovery_error.h"

namespace Pica {
namespace {
using Environment = Common::Uberhar::ParallelFloatEnvironment;
// CodexAstraLocal: Charge the prior smallest admitted draw once per grouped
// packet. The proxy bounds expansion; it does not predict a device speedup.
constexpr u64 GroupWorkUnit = 96 * 35;
// CodexAstraLocal: Small owned members retain a minimum useful-work floor and
// bounded capture charge. Admission and dispatch use this same partition rule.
constexpr u64 MemberWorkUnit = 12 * 35;
constexpr std::size_t MaxGroupMembers = 4, MaxPackets = 64;

// CodexAstraLocal TEST-ONLY LOG: Sparse disjoint feature spans never count as
// saved time or GPU execution; remove after .50 qualification. No shared atomics.
struct ScopedCost {
    bool enabled; u64& samples; u64& nanoseconds;
    std::chrono::steady_clock::time_point start;
    ScopedCost(bool measure, u64& count, u64& total)
        : enabled{measure}, samples{count}, nanoseconds{total},
          start{measure ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}} {}
    ~ScopedCost() {
        if (enabled) {
            ++samples;
            nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
        }
    }
};

// CodexAstraLocal: Credit follows the last CPU/renderer reference, not merely
// completion. Canceled command captures release safely without waiting, and
// completed tickets held by a slow command queue cannot evade the byte bound.
struct Budget {
    std::mutex mutex;
    std::size_t packets{}, bytes{};
};
struct Credit {
    std::shared_ptr<Budget> budget;
    std::size_t bytes{};
    bool armed{};
    ~Credit() {
        if (armed) {
            std::lock_guard lock{budget->mutex};
            --budget->packets;
            budget->bytes -= bytes;
        }
    }
};
// CodexAstraLocal: Resolve only caller-owned capture ranges. Byte-wise index
// decoding avoids alignment assumptions and preserves original FIFO identities
// while later owned attribute copies rebase the address domain.
std::span<const u8> Map(const CpuDrawCapture& source, PAddr address) {
    for (u32 n = 0; n < source.binding_count; ++n) {
        const auto& binding = source.bindings[n];
        if (address >= binding.address && u64(address) - binding.address < binding.bytes.size())
            return binding.bytes.subspan(address - binding.address);
    }
    return {};
}
u32 Vertex(const CpuDrawCapture& source, u32 index) {
    if (!source.indexed) return source.base_vertex + index;
    u16 value{};
    std::memcpy(&value, source.index_bytes.data() + u64(index) * source.index_width,
                source.index_width);
    return value;
}
} // namespace

struct CpuDrawPacket::Impl {
    ShaderRunLease lease;
    Uniforms uniforms{};
    AttributeBuffer defaults{};
    NativeVertexInputPlan input;
    std::unique_ptr<NativeVertexPlan> output;
    std::vector<u8> raw, hardware;
    std::vector<u32> vertices;
    std::vector<OutputVertex> converted;
    NativeVertexCounts counts;
    std::array<OutputVertex, 2> last_pair;
    Environment environment;
    CpuDrawHardwareWriter writer{};
    u32 first_vertex{}, vertex_count{};
    bool indexed{};
    u64 guaranteed_work{};
    u64 effect_identity{}; // CodexAstraLocal: Exact proof generation for comparable wave history.
    bool requires_group{};
    bool work_seed{}; // CodexAstraLocal: Count newly admitted small seeds only after publication.
    std::shared_ptr<Credit> credit;
    mutable std::mutex mutex;
    mutable std::condition_variable completion;
    std::atomic<bool> submitted{}, ready{};
    std::exception_ptr error;

    // CodexAstraLocal: A complete draw runs one fresh unit and its original FIFO
    // serially. Only different packets run concurrently, including carried shader
    // values. The stateless writer receives complete owned output and no renderer.
    void Execute() noexcept {
        const auto incoming = Environment::Capture();
        environment.Apply();
        try {
            ShaderUnit unit;
            const auto context = lease.Bind(uniforms);
            NativeVertexSamples samples;
            u32 out{};
            counts = RunNativeVertexBatch<false>(vertex_count, indexed,
                [&](u32 index) { return vertices[index]; },
                [&]<bool>(u32 vertex, u32) {
                    input.Load(unit, defaults, vertex - first_vertex);
                    context.Run(unit);
                    return output->Convert(unit);
                },
                [&](const OutputVertex& value) { converted[out++] = value; }, samples);
            last_pair = {converted[vertex_count - 3], converted[vertex_count - 2]};
            writer(converted, hardware);
        } catch (...) {
            error = std::current_exception();
        }
        // CodexAstraLocal: The admitted backend isolates guest FP status; only
        // restoring this worker's incoming environment has a runtime consumer.
        incoming.Apply();
        // CodexAstraLocal: Publish only after all packet writes and FP restoration.
        // Existing pool borrowers still own the packet until the full wave joins.
        {
            std::lock_guard lock{mutex};
            ready.store(true, std::memory_order_release);
        }
        completion.notify_all();
    }
    void Fail(std::exception_ptr cause) noexcept {
        {
            std::lock_guard lock{mutex};
            if (ready.load(std::memory_order_relaxed)) return;
            error = std::move(cause);
            ready.store(true, std::memory_order_release);
        }
        completion.notify_all();
    }
};

CpuDrawPacket::CpuDrawPacket(std::unique_ptr<Impl> state) : impl{std::move(state)} {}
CpuDrawPacket::~CpuDrawPacket() = default;

void CpuDrawPacket::Wait() const {
    // CodexAstraLocal: Waiting on an unpublished packet would deadlock the
    // scheduler. Refuse it through the same contained terminal type as failure.
    if (!impl->submitted.load(std::memory_order_acquire))
        throw VideoCore::ShaderRecoveryError{"Uberhar CPU draw was not published"};
    std::unique_lock lock{impl->mutex};
    impl->completion.wait(lock, [&] { return impl->ready.load(std::memory_order_acquire); });
    if (impl->error)
        throw VideoCore::ShaderRecoveryError{"Uberhar deferred CPU draw failed"};
}
std::span<const u8> CpuDrawPacket::HardwareBytes() const { Wait(); return impl->hardware; }
CpuDrawPacket::Completion CpuDrawPacket::CompletedResult() const {
    Wait(); return {impl->counts, impl->last_pair};
}
bool CpuDrawPacket::TryCompletedResult(Completion& result) const {
    if (!impl->submitted.load(std::memory_order_acquire) ||
        !impl->ready.load(std::memory_order_acquire)) return false;
    if (impl->error)
        throw VideoCore::ShaderRecoveryError{"Uberhar deferred CPU draw failed"};
    result = {impl->counts, impl->last_pair};
    return true;
}
u32 CpuDrawPacket::VertexCount() const noexcept { return impl->vertex_count; }

struct CpuDrawExecutor::Impl {
    struct Effect {
        ShaderRunLease lease;
        u16 booleans{};
        bool valid{}, accepted{};
        u32 minimum_arithmetic{};
        u64 identity{};
    };
    // CodexAstraLocal: This new coordinator owns the unchanged synchronous pool.
    // It can run without the emulator/Vulkan owners, closing the Record/Map/wait
    // cycle. Idle queue and workers sleep; no collection delay or polling exists.
    // CodexAstraLocal: Capacity reservation guarantees coordinator progress but
    // holds no active permit while idle or joining. Without a reserved lane,
    // Submit keeps its existing synchronous forward-progress path.
    // CodexAstraLocal: Policy-only refusals create no worker or reserved lane.
    // The first owned accepted packet starts progress before publication.
    std::optional<Common::Uberhar::AuxiliaryBudget::ProgressLane> progress_lane;
    std::unique_ptr<Common::Uberhar::ParallelWork> pool;
    bool asynchronous{}, start_attempted{};
    const unsigned processors;
    const CpuDrawPolicy policy;
    const std::size_t max_packets, max_bytes;
    void (*worker_start)();
    std::shared_ptr<Budget> budget{std::make_shared<Budget>()};
    mutable std::mutex mutex;
    std::condition_variable wake, drained;
    std::vector<std::shared_ptr<CpuDrawPacket>> pending, wave;
    struct Grain { u32 first{}, end{}; bool coordinator{}; };
    std::vector<Grain> grains;
    // CodexAstraLocal: Exact Partition reads packet costs directly; retaining
    // a scalar pending-work total would add bookkeeping with no consumer.
    // The locked seed state alone serves admission; the removed PICA advisory
    // read no longer justifies publishing an atomic mirror on every queue change.
    bool pending_seed{};
    bool running{}, stopping{};
    bool capture_at_capacity{};
    std::exception_ptr terminal;
    std::thread coordinator;
    Statistics statistics;
    // CodexAstraLocal: Owner-only refusal/cache counters never contend with the
    // coordinator. GetStatistics merges them under the existing report contract.
    Statistics owner_statistics;
    std::array<Effect, 16> effects{};
    u32 next_effect{};
    mutable std::size_t last_effect{}; // CodexAstraLocal: Adjacent exact policy hits precede the bounded scan.
    // CodexAstraLocal: Cache slots already own immutable leases; the accepted
    // owner-only token needs no second shared_ptr copy. Generation plus exact
    // lease/entry/Boolean validation refuses overwritten or changed decisions.
    struct Approval { std::size_t slot{}; u64 generation{}; };
    std::optional<Approval> approved_effect;
    u64 eligibility_calls{}, proof_calls{};
    u64 effect_generation{};
    struct WaveKey {
        u64 effect{}, work{}; u32 vertices{}; bool indexed{};
        bool operator==(const WaveKey&) const = default;
    };
    struct WaveIdentity { WaveKey key; u64 identity{}; };
    std::array<WaveIdentity, 4> wave_identities{};
    u64 next_wave_identity{};

    Impl(unsigned available, std::size_t packets, std::size_t bytes, void (*start)(), CpuDrawPolicy options)
        : processors{available}, policy{options}, max_packets{packets}, max_bytes{bytes}, worker_start{start} {
        if (!max_packets || max_packets > MaxPackets || !max_bytes)
            throw std::invalid_argument{"invalid CPU draw queue limits"};
        // CodexAstraLocal: Both queues reserve their fixed credit cap before the
        // thread starts; Submit and the coordinator exchange cannot allocate.
        pending.reserve(max_packets);
        wave.reserve(max_packets);
        grains.reserve(max_packets);
    }
    void StartProgress() {
        if (start_attempted) return;
        start_attempted = true; ++owner_statistics.start_requests;
        if (processors < 2) { ++owner_statistics.start_refused; return; }
        progress_lane.emplace(Common::Uberhar::AuxiliaryBudget::Global().ReserveProgress());
        if (!*progress_lane) { progress_lane.reset(); ++owner_statistics.start_refused; return; }
        try {
            pool = std::make_unique<Common::Uberhar::ParallelWork>(
                std::max(1U, processors - 1), worker_start,
                Common::Uberhar::AuxiliaryBudget::Global(), &*progress_lane,
                policy.adaptive, policy.sampled_costs);
            asynchronous = true;
            coordinator = std::thread{[this] { Loop(); }};
            ++owner_statistics.async_starts;
        } catch (const std::system_error&) {
            // CodexAstraLocal: No packet has been published. Failed optional
            // startup releases the lane and retains synchronous forward progress.
            asynchronous = false; pool.reset(); progress_lane.reset(); ++owner_statistics.start_failed;
        } catch (...) {
            asynchronous = false; pool.reset(); progress_lane.reset(); throw;
        }
    }
    ~Impl() {
        {
            std::lock_guard lock{mutex}; stopping = true;
        }
        wake.notify_one();
        if (coordinator.joinable()) coordinator.join();
    }

    static bool Matches(const Effect& effect, const ShaderRunLease& lease, u16 booleans) {
        return effect.valid && effect.lease.OwnerIdentity() == lease.OwnerIdentity() &&
            effect.lease.EntryPoint() == lease.EntryPoint() && effect.booleans == booleans;
    }
    const Effect* FindEffect(const ShaderRunLease& lease, u16 booleans) const {
        // CodexAstraLocal: Cached-only lookup cannot compile/analyze or borrow
        // guest state. Retained leases prevent address reuse from aliasing code.
        if (Matches(effects[last_effect], lease, booleans)) return &effects[last_effect];
        for (std::size_t i = 0; i < effects.size(); ++i) if (Matches(effects[i], lease, booleans)) {
            last_effect = i; return &effects[i];
        }
        return nullptr;
    }
    const Effect& Effects(const ShaderRunLease& lease, u16 booleans) {
        // CodexAstraLocal: Immutable lease ownership prevents address ABA. The
        // same graph first proves acyclic, valid, effect-free shader flow; only
        // its later per-vertex carry refusals are irrelevant to whole-draw work.
        if (const auto* effect = FindEffect(lease, booleans)) {
            ++owner_statistics.effect_hits; return *effect;
        }
        ParallelVertexWork work;
        const auto proof = AnalyzeParallelVertex(lease.Program(), lease.Swizzles(), lease.EntryPoint(),
            booleans, 0, ParallelVertexContract::FullArithmeticReads, &work);
        bool accepted{};
        switch (proof.status) {
        case ParallelVertexStatus::Independent: case ParallelVertexStatus::TemporaryCarry:
        case ParallelVertexStatus::AddressCarry: case ParallelVertexStatus::ConditionCarry:
        case ParallelVertexStatus::OutputCarry: accepted = true; break;
        default: break;
        }
        last_effect = next_effect++ % effects.size();
        auto& cached = effects[last_effect];
        // CodexAstraLocal: Saturation disables generation-dependent reuse and
        // adaptive identity, rather than allowing a wrapped token to alias.
        const auto generation = effect_generation == std::numeric_limits<u64>::max()
            ? 0 : ++effect_generation;
        cached = {lease, booleans, true, accepted && work.valid, work.minimum_arithmetic,
                  generation};
        ++owner_statistics.effect_builds;
        return cached;
    }

    // CodexAstraLocal: Exact bounded contiguous partition is shared by final
    // admission and dispatch. Aggregate credit alone cannot authorize a cheap
    // tail that would require an oversized or underqualified final grain.
    bool Partition(const std::vector<std::shared_ptr<CpuDrawPacket>>& packets,
                   std::optional<u64> extra, std::vector<Grain>* output = nullptr) const {
        const auto size = packets.size() + extra.has_value();
        if (!size || size > max_packets) return false;
        std::array<int, MaxPackets + 1> prior;
        prior.fill(-1); prior[0] = 0;
        const auto cost = [&](std::size_t i) {
            return i < packets.size() ? packets[i]->impl->guaranteed_work : *extra;
        };
        for (std::size_t end = 1; end <= size; ++end) {
            u64 work{};
            for (std::size_t members = 1; members <= std::min(end, MaxGroupMembers); ++members) {
                const auto value = cost(end - members);
                if (value < MemberWorkUnit) break;
                work += value;
                if (prior[end - members] >= 0 &&
                    work >= GroupWorkUnit + MemberWorkUnit * (members - 1)) {
                    prior[end] = static_cast<int>(end - members); break;
                }
            }
        }
        if (prior[size] < 0) return false;
        if (output) {
            output->clear();
            for (auto end = size; end; end = static_cast<std::size_t>(prior[end]))
                output->push_back({static_cast<u32>(prior[end]), static_cast<u32>(end), false});
            std::reverse(output->begin(), output->end());
        }
        return true;
    }
    // CodexAstraLocal: Executing waves cannot fund a new member. Only already
    // owned pending packets participate; no timer or future capture is assumed.
    bool PendingGroupFits(u64 work) const {
        return policy.broader_groups && asynchronous && !stopping && !terminal &&
               pending_seed && !pending.empty() && Partition(pending, work);
    }
    bool CanJoinPending(u64 work) const {
        std::lock_guard lock{mutex};
        return PendingGroupFits(work);
    }
    bool AllowsEffect(const Effect* effect, u32 count) {
        ScopedCost cost{policy.sampled_costs && (++eligibility_calls % 1024 == 0),
                        owner_statistics.eligibility_samples, owner_statistics.eligibility_ns};
        approved_effect.reset();
        const bool shape = count && count <= 65535 && count % 3 == 0;
        const bool supported = effect && effect->accepted;
        const bool useful = supported && effect->minimum_arithmetic >= 35 &&
            ((count >= 96 || (policy.expanded_seeds &&
                u64(count) * effect->minimum_arithmetic >= GroupWorkUnit)) ||
             (u64(count) * effect->minimum_arithmetic >= MemberWorkUnit &&
              CanJoinPending(u64(count) * effect->minimum_arithmetic)));
        if (shape && useful) {
            if (effect->identity) approved_effect = Approval{
                static_cast<std::size_t>(effect - effects.data()), effect->identity};
            return true;
        }
        // CodexAstraLocal: These fields have only the emulation-owner writer;
        // no queue lock is needed solely to count a negative policy decision.
        ++owner_statistics.captures; ++owner_statistics.refused; ++owner_statistics.early_refused;
        owner_statistics.work_refused += shape && supported && !useful;
        return false;
    }
    u64 WaveWorkIdentity() {
        // CodexAstraLocal: Mixed shaders/costs keep the static cap. Comparable
        // homogeneous waves retain an exact proof/cost/input key, never a hash.
        const auto key_for = [](const auto& packet) {
            return WaveKey{packet->impl->effect_identity, packet->impl->guaranteed_work,
                           packet->impl->vertex_count, packet->impl->indexed};
        };
        const auto key = key_for(wave.front());
        if (!key.effect) return 0; // CodexAstraLocal: Exhausted generations stay static.
        for (const auto& packet : wave) if (!(key_for(packet) == key)) return 0;
        for (const auto& entry : wave_identities) if (entry.identity && entry.key == key) return entry.identity;
        // CodexAstraLocal: Identity exhaustion cannot recycle an adaptive key.
        if (next_wave_identity == std::numeric_limits<u64>::max()) return 0;
        const auto identity = ++next_wave_identity;
        wave_identities[(identity - 1) % wave_identities.size()] = {key, identity};
        return identity;
    }

    void Loop() noexcept {
        try {
            if (worker_start) {
                try { worker_start(); } catch (...) {} // Names cannot break task ownership.
            }
            for (;;) {
                {
                    std::unique_lock lock{mutex};
                    wake.wait(lock, [&] { return stopping || !pending.empty(); });
                    if (pending.empty() && stopping) break;
                    running = true;
                    wave.swap(pending);
                    pending_seed = false;
                }
                // CodexAstraLocal TEST-ONLY LOG: Sparse complete-wave wall and
                // owner/join costs price actual grouping; remove after .49 qualification.
                const bool measure = policy.sampled_costs && statistics.waves % 64 == 0;
                const auto begin = measure ? std::chrono::steady_clock::now() :
                                             std::chrono::steady_clock::time_point{};
                // CodexAstraLocal: Publication already proved this partition.
                // Unexpected loss is terminal for every ticket, never replayed.
                if (!Partition(wave, std::nullopt, &grains))
                    throw VideoCore::ShaderRecoveryError{"invalid CPU draw work partition"};
                pool->SetWorkIdentity(WaveWorkIdentity());
                const auto coordinator_id = std::this_thread::get_id();
                const auto result = pool->Run(static_cast<u32>(grains.size()), 1,
                    [&](u32 first, u32 end) {
                        const bool on_coordinator = std::this_thread::get_id() == coordinator_id;
                        for (u32 n = first; n < end; ++n) {
                            auto& grain = grains[n]; grain.coordinator = on_coordinator;
                            for (u32 p = grain.first; p < grain.end; ++p) wave[p]->impl->Execute();
                        }
                    }, measure);
                const u64 elapsed = measure ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - begin).count() : 0;
                u64 failures{};
                for (const auto& packet : wave) failures += bool(packet->impl->error);
                u64 owner_packets{};
                for (const auto& grain : grains)
                    if (grain.coordinator) owner_packets += grain.end - grain.first;
                {
                    std::lock_guard lock{mutex};
                    ++statistics.waves; statistics.completed += wave.size(); statistics.failed += failures;
                    statistics.maximum_wave = std::max(statistics.maximum_wave, static_cast<u32>(wave.size()));
                    statistics.coordinator_packets += owner_packets;
                    statistics.auxiliary_packets += wave.size() - owner_packets;
                    statistics.grouped_waves += wave.size() > 1;
                    statistics.work_grains += grains.size();
                    statistics.peak_threads = std::max(statistics.peak_threads, result.working_threads);
                    statistics.scheduling = pool->Scheduling();
                    if (measure) {
                        ++statistics.cost_samples; statistics.cost_packets += wave.size();
                        statistics.wave_ns += elapsed; statistics.owner_ns += result.owner_process_ns;
                        statistics.join_ns += result.join_ns;
                        statistics.wave_max_ns = std::max(statistics.wave_max_ns, elapsed);
                    }
                }
                wave.clear();
                {
                    std::lock_guard lock{mutex};
                    running = false;
                }
                drained.notify_all();
            }
        } catch (...) {
            // CodexAstraLocal: Pool Run drains its borrowers before throwing.
            // A coordinator failure completes every unstarted ticket as terminal
            // so an ordered scheduler wait cannot hang. No failed work is retried.
            std::vector<std::shared_ptr<CpuDrawPacket>> canceled;
            {
                std::lock_guard lock{mutex};
                terminal = std::current_exception(); stopping = true;
                canceled.swap(pending);
            }
            // CodexAstraLocal: Include the active wave, not only pending tasks.
            // Publication stays terminal until every ticket has a completion.
            for (const auto& packet : wave) packet->impl->Fail(terminal);
            for (const auto& packet : canceled) packet->impl->Fail(terminal);
            u64 failures{};
            for (const auto& packet : wave) failures += bool(packet->impl->error);
            for (const auto& packet : canceled) failures += bool(packet->impl->error);
            const auto completions = wave.size() + canceled.size();
            wave.clear();
            canceled.clear();
            {
                std::lock_guard lock{mutex};
                statistics.completed += completions;
                statistics.failed += failures;
                running = false;
            }
            drained.notify_all();
        }
    }
};

CpuDrawExecutor::CpuDrawExecutor(unsigned processors, std::size_t packets, std::size_t bytes,
                               void (*start)(), CpuDrawPolicy policy)
    : impl{std::make_unique<Impl>(processors, packets, bytes, start, policy)} {}
CpuDrawExecutor::~CpuDrawExecutor() = default;

bool CpuDrawExecutor::AllowsShaderWork(const ShaderRunLease& lease, u16 booleans, u32 count) {
    impl->capture_at_capacity = false;
    return impl->AllowsEffect(lease ? &impl->Effects(lease, booleans) : nullptr, count);
}
std::optional<bool> CpuDrawExecutor::CachedShaderWork(const ShaderRunLease& lease, u16 booleans,
                                                     u32 count) {
    // CodexAstraLocal: Unknown identities preserve original preparation. A
    // cached positive proof is retained for exact validation at Capture, too.
    if (!impl->policy.cached_precheck || !lease) return std::nullopt;
    ++impl->owner_statistics.cached_checks;
    const Impl::Effect* effect{};
    {
        ScopedCost lookup{impl->policy.sampled_costs &&
            (impl->owner_statistics.cached_checks % 1024 == 0),
            impl->owner_statistics.lookup_samples, impl->owner_statistics.lookup_ns};
        effect = impl->FindEffect(lease, booleans);
    }
    if (!effect) { ++impl->owner_statistics.cached_misses; return std::nullopt; }
    ++impl->owner_statistics.cached_hits;
    impl->capture_at_capacity = false;
    const bool accepted = impl->AllowsEffect(effect, count);
    impl->owner_statistics.cached_negative += !accepted;
    return accepted;
}

std::shared_ptr<CpuDrawPacket> CpuDrawExecutor::Capture(const CpuDrawCapture& source,
                                                      CpuDrawHardwareWriter writer, u32 stride) {
    const auto refuse = [&](bool capacity = false, bool work = false) -> std::shared_ptr<CpuDrawPacket> {
        impl->capture_at_capacity = capacity;
        ++impl->owner_statistics.refused;
        impl->owner_statistics.work_refused += work; return {};
    };
    ++impl->owner_statistics.captures;
    impl->capture_at_capacity = false;
    // CodexAstraLocal: Complete nonempty List/Shader triples are the first seam.
    // Root's adapter additionally establishes backend/observer/memory/renderer
    // guards; this class never authorizes guest command execution by itself.
    if (!source.shader_lease || !writer || stride != 88 || source.count == 0 ||
        source.count > 65535 || source.count % 3 || source.binding_count > 16 ||
        source.available_attributes > 16 || source.shader.max_input_attribute_index + 1 > source.available_attributes ||
        source.shader_lease.EntryPoint() != source.shader.main_offset ||
        !Environment::AllowsParallel() ||
        (source.indexed && ((source.index_width != 1 && source.index_width != 2) ||
            u64(source.count) * source.index_width > source.index_bytes.size())) ||
        (!source.indexed && u64(source.base_vertex) + source.count - 1 > std::numeric_limits<u32>::max()))
        return refuse();
    // CodexAstraLocal: Reuse only the accepted immutable same-code/entry/boolean
    // proof; standalone captures and changed keys still perform normal lookup.
    const auto booleans = ParallelVertexBooleanUniforms(source.uniforms);
    const Impl::Effect* selected_effect{};
    {
        ScopedCost proof{impl->policy.sampled_costs && (++impl->proof_calls % 1024 == 0),
                         impl->owner_statistics.proof_samples, impl->owner_statistics.proof_ns};
        if (impl->approved_effect) {
            const auto& token = *impl->approved_effect;
            const auto& candidate = impl->effects[token.slot];
            if (token.generation && candidate.identity == token.generation &&
                Impl::Matches(candidate, source.shader_lease, booleans))
                selected_effect = &candidate;
            else ++impl->owner_statistics.token_mismatches;
        } else ++impl->owner_statistics.token_missing;
        impl->owner_statistics.effect_reuses += selected_effect != nullptr;
        if (!selected_effect) selected_effect = &impl->Effects(source.shader_lease, booleans);
    }
    // CodexAstraLocal: No cache mutation follows before the packet captures its
    // own source lease; no mutable cache reference crosses publication.
    const auto& effect = *selected_effect;
    if (!effect.accepted)
        return refuse();
    // CodexAstraLocal: The finite complete-path control lost on tiny/light
    // packets. This conservative arithmetic floor excludes those cohorts; it is
    // a workload policy, not an estimate or guarantee of target execution time.
    if (effect.minimum_arithmetic < 35) return refuse(false, true);
    u32 minimum = std::numeric_limits<u32>::max(), maximum{};
    VertexCacheIndex fifo;
    u32 misses{};
    for (u32 n = 0; n < source.count; ++n) {
        const u32 vertex = Vertex(source, n); minimum = std::min(minimum, vertex); maximum = std::max(maximum, vertex);
        // CodexAstraLocal: Reuse the original FIFO rules and identities. Input
        // count or unique-index count is not the number of actual shader calls.
        if (!source.indexed || fifo.Find(static_cast<u16>(vertex)) < 0) {
            ++misses;
            if (source.indexed) fifo.Insert(static_cast<u16>(vertex));
        }
    }
    const u64 guaranteed_work = u64(misses) * effect.minimum_arithmetic;
    const bool work_seed = misses >= 96 ||
        (impl->policy.expanded_seeds && guaranteed_work >= GroupWorkUnit);
    // CodexAstraLocal: Count the disabled standalone opportunity independently
    // of successful pending-member admission; final grouping checks remain live.
    impl->owner_statistics.expanded_refused += !impl->policy.expanded_seeds &&
        misses < 96 && guaranteed_work >= GroupWorkUnit;
    if (!work_seed) {
        if (!impl->CanJoinPending(guaranteed_work)) return refuse(false, true);
    }
    // CodexAstraLocal: This temporary validates guest mappings only; it never
    // loads vertices or reports recipe use. Skip its unused recipe selection,
    // retaining every range check and the executed packet's complete recipe.
    NativeVertexInputPlan checked;
    if (checked.Prepare(source.shader, source.available_attributes, source.base_address, maximum,
        [&](u32 n) { return source.attributes[n]; }, [&](PAddr address) { return Map(source, address); }, false)
        != NativeVertexInputPlan::Result::Ready) return refuse();
    auto output = std::make_unique<NativeVertexPlan>(source.shader, source.rasterizer);
    if (!output->Supported()) return refuse();
    auto dense = source.attributes;
    u64 raw_size{};
    for (u32 n = 0; n <= source.shader.max_input_attribute_index; ++n) {
        if (dense[n].is_default) continue;
        const u32 width = dense[n].format == PipelineRegs::VertexAttributeFormat::FLOAT ? 4 :
            dense[n].format == PipelineRegs::VertexAttributeFormat::SHORT ? 2 : 1;
        const u32 bytes = dense[n].elements * width;
        dense[n].offset = static_cast<u32>(raw_size);
        dense[n].stride = source.attributes[n].stride ? bytes : 0;
        raw_size += (dense[n].stride ? u64(maximum) - minimum + 1 : 1) * bytes;
        if (raw_size > std::numeric_limits<u32>::max()) return refuse();
    }
    // CodexAstraLocal: Dense snapshots still charge unused sparse holes. Bound
    // their raw copy volume by the largest tested qualifying input layout.
    if (raw_size > u64(misses) * 70) {
        ++impl->owner_statistics.copy_refused;
        return refuse(false, true);
    }
    const u64 footprint = sizeof(CpuDrawPacket) + sizeof(CpuDrawPacket::Impl) + sizeof(NativeVertexPlan) +
        raw_size + u64(source.count) * (sizeof(u32) + sizeof(OutputVertex) + stride);
    if (footprint > impl->max_bytes) return refuse(true);
    auto credit = std::make_shared<Credit>(); credit->budget = impl->budget; credit->bytes = footprint;
    bool at_capacity{};
    {
        std::lock_guard lock{impl->budget->mutex};
        at_capacity = impl->budget->packets >= impl->max_packets ||
            impl->budget->bytes > impl->max_bytes - footprint;
        if (!at_capacity) {
            ++impl->budget->packets; impl->budget->bytes += footprint; credit->armed = true;
        }
    }
    if (at_capacity) return refuse(true);
    auto packet = std::make_unique<CpuDrawPacket::Impl>();
    packet->credit = std::move(credit);
    packet->lease = source.shader_lease; packet->uniforms = source.uniforms; packet->defaults = source.defaults;
    packet->output = std::move(output); packet->environment = Environment::Capture(); packet->writer = writer;
    packet->first_vertex = minimum; packet->vertex_count = source.count; packet->indexed = source.indexed;
    packet->guaranteed_work = guaranteed_work; packet->requires_group = !work_seed;
    packet->work_seed = work_seed && misses < 96;
    packet->effect_identity = effect.identity;
    packet->vertices.resize(source.count); packet->converted.resize(source.count);
    packet->hardware.resize(u64(source.count) * stride); packet->raw.resize(raw_size);
    for (u32 n = 0; n < source.count; ++n) packet->vertices[n] = Vertex(source, n);
    // CodexAstraLocal: Rebase only owned loader addresses; FIFO identities stay
    // original. Zero-stride attributes copy one value, and high unindexed bases
    // do not copy an unused prefix. Sparse index holes remain charged/bounded.
    for (u32 n = 0; n <= source.shader.max_input_attribute_index; ++n) {
        if (dense[n].is_default) continue;
        const auto& old = source.attributes[n];
        const auto memory = Map(source, source.base_address + old.offset);
        const u32 width = old.format == PipelineRegs::VertexAttributeFormat::FLOAT ? 4 :
            old.format == PipelineRegs::VertexAttributeFormat::SHORT ? 2 : 1;
        const u32 bytes = width * old.elements;
        const u64 rows = old.stride ? u64(maximum) - minimum + 1 : 1;
        for (u64 row = 0; row < rows; ++row)
            std::memcpy(packet->raw.data() + dense[n].offset + row * dense[n].stride,
                        memory.data() + (u64(minimum) + row) * old.stride, bytes);
    }
    if (packet->input.Prepare(source.shader, source.available_attributes, 0, maximum - minimum,
        [&](u32 n) { return dense[n]; },
        [&](PAddr address) { return std::span<const u8>{packet->raw}.subspan(address); }, true)
        != NativeVertexInputPlan::Result::Ready) return refuse();
    if (packet->requires_group) {
        ++impl->owner_statistics.group_candidates;
    }
    return std::shared_ptr<CpuDrawPacket>{new CpuDrawPacket{std::move(packet)}};
}

bool CpuDrawExecutor::LastCaptureAtCapacity() const noexcept { return impl->capture_at_capacity; }

bool CpuDrawExecutor::Submit(const std::shared_ptr<CpuDrawPacket>& packet) {
    if (!packet || packet->impl->credit->budget != impl->budget) return false;
    // CodexAstraLocal: Reserve/start only for an actual owned qualifying seed.
    // No task is visible during optional startup or its synchronous fallback.
    impl->StartProgress();
    {
        std::lock_guard lock{impl->mutex};
        if (impl->stopping || impl->terminal || packet->impl->submitted.load(std::memory_order_relaxed)) return false;
        // CodexAstraLocal: This is the last reversible boundary. A coordinator
        // exchange during capture cannot leave an underqualified tiny packet
        // published alone; the caller still owns its untouched serial fallback.
        if (packet->impl->requires_group && !impl->PendingGroupFits(packet->impl->guaranteed_work)) {
            ++impl->statistics.group_lost; ++impl->owner_statistics.refused;
            ++impl->owner_statistics.work_refused;
            return false;
        }
        if (impl->asynchronous) {
            impl->pending.push_back(packet);
            impl->pending_seed |= !packet->impl->requires_group;
        }
        packet->impl->submitted.store(true, std::memory_order_release);
        ++impl->statistics.submitted;
        impl->owner_statistics.work_seeded += packet->impl->work_seed;
        impl->owner_statistics.original_seeded += !packet->impl->requires_group && !packet->impl->work_seed;
        if (packet->impl->requires_group) ++impl->statistics.group_submitted;
        else ++impl->statistics.seed_submitted;
    }
    if (impl->asynchronous) impl->wake.notify_one();
    else {
        packet->impl->Execute();
        std::lock_guard lock{impl->mutex};
        ++impl->statistics.completed; ++impl->statistics.waves; ++impl->statistics.coordinator_packets;
        ++impl->statistics.work_grains;
        impl->statistics.failed += bool(packet->impl->error);
        impl->statistics.maximum_wave = std::max(impl->statistics.maximum_wave, 1U);
        impl->statistics.peak_threads = 1;
    }
    return true;
}
void CpuDrawExecutor::Drain() {
    ++impl->owner_statistics.drain_calls;
    std::unique_lock lock{impl->mutex};
    // CodexAstraLocal TEST-ONLY LOG: At most one in64real drain calls reads a
    // clock; keep waits separate from execution, remove after .49 qualification.
    const bool measure = impl->policy.sampled_costs &&
        (impl->owner_statistics.drain_calls & 63U) == 1 && (!impl->pending.empty() || impl->running);
    const auto begin = measure ? std::chrono::steady_clock::now() :
                                 std::chrono::steady_clock::time_point{};
    impl->drained.wait(lock, [&] { return impl->pending.empty() && !impl->running; });
    if (measure) {
        ++impl->owner_statistics.drain_samples;
        impl->owner_statistics.drain_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count();
    }
    if (impl->terminal) throw VideoCore::ShaderRecoveryError{"Uberhar CPU draw coordinator failed"};
}
CpuDrawExecutor::Statistics CpuDrawExecutor::GetStatistics() const {
    std::scoped_lock lock{impl->mutex, impl->budget->mutex};
    auto result = impl->statistics;
    // CodexAstraLocal: GetStatistics is an owner-only snapshot. Only these
    // counters have exclusively owner writes; coordinator fields remain locked.
    const auto& owner = impl->owner_statistics;
    result.captures = owner.captures; result.refused = owner.refused;
    result.work_refused = owner.work_refused; result.early_refused = owner.early_refused;
    result.group_candidates = owner.group_candidates; result.copy_refused = owner.copy_refused;
    result.cached_checks = owner.cached_checks; result.cached_hits = owner.cached_hits;
    result.cached_negative = owner.cached_negative; result.cached_misses = owner.cached_misses;
    result.effect_hits = owner.effect_hits; result.effect_builds = owner.effect_builds;
    result.effect_reuses = owner.effect_reuses; result.work_seeded = owner.work_seeded;
    result.original_seeded = owner.original_seeded; result.expanded_refused = owner.expanded_refused;
    result.token_missing = owner.token_missing; result.token_mismatches = owner.token_mismatches;
    result.lookup_samples = owner.lookup_samples; result.lookup_ns = owner.lookup_ns;
    result.eligibility_samples = owner.eligibility_samples; result.eligibility_ns = owner.eligibility_ns;
    result.proof_samples = owner.proof_samples; result.proof_ns = owner.proof_ns;
    result.start_requests = owner.start_requests; result.start_refused = owner.start_refused;
    result.start_failed = owner.start_failed; result.async_starts = owner.async_starts;
    result.drain_calls = owner.drain_calls; result.drain_samples = owner.drain_samples;
    result.drain_ns = owner.drain_ns;
    result.resident_packets = impl->budget->packets; result.resident_bytes = impl->budget->bytes;
    return result;
}
} // namespace Pica

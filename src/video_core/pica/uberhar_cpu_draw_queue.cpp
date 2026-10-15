// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#include "video_core/pica/uberhar_cpu_draw_queue.h"

#include <atomic>
#include <condition_variable>
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
    bool requires_group{};
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
    };
    // CodexAstraLocal: This new coordinator owns the unchanged synchronous pool.
    // It can run without the emulator/Vulkan owners, closing the Record/Map/wait
    // cycle. Idle queue and workers sleep; no collection delay or polling exists.
    // CodexAstraLocal: Capacity reservation guarantees coordinator progress but
    // holds no active permit while idle or joining. Without a reserved lane,
    // Submit keeps its existing synchronous forward-progress path.
    Common::Uberhar::AuxiliaryBudget::ProgressLane progress_lane;
    Common::Uberhar::ParallelWork pool;
    const bool asynchronous;
    const std::size_t max_packets, max_bytes;
    void (*worker_start)();
    std::shared_ptr<Budget> budget{std::make_shared<Budget>()};
    mutable std::mutex mutex;
    std::condition_variable wake, drained;
    std::vector<std::shared_ptr<CpuDrawPacket>> pending, wave;
    struct Grain { u32 first{}, end{}; bool coordinator{}; };
    std::vector<Grain> grains;
    u64 pending_work{};
    bool pending_seed{};
    std::atomic<bool> pending_seed_hint{};
    bool running{}, stopping{};
    bool capture_at_capacity{};
    std::exception_ptr terminal;
    std::thread coordinator;
    Statistics statistics;
    std::array<Effect, 16> effects{};
    u32 next_effect{};

    Impl(unsigned processors, std::size_t packets, std::size_t bytes, void (*start)())
        : progress_lane{processors > 1 ? Common::Uberhar::AuxiliaryBudget::Global().ReserveProgress()
                                       : Common::Uberhar::AuxiliaryBudget::ProgressLane{}},
          pool{std::max(1U, processors > 1 ? processors - 1 : 1U), start,
               Common::Uberhar::AuxiliaryBudget::Global(), progress_lane ? &progress_lane : nullptr},
          asynchronous{processors > 1 && bool(progress_lane)}, max_packets{packets}, max_bytes{bytes}, worker_start{start} {
        if (!max_packets || max_packets > std::numeric_limits<u32>::max() || !max_bytes)
            throw std::invalid_argument{"invalid CPU draw queue limits"};
        // CodexAstraLocal: Both queues reserve their fixed credit cap before the
        // thread starts; Submit and the coordinator exchange cannot allocate.
        pending.reserve(max_packets);
        wave.reserve(max_packets);
        grains.reserve(max_packets);
        statistics.scheduling = pool.Scheduling();
        if (asynchronous) coordinator = std::thread{[this] { Loop(); }};
    }
    ~Impl() {
        {
            std::lock_guard lock{mutex}; stopping = true;
        }
        wake.notify_one();
        if (coordinator.joinable()) coordinator.join();
    }

    const Effect& Effects(const ShaderRunLease& lease, u16 booleans) {
        // CodexAstraLocal: Immutable lease ownership prevents address ABA. The
        // same graph first proves acyclic, valid, effect-free shader flow; only
        // its later per-vertex carry refusals are irrelevant to whole-draw work.
        for (const auto& effect : effects) {
            if (effect.valid && effect.lease.OwnerIdentity() == lease.OwnerIdentity() &&
                effect.lease.EntryPoint() == lease.EntryPoint() && effect.booleans == booleans) {
                return effect;
            }
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
        auto& cached = effects[next_effect++ % effects.size()];
        cached = {lease, booleans, true, accepted && work.valid, work.minimum_arithmetic};
        return cached;
    }

    // CodexAstraLocal: Call only with mutex held. Executing waves cannot fund a
    // new small packet: the seed and all admitted members must be picked up in
    // the same pending exchange. No fill timer or future packet is assumed.
    bool PendingGroupFits(u64 work) const {
        return asynchronous && !stopping && !terminal && pending_seed && !pending.empty() &&
               pending_work + work >= GroupWorkUnit * (pending.size() + 1);
    }
    bool CanJoinPending(u64 work) const {
        std::lock_guard lock{mutex};
        return PendingGroupFits(work);
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
                    pending_work = 0;
                    pending_seed = false;
                    pending_seed_hint.store(false, std::memory_order_relaxed);
                }
                // CodexAstraLocal: Dispatch contiguous cost-qualified ranges,
                // retaining one fresh unit/FIFO per packet. Merge a short tail
                // into the preceding range instead of scheduling tiny claims.
                grains.clear();
                u32 first{};
                u64 work{};
                for (u32 n = 0; n < wave.size(); ++n) {
                    work += wave[n]->impl->guaranteed_work;
                    if (work >= GroupWorkUnit) {
                        grains.push_back({first, n + 1, false}); first = n + 1; work = 0;
                    }
                }
                if (first < wave.size()) {
                    if (grains.empty()) grains.push_back({0, static_cast<u32>(wave.size()), false});
                    else grains.back().end = static_cast<u32>(wave.size());
                }
                const auto coordinator_id = std::this_thread::get_id();
                const auto result = pool.Run(static_cast<u32>(grains.size()), 1,
                    [&](u32 first, u32 end) {
                        const bool on_coordinator = std::this_thread::get_id() == coordinator_id;
                        for (u32 n = first; n < end; ++n) {
                            auto& grain = grains[n]; grain.coordinator = on_coordinator;
                            for (u32 p = grain.first; p < grain.end; ++p) wave[p]->impl->Execute();
                        }
                    });
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
                    statistics.scheduling = pool.Scheduling();
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
                pending_seed_hint.store(false, std::memory_order_relaxed);
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
                               void (*start)())
    : impl{std::make_unique<Impl>(processors, packets, bytes, start)} {}
CpuDrawExecutor::~CpuDrawExecutor() = default;
bool CpuDrawExecutor::HasPendingSeed() const noexcept {
    return impl->pending_seed_hint.load(std::memory_order_relaxed);
}

bool CpuDrawExecutor::AllowsShaderWork(const ShaderRunLease& lease, u16 booleans, u32 count) {
    impl->capture_at_capacity = false;
    bool accepted{}, work_refused{};
    if (lease && count && count <= 65535 && count % 3 == 0) {
        const auto& effect = impl->Effects(lease, booleans);
        if (effect.accepted) {
            accepted = effect.minimum_arithmetic >= 35 &&
                (count >= 96 || impl->CanJoinPending(u64(count) * effect.minimum_arithmetic));
            work_refused = !accepted;
        }
    }
    if (!accepted) {
        std::lock_guard lock{impl->mutex};
        ++impl->statistics.captures; ++impl->statistics.refused; ++impl->statistics.early_refused;
        impl->statistics.work_refused += work_refused;
    }
    return accepted;
}

std::shared_ptr<CpuDrawPacket> CpuDrawExecutor::Capture(const CpuDrawCapture& source,
                                                      CpuDrawHardwareWriter writer, u32 stride) {
    const auto refuse = [&](bool capacity = false, bool work = false) -> std::shared_ptr<CpuDrawPacket> {
        impl->capture_at_capacity = capacity;
        std::lock_guard lock{impl->mutex}; ++impl->statistics.refused;
        impl->statistics.work_refused += work; return {};
    };
    {
        std::lock_guard lock{impl->mutex}; ++impl->statistics.captures;
    }
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
    const auto& effect = impl->Effects(source.shader_lease, ParallelVertexBooleanUniforms(source.uniforms));
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
    if (misses < 96) {
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
        { std::lock_guard lock{impl->mutex}; ++impl->statistics.copy_refused; }
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
    packet->guaranteed_work = guaranteed_work; packet->requires_group = misses < 96;
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
        std::lock_guard lock{impl->mutex}; ++impl->statistics.group_candidates;
    }
    return std::shared_ptr<CpuDrawPacket>{new CpuDrawPacket{std::move(packet)}};
}

bool CpuDrawExecutor::LastCaptureAtCapacity() const noexcept { return impl->capture_at_capacity; }

bool CpuDrawExecutor::Submit(const std::shared_ptr<CpuDrawPacket>& packet) {
    if (!packet || packet->impl->credit->budget != impl->budget) return false;
    {
        std::lock_guard lock{impl->mutex};
        if (impl->stopping || impl->terminal || packet->impl->submitted.load(std::memory_order_relaxed)) return false;
        // CodexAstraLocal: This is the last reversible boundary. A coordinator
        // exchange during capture cannot leave an underqualified tiny packet
        // published alone; the caller still owns its untouched serial fallback.
        if (packet->impl->requires_group && !impl->PendingGroupFits(packet->impl->guaranteed_work)) {
            ++impl->statistics.group_lost; ++impl->statistics.refused; ++impl->statistics.work_refused;
            return false;
        }
        if (impl->asynchronous) {
            impl->pending.push_back(packet);
            // Credit above the finite member cap cannot authorize more packets;
            // cap only this work proxy to avoid accumulation overflow.
            impl->pending_work = std::min(GroupWorkUnit * impl->max_packets,
                impl->pending_work + packet->impl->guaranteed_work);
            impl->pending_seed |= !packet->impl->requires_group;
            impl->pending_seed_hint.store(impl->pending_seed, std::memory_order_relaxed);
        }
        packet->impl->submitted.store(true, std::memory_order_release);
        ++impl->statistics.submitted;
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
    std::unique_lock lock{impl->mutex};
    impl->drained.wait(lock, [&] { return impl->pending.empty() && !impl->running; });
    if (impl->terminal) throw VideoCore::ShaderRecoveryError{"Uberhar CPU draw coordinator failed"};
}
CpuDrawExecutor::Statistics CpuDrawExecutor::GetStatistics() const {
    std::scoped_lock lock{impl->mutex, impl->budget->mutex};
    auto result = impl->statistics;
    result.resident_packets = impl->budget->packets; result.resident_bytes = impl->budget->bytes;
    return result;
}
} // namespace Pica

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <memory>
#include <optional> // CodexAstraLocal: Cached-only early policy distinguishes unknown from refusal.
#include <span>
#include "common/uberhar_parallel_work.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/pica/uberhar_vertex_output.h"
#include "video_core/shader/shader.h"

namespace Pica {

// CodexAstraLocal: Owner-only capture views expire before publication. The
// packet copies raw bytes/indices/defaults/uniforms; MemoryRefs remain caller
// owned only through Capture, and never become worker-visible guest pointers.
struct CpuDrawCapture {
    struct Binding { PAddr address{}; std::span<const u8> bytes; };
    ShaderRunLease shader_lease;
    ShaderRegs shader{};
    RasterizerRegs rasterizer{};
    Uniforms uniforms{};
    AttributeBuffer defaults{};
    std::array<NativeInputAttribute, 16> attributes{};
    std::array<Binding, 16> bindings{};
    u32 binding_count{}, available_attributes{};
    PAddr base_address{};
    std::span<const u8> index_bytes;
    u32 index_width{}, count{}, base_vertex{};
    bool indexed{};
};

// CodexAstraLocal: Only the actual accelerated stateless complete-triangle writer
// qualifies. It receives preallocated output bytes, cannot allocate/reenter the
// renderer, and retains original 88-byte/quaternion arithmetic on CPU workers.
using CpuDrawHardwareWriter = void (*)(std::span<const OutputVertex>, std::span<u8>) noexcept;

class CpuDrawExecutor;
// CodexAstraLocal: Immutable construction policy supports bounded qualification
// without UI/config reads in the draw path. Hard ownership/byte limits never vary.
struct CpuDrawPolicy {
    bool cached_precheck{true};
    bool broader_groups{true};
    bool sampled_costs{true};
    // CodexAstraLocal: Expanded standalone seeds are independent of useful
    // pending-member grouping. Ordinary execution restores the .48 seed floor.
    bool expanded_seeds{false};
    bool adaptive{true};
};
class CpuDrawPacket {
public:
    struct Completion {
        NativeVertexCounts counts;
        std::array<OutputVertex, 2> last_pair;
    };
    ~CpuDrawPacket();
    CpuDrawPacket(const CpuDrawPacket&) = delete;
    CpuDrawPacket& operator=(const CpuDrawPacket&) = delete;

    // CodexAstraLocal: Waiting publishes a typed terminal error through the
    // existing Vulkan scheduler boundary. Accessors require completed work;
    // the owning shared packet retains bytes until command/caller release.
    void Wait() const;
    std::span<const u8> HardwareBytes() const;
    // CodexAstraLocal: Owner reconciliation takes one completion wait/lock for
    // both counters and retained primitive bytes, without repeating conversion.
    Completion CompletedResult() const;
    // CodexAstraLocal: A ready prefix may retire without waiting for unrelated
    // later packets. Acquire publication protects all result bytes; a ready
    // failure remains terminal and cannot be skipped by owner reconciliation.
    bool TryCompletedResult(Completion& result) const;
    u32 VertexCount() const noexcept;

private:
    struct Impl;
    explicit CpuDrawPacket(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
    friend class CpuDrawExecutor;
};

class CpuDrawExecutor {
public:
    struct Statistics {
        u64 captures{}, refused{};
        u64 work_refused{};
        // CodexAstraLocal: Separate preparation avoidance, real grouped
        // admissions, and lost pre-publication opportunities from throughput.
        u64 early_refused{}, group_candidates{}, seed_submitted{}, group_submitted{};
        u64 group_lost{}, copy_refused{}, grouped_waves{}, work_grains{};
        // CodexAstraLocal TEST-ONLY LOG: Remove feature/cost qualification fields
        // after .49 hardware acceptance; none authorizes publication or replay.
        u64 cached_checks{}, cached_hits{}, cached_negative{}, cached_misses{};
        u64 effect_hits{}, effect_builds{}, effect_reuses{}, work_seeded{};
        // CodexAstraLocal TEST-ONLY LOG: Owner-only feature costs and exclusive
        // token/seed outcomes; remove after .50 hardware qualification.
        u64 original_seeded{}, expanded_refused{}, token_missing{}, token_mismatches{};
        u64 lookup_samples{}, lookup_ns{}, eligibility_samples{}, eligibility_ns{};
        u64 proof_samples{}, proof_ns{};
        u64 start_requests{}, start_refused{}, start_failed{}, async_starts{};
        u64 cost_samples{}, cost_packets{}, wave_ns{}, owner_ns{}, join_ns{}, wave_max_ns{};
        u64 drain_calls{}, drain_samples{}, drain_ns{};
        u64 submitted{}, completed{}, failed{}, waves{}, coordinator_packets{}, auxiliary_packets{};
        u32 maximum_wave{}, peak_threads{};
        std::size_t resident_packets{}, resident_bytes{};
        // CodexAstraLocal: The coordinator publishes its controller snapshot
        // after joined work; main-thread reporting never reads live pool state.
        Common::Uberhar::AdaptiveParticipation::Statistics scheduling{};
    };
    // CodexAstraLocal: Reserve the emulator owner while sizing the independent
    // CPU coordinator/pool from allowed logical processors. This is a capacity
    // bound, not an assumption that SMT siblings or hybrid cores are equivalent.
    explicit CpuDrawExecutor(unsigned processors = Common::Uberhar::AvailableProcessors(),
                             std::size_t max_packets = 64,
                             std::size_t max_bytes = 8 * 1024 * 1024,
                             void (*worker_start)() = nullptr,
                             CpuDrawPolicy policy = {});
    ~CpuDrawExecutor();
    CpuDrawExecutor(const CpuDrawExecutor&) = delete;
    CpuDrawExecutor& operator=(const CpuDrawExecutor&) = delete;

    // CodexAstraLocal: Owner-only policy before uniform/default/raw capture.
    // Success is only an upper-bound opportunity; Capture checks exact FIFO
    // misses/copies and Submit rechecks the still-pending group atomically.
    // A refusal counts one capture/refusal; success is counted by Capture only.
    bool AllowsShaderWork(const ShaderRunLease& lease, u16 booleans, u32 input_count);
    // CodexAstraLocal: Owner-only, no analysis/allocation on a miss. The caller
    // must first validate its current lease/revision/entry; unknown keeps setup.
    std::optional<bool> CachedShaderWork(const ShaderRunLease& lease, u16 booleans, u32 input_count);

    // CodexAstraLocal: Capture performs exact effect/flow proof and all owned
    // allocation before a task is published. Refusal returns empty; allocation
    // exceptions retain the caller's existing before-dispatch recovery path.
    // CodexAstraLocal: Work-qualified seeds pay 96*35 actual FIFO arithmetic
    // units; the separate expanded_seeds control additionally admits below96
    // actual misses. Ordinary standalone seeds retain>=96misses. Joining members pay at least
    // 12*35 units and belong to a certified1–4-member contiguous cost grain;
    // every extra member adds12*35 units to the3360 dispatch threshold. These are
    // conservative policy proxies, not measured capture costs. Copies stay<=70B/miss.
    std::shared_ptr<CpuDrawPacket> Capture(const CpuDrawCapture& source,
                                         CpuDrawHardwareWriter writer,
                                         u32 hardware_stride = 88);
    // CodexAstraLocal: Owner-only refusal reason distinguishes unsupported work
    // from credit held by earlier commands; only the latter needs queue drainage.
    bool LastCaptureAtCapacity() const noexcept;
    // CodexAstraLocal: Successful Submit guarantees independent progress before
    // a Vulkan Record may dispatch its waiting command. Capture already owns
    // byte/count credit; false also covers a small packet whose pending group
    // disappeared. No task is published on false, so the owner can run serially.
    bool Submit(const std::shared_ptr<CpuDrawPacket>& packet);
    void Drain();
    // CodexAstraLocal: Owner-only; diagnostic fields merge without adding
    // rejection-path synchronization. Callers may not race capture/submit.
    Statistics GetStatistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Pica

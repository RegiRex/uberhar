// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <memory>
#include <optional> // CodexAstraLocal: Cached-only eligibility can be unknown.
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
        // CodexAstraLocal TEST-ONLY LOG: Owner-only CPU preparation/startup evidence;
        // remove after the isolated .48 CPU comparison is qualified on hardware.
        u64 cached_checks{}, cached_hits{}, cached_misses{}, cached_negative{};
        u64 effect_hits{}, effect_builds{}, effect_reuses{}, token_missing{}, token_mismatches{};
        u64 start_requests{}, start_refused{}, start_failed{}, async_starts{};
        u64 lookup_samples{}, lookup_ns{}, eligibility_samples{}, eligibility_ns{};
        u64 proof_samples{}, proof_ns{};
        u64 work_refused{};
        // CodexAstraLocal: Separate preparation avoidance, real grouped
        // admissions, and lost pre-publication opportunities from throughput.
        u64 early_refused{}, group_candidates{}, seed_submitted{}, group_submitted{};
        u64 group_lost{}, copy_refused{}, grouped_waves{}, work_grains{};
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
                             void (*worker_start)() = nullptr, bool sampled_costs = false);
    ~CpuDrawExecutor();
    CpuDrawExecutor(const CpuDrawExecutor&) = delete;
    CpuDrawExecutor& operator=(const CpuDrawExecutor&) = delete;

    // CodexAstraLocal: Owner-only policy before uniform/default/raw capture.
    // Success is only an upper-bound opportunity; Capture checks exact FIFO
    // misses/copies and Submit rechecks the still-pending group atomically.
    // A refusal counts one capture/refusal; success is counted by Capture only.
    bool AllowsShaderWork(const ShaderRunLease& lease, u16 booleans, u32 input_count);
    // CodexAstraLocal: Reuse a retained exact proof before redundant owner setup;
    // unknown code preserves the .48 setup path and every real capture is rechecked.
    std::optional<bool> CachedShaderWork(const ShaderRunLease& lease, u16 booleans, u32 input_count);
    // CodexAstraLocal: Advisory only, before small-draw preparation/allocation.
    // A stale false merely misses an opportunity; locked admission stays final.
    bool HasPendingSeed() const noexcept;

    // CodexAstraLocal: Capture performs exact effect/flow proof and all owned
    // allocation before a task is published. Refusal returns empty; allocation
    // exceptions retain the caller's existing before-dispatch recovery path.
    // Individual seeds retain 96 misses/35 guaranteed arithmetic operations and
    // <=70 raw bytes/miss. Smaller packets require an existing pending seed and
    // aggregate work >=96*35 per member; this is a work proxy, not target timing.
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
    // CodexAstraLocal: Called by the same owner as Capture/Submit/Drain; workers
    // retain locks for their own publication counters, never write owner counters.
    Statistics GetStatistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Pica

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <memory>
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
        u64 submitted{}, completed{}, failed{}, waves{}, coordinator_packets{}, auxiliary_packets{};
        u32 maximum_wave{}, peak_threads{};
        std::size_t resident_packets{}, resident_bytes{};
    };
    // CodexAstraLocal: Reserve the emulator owner while sizing the independent
    // CPU coordinator/pool from allowed logical processors. This is a capacity
    // bound, not an assumption that SMT siblings or hybrid cores are equivalent.
    explicit CpuDrawExecutor(unsigned processors = Common::Uberhar::AvailableProcessors(),
                             std::size_t max_packets = 64,
                             std::size_t max_bytes = 8 * 1024 * 1024,
                             void (*worker_start)() = nullptr);
    ~CpuDrawExecutor();
    CpuDrawExecutor(const CpuDrawExecutor&) = delete;
    CpuDrawExecutor& operator=(const CpuDrawExecutor&) = delete;

    // CodexAstraLocal: Capture performs exact effect/flow proof and all owned
    // allocation before a task is published. Refusal returns empty; allocation
    // exceptions retain the caller's existing before-dispatch recovery path.
    // The first cost-qualified policy requires 96 original misses, 35 arithmetic
    // instructions on every path, and at most 70 captured raw bytes per miss.
    std::shared_ptr<CpuDrawPacket> Capture(const CpuDrawCapture& source,
                                         CpuDrawHardwareWriter writer,
                                         u32 hardware_stride = 88);
    // CodexAstraLocal: Owner-only refusal reason distinguishes unsupported work
    // from credit held by earlier commands; only the latter needs queue drainage.
    bool LastCaptureAtCapacity() const noexcept;
    // CodexAstraLocal: Successful Submit guarantees independent progress before
    // a Vulkan Record may dispatch its waiting command. Capture already owns
    // byte/count credit; false means an unsubmitted lifecycle/duplicate refusal.
    bool Submit(const std::shared_ptr<CpuDrawPacket>& packet);
    void Drain();
    Statistics GetStatistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Pica

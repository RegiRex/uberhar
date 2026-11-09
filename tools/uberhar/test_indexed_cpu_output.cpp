// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
// CodexAstraLocal: Production helper/uploader/recording branch, with owned fake
// stream and delayed recording boundaries. No mirrored dedup implementation.
#include <array>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>
#define XXH_INLINE_ALL
#include <xxhash.h>
#include "video_core/uberhar_indexed_cpu_output.h"

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
#define ASSERT(x) assert(x)
using VideoCore::IndexedCpuOutput;
namespace Common {
u64 ComputeHash64(const void* p, std::size_t n) { return XXH3_64bits(p, n); }
}
namespace Pica {
struct PipelineRegs {
    enum class UseGS { No, Yes };
    enum class TriangleTopology { List, Strip };
    UseGS use_gs = UseGS::No;
    TriangleTopology triangle_topology = TriangleTopology::List;
};
}
using TriangleTopology = Pica::PipelineRegs::TriangleTopology;
struct RasterizerAccelerated {
    struct HardwareVertex { std::array<u8, 88> data{}; };
    #include "actual_types.inc"
    std::vector<HardwareVertex> vertex_batch;
    IndexedCpuOutput indexed_cpu_output;
    IndexedOutputStats indexed_output_stats;
    CpuOutputUpload UploadCpuOutput(std::span<u8>, bool);
};
#include "actual_method.inc"

using Vertex = RasterizerAccelerated::HardwareVertex;
std::span<const u8> Bytes(const std::vector<Vertex>& input) {
    return {reinterpret_cast<const u8*>(input.data()), input.size() * sizeof(Vertex)};
}
auto Hash = [](const void* p, std::size_t n) { return Common::ComputeHash64(p, n); };
std::vector<Vertex> Mesh(u32 count, u32 unique = 12) {
    std::vector<Vertex> result(count);
    for (u32 i = 0; i < count; ++i) {
        u32 key = i % unique;
        for (u32 k = 0; k < 22; ++k) {
            const u32 value = key * 7919U + k * 17U;
            std::memcpy(result[i].data.data() + k * 4, &value, 4);
        }
    }
    return result;
}
void Reconstruct(const std::vector<Vertex>& original, const std::vector<u8>& upload,
                 const IndexedCpuOutput::Plan& plan) {
    assert(plan && plan.upload_bytes <= original.size() * 88);
    assert(plan.index_offset % alignof(u16) == 0);
    for (u32 i = 0; i < original.size(); ++i) {
        u16 index;
        std::memcpy(&index, upload.data() + plan.index_offset + i * 2, 2);
        assert(index < plan.unique_vertices);
        assert(std::memcmp(original[i].data.data(), upload.data() + index * 88, 88) == 0);
    }
}

struct Recorded {
    bool indexed = false;
    u32 count = 0, vertex_offset = 0, index_offset = 0;
};
namespace vk {
enum class IndexType { eUint16 };
struct CommandBuffer {
    Recorded* result;
    void bindVertexBuffers(int, int, u32 offset) { result->vertex_offset = offset; }
    void bindIndexBuffer(int, u32 offset, IndexType) { assert(offset % 2 == 0); result->index_offset = offset; }
    void draw(u32 count, int instances, int first, int instance) {
        assert(instances == 1 && first == 0 && instance == 0);
        result->count = count;
    }
    void drawIndexed(u32 count, int instances, int first, int offset, int instance) {
        assert(instances == 1 && first == 0 && offset == 0 && instance == 0);
        result->count = count; result->indexed = true;
    }
};
}
struct Stream {
    std::vector<u8> data;
    u32 mapped = 0, maps = 0, commits = 0, offset = 0, committed = 0;
    u32 tick = 1, stamped_tick = 0;
    explicit Stream(u32 capacity) : data(capacity, 0xcc) {}
    u8* Map(u32 bytes, u32 alignment) {
        assert(mapped == 0);
        ++maps;
        offset = ((offset + alignment - 1) / alignment) * alignment;
        if (offset + bytes > data.size()) offset = 0;
        assert(bytes <= data.size()); mapped = bytes;
        return data.data() + offset;
    }
    void Commit(u32 bytes) {
        assert(mapped && bytes <= mapped); ++commits;
        committed = bytes; mapped = 0; stamped_tick = tick;
    }
    int Handle() const { return 1; }
};
struct Scheduler {
    std::vector<std::function<void(vk::CommandBuffer)>> commands;
    template <typename F> void Record(F fn) { commands.push_back(std::move(fn)); }
};
struct Renderer : RasterizerAccelerated {
    bool indexed_cpu_output_enabled = true;
    struct { bool ready = false; } cpu_bridge;
    bool cpu_fragment_use = false, compute_packet = false, vertex_capture = false;
    bool ready_vertex_attempt = false;
    struct { Pica::PipelineRegs pipeline; } regs;
    Stream stream_buffer{65536};
    Scheduler scheduler;
    bool Upload() {
        const auto vertex_count = static_cast<u32>(vertex_batch.size());
        const auto vertex_size = vertex_count * 88;
        u8* vertex_data = stream_buffer.Map(vertex_size, 88);
        const auto vertex_offset = stream_buffer.offset;
        // Final pass/binding moved to another tick after the upper-bound Map.
        ++stream_buffer.tick;
        bool indexed_cpu_draw = false;
        const bool queued_worker_upload = false;
        #include "actual_upload.inc"
        return indexed_cpu_draw;
    }
    Recorded Execute() {
        Recorded result;
        for (auto& command : scheduler.commands) command(vk::CommandBuffer{&result});
        scheduler.commands.clear();
        return result;
    }
};

int main() {
    IndexedCpuOutput helper;
    auto input = Mesh(768);
    // Signed zero, opposite quaternion sign bits and NaN payloads are distinct
    // even when a semantic float comparison or position-only key might merge.
    for (u32 i = 0; i < input.size(); ++i) {
        const u32 values[] = {0, 0x80000000U, 0x7fc00001U, 0x7fc00002U};
        std::memcpy(input[i].data.data() + 52, &values[i % 4], 4);
    }
    auto plan = helper.Prepare(Bytes(input), Hash);
    assert(plan && plan.unique_vertices == 12);
    std::vector<u8> upload(plan.upload_bytes + 8, 0xcd);
    assert(helper.Copy(Bytes(input), upload));
    Reconstruct(input, upload, plan);
    assert(upload[plan.upload_bytes] == 0xcd);
    auto other_source = input;
    assert(!helper.Copy(Bytes(other_source), upload));
    assert(!helper.Copy(Bytes(input), std::span<u8>{upload.data(), plan.upload_bytes - 1}));
    // A new generation must not reuse prior records/indices with changed bytes.
    for (auto& vertex : input) vertex.data[87] ^= 0x55;
    auto next = helper.Prepare(Bytes(input), Hash);
    assert(next && helper.Copy(Bytes(input), upload));
    Reconstruct(input, upload, next);
    assert(helper.Prepare(Bytes(Mesh(189)), Hash).decision == IndexedCpuOutput::Decision::Small);
    assert(helper.Prepare(Bytes(Mesh(32769)), Hash).decision == IndexedCpuOutput::Decision::Large);
    assert(helper.Prepare(Bytes(Mesh(193)), Hash).decision == IndexedCpuOutput::Decision::Shape);
    auto no_reuse = Mesh(768, 768);
    plan = helper.Prepare(Bytes(no_reuse), Hash);
    assert(plan.decision == IndexedCpuOutput::Decision::PrefixReuse && plan.hashed_vertices == 48);
    // Good prefix alone cannot admit a no-reuse tail or negative transfer gain.
    auto bad_tail = Mesh(768, 768);
    std::copy_n(input.begin(), 48, bad_tail.begin());
    assert(helper.Prepare(Bytes(bad_tail), Hash).decision == IndexedCpuOutput::Decision::Savings);
    auto collide = [](const void*, std::size_t) { return u64{0}; };
    plan = helper.Prepare(Bytes(input), collide);
    assert(plan && helper.Copy(Bytes(input), upload));
    Reconstruct(input, upload, plan);
    auto overflow = Mesh(768, 768);
    std::copy_n(input.begin(), 48, overflow.begin());
    assert(helper.Prepare(Bytes(overflow), collide).decision == IndexedCpuOutput::Decision::ProbeBudget);
    auto large = Mesh(IndexedCpuOutput::MaxVertices);
    plan = helper.Prepare(Bytes(large), Hash);
    assert(plan && helper.ScratchBytes() <= 1245184);
    upload.resize(plan.upload_bytes);
    assert(helper.Copy(Bytes(large), upload)); Reconstruct(large, upload, plan);
    // The exact source-extracted Vulkan branch records only GPU offsets. Clearing
    // and destroying CPU vertices before command execution cannot alter its draw.
    for (int exclusion = 0; exclusion < 9; ++exclusion) {
        Renderer renderer;
        renderer.vertex_batch = input;
        renderer.stream_buffer.offset = 65000; // Exercise one wrap before final tick.
        if (exclusion == 1) renderer.indexed_cpu_output_enabled = false;
        if (exclusion == 2) renderer.cpu_bridge.ready = true;
        if (exclusion == 3) renderer.cpu_fragment_use = true;
        if (exclusion == 4) renderer.compute_packet = true;
        if (exclusion == 5) renderer.vertex_capture = true;
        if (exclusion == 6) renderer.ready_vertex_attempt = true;
        if (exclusion == 7) renderer.regs.pipeline.use_gs = Pica::PipelineRegs::UseGS::Yes;
        if (exclusion == 8) renderer.regs.pipeline.triangle_topology = TriangleTopology::Strip;
        // This mock stream deliberately accommodates the full expanded upper bound.
        renderer.stream_buffer.data.resize(131072, 0xcc);
        renderer.stream_buffer.offset = 130900;
        const bool indexed = renderer.Upload();
        assert(indexed == (exclusion == 0 || exclusion == 3));
        assert(renderer.stream_buffer.maps == 1 && renderer.stream_buffer.commits == 1);
        assert(renderer.stream_buffer.stamped_tick == renderer.stream_buffer.tick);
        renderer.vertex_batch.clear(); renderer.vertex_batch.shrink_to_fit();
        const auto command = renderer.Execute();
        assert(command.indexed == indexed && command.count == input.size());
        if (indexed) {
            IndexedCpuOutput::Plan actual;
            actual.decision = IndexedCpuOutput::Decision::Accepted;
            actual.unique_vertices = (command.index_offset - command.vertex_offset) / 88;
            actual.upload_bytes = renderer.stream_buffer.committed;
            actual.index_offset = command.index_offset;
            Reconstruct(input, renderer.stream_buffer.data, actual);
            assert(renderer.indexed_output_stats.accepted_samples == 1);
        } else {
            assert(std::memcmp(renderer.stream_buffer.data.data() + command.vertex_offset,
                               input.data(), input.size() * 88) == 0);
            assert(renderer.indexed_cpu_output.ScratchBytes() == 0);
        }
    }
    std::cout << "PASS exact bytes/order, quaternion/NaN/signed-zero identity, bounded refusal/collision, "
                 "generation reuse, one Map/Commit, final-tick lifetime, ready-fragment ABI and six exclusions\n";
}

// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/vector_math.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "video_core/uberhar_indexed_cpu_output.h"
#include "video_core/uberhar_owned_vertex_upload.h"

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore {

class RasterizerAccelerated : public RasterizerInterface {
public:
    explicit RasterizerAccelerated(Memory::MemorySystem& memory, Pica::PicaCore& pica);
    virtual ~RasterizerAccelerated() = default;

    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;

    // CodexAstraLocal: Reserve a conservative append bound before worker
    // publication and restore only logical size on a later shader failure.
    void AppendOrderedVertices(Pica::PrimitiveAssembler& assembler,
                               const Pica::OrderedVertexView& vertices) override;
    bool BeginVertexAppend(u32 inputs, std::size_t& checkpoint) override;
    void RollbackVertexAppend(std::size_t checkpoint) noexcept override;

    // CodexAstraLocal: Pure complete-list conversion shares the original
    // quaternion correction and exact HardwareVertex constructor with AddTriangle.
    // Callers preallocate precisely 88 bytes per vertex; no renderer is borrowed.
    static void WriteDeferredTriangles(std::span<const Pica::OutputVertex> vertices,
                                       std::span<u8> destination) noexcept;

    // CodexAstraLocal: This sink only packs vertices into its owned batch during
    // AddTriangle; guest framebuffer/cache writes occur after loading joins.
    bool DefersGuestMemoryWritesUntilDraw() const override {
        return true;
    }

protected:
    /// Sync vertex and framgent uniforms from PICA registers
    void SyncDrawUniforms();

protected:
    /// Structure that the hardware rendered vertices are composed of
    struct HardwareVertex {
        HardwareVertex() = default;
        HardwareVertex(const Pica::OutputVertex& v, bool flip_quaternion);

        Common::Vec4f position;
        Common::Vec4f color;
        Common::Vec2f tex_coord0;
        Common::Vec2f tex_coord1;
        Common::Vec2f tex_coord2;
        float tex_coord0_w;
        Common::Vec4f normquat;
        Common::Vec3f view;
    };

    struct VertexArrayInfo {
        u32 vs_input_index_min;
        u32 vs_input_index_max;
        u32 vs_input_size;

        bool Invalid() const {
            return vs_input_index_min == 0 && vs_input_index_max == 0 && vs_input_size == 0;
        }
    };

    /// Retrieve the range and the size of the input vertex
    VertexArrayInfo AnalyzeVertexArray(bool is_indexed, u32 stride_alignment = 1);

    // CodexAstraLocal: Ordinary Vulkan CPU output only; the expanded batch stays
    // intact for every other consumer. OFF bypasses all keying and scratch work.
    struct CpuOutputUpload {
        u32 bytes;
        u32 index_offset;
        bool indexed;
    };
    CpuOutputUpload UploadCpuOutput(std::span<u8> destination, bool eligible);
    void ReportIndexedCpuOutput(bool final);

protected:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    Pica::RegsInternal& regs;
    std::vector<HardwareVertex> vertex_batch;
    const bool indexed_cpu_output_enabled;
    const bool worker_cpu_upload_enabled;
    OwnedVertexUpload<HardwareVertex> owned_vertex_upload;
    u64 worker_upload_attempts{};
    u64 worker_upload_owner_samples{}, worker_upload_owner_ns{};
    IndexedCpuOutput indexed_cpu_output;
    // CodexAstraLocal TEST-ONLY LOG: Bounded counters and one inclusive key/copy
    // clock sample per 64 eligible uploads (plus first allocation). Remove after
    // indexed-output ON/OFF qualification; never interpret bytes as GPU timings.
    struct IndexedOutputStats {
        u64 ineligible = 0;
        u64 attempts = 0;
        std::array<u64, static_cast<std::size_t>(IndexedCpuOutput::Decision::Count)> decisions{};
        u64 input_vertices = 0;
        u64 unique_vertices = 0;
        u64 expanded_bytes = 0;
        u64 uploaded_bytes = 0;
        u64 hashed_vertices = 0;
        u64 probes = 0;
        u64 accepted_samples = 0;
        u64 accepted_ns = 0;
        u64 refused_samples = 0;
        u64 refused_ns = 0;
        u64 max_sample_ns = 0;
    } indexed_output_stats;
    Pica::Shader::UserConfig user_config{};
    Pica::Shader::Generator::VSUniformData vs_data{};
    Pica::Shader::Generator::FSUniformData fs_data{};
    bool vs_data_dirty = true;
    // AstraPro: Revisited pending CPU-side VS block; not a GPU time/error metric.
    u64 pending_vs_uniform_resyncs = 0;
    bool fs_data_dirty = true;
};

} // namespace VideoCore

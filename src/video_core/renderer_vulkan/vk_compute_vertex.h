// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/compute_assembly.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/renderer_vulkan/vk_compute_raster.h"

namespace Memory { class MemorySystem; }
namespace Pica { class PicaCore; }
namespace Vulkan {
// CodexAstraLocal: Snapshot original guest data, not converted CPU vertices.
// Full byte equality keys the mandatory translator; guest inputs are draw-owned.
struct ComputeVertexInput {
    static constexpr u32 MaximumInputs = 4096;
    static constexpr u32 MaximumRawBytes = 16 * 1024 * 1024;
    static constexpr u32 HeaderWords = 64; // Status, touched semantic pair and state.
    std::array<u32, 193> metadata{};
    std::vector<u8> raw;
    Pica::ProgramCode program;
    Pica::SwizzleData swizzles;
    bool sanitize_mul{};
};
ComputeVertexInput CaptureComputeVertexInput(
    Memory::MemorySystem& memory, const Pica::PicaCore& pica, bool indexed,
    bool sanitize_mul, const Pica::ComputeAssemblyState& assembly,
    const std::function<void(PAddr, u32)>& flush,
    const Pica::AttributeBuffer* immediate = nullptr);

// CodexAstraLocal: A producer owns its output through the final raster tick.
// The initial synchronous completion reads only status/count and the final
// assembler pair, never rendered vertices or pixels. The wait is real overhead.
class ComputeVertexProducer final {
public:
    struct Batch {
        ComputeRasterizer::GpuVertices vertices;
        u32 count{};
        Pica::ComputeAssemblyResult assembly;
    };
    ComputeVertexProducer(const Instance& instance, Scheduler& scheduler,
                          DescriptorUpdateQueue& updates);
    ~ComputeVertexProducer();
    Batch Produce(const ComputeVertexInput& input, bool disable_optimizer);
    void MarkRasterUse();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Vulkan

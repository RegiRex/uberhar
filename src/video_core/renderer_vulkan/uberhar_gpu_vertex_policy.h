// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <cstddef>
#include "common/common_types.h"
#include "video_core/pica/regs_pipeline.h"

namespace Vulkan::ReadyVertexPolicy {
// AstraPro: Automatic alone opts into this new experiment. Only complete lists
// with an empty CPU assembler are safe to move without inheriting strip/fan tail
// loss. Small batches remain on CPU; bound uploads and speculative cache growth.
constexpr u32 MinVertices = 96;
constexpr u32 MaxVertices = 65535;
constexpr u32 MaxUploadBytes = 4 * 1024 * 1024;
constexpr std::size_t MaxPrograms = 128;
constexpr std::size_t MaxPipelines = 256;
constexpr bool Eligible(bool automatic, bool debugging, bool assembler_empty, bool geometry,
                        Pica::PipelineRegs::TriangleTopology topology, u32 count) {
    return automatic && !debugging && assembler_empty && !geometry &&
           topology == Pica::PipelineRegs::TriangleTopology::List &&
           count >= MinVertices && count <= MaxVertices && count % 3 == 0;
}
// AstraPro: Only a completed, successful, exact-state pipeline can replace CPU
// execution. Pending/failed/mismatched handles always retain the original draw.
constexpr bool CanSelect(bool done, bool failed, u64 expected, u64 actual) {
    return done && !failed && expected == actual;
}
constexpr bool CanQueue(std::size_t resident, bool pending) {
    return resident < MaxPipelines && !pending;
}
} // namespace Vulkan::ReadyVertexPolicy

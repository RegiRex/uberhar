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
// AstraPro: Shader topology also assembles independent triples when no guest GS
// runs and no winding request is pending. IsEmpty alone does NOT prove winding
// is clear: an earlier GS emitter can leave a request for the next triangle.
// Keep the actual guest topology in the pipeline key; both host topologies map
// to triangle lists in pica_to_vk.h. Never admit strip/fan tails by analogy.
enum class Admission : u32 {
    List, ShaderList, Disabled, Debugger, Geometry, Assembly, Winding, Topology,
    TooSmall, TooLarge, Incomplete, Count
};
constexpr Admission Classify(bool automatic, bool debugging, bool assembler_empty, bool geometry,
                             Pica::PipelineRegs::TriangleTopology topology, u32 count,
                             bool pending_winding, bool topology_matches) {
    using Topology = Pica::PipelineRegs::TriangleTopology;
    if (!automatic) return Admission::Disabled;
    if (debugging) return Admission::Debugger;
    if (geometry) return Admission::Geometry;
    if (!assembler_empty) return Admission::Assembly;
    if (!topology_matches || (topology != Topology::List && topology != Topology::Shader))
        return Admission::Topology;
    if (topology == Topology::Shader && pending_winding) return Admission::Winding;
    if (count < MinVertices) return Admission::TooSmall;
    if (count > MaxVertices) return Admission::TooLarge;
    if (count % 3 != 0) return Admission::Incomplete;
    return topology == Topology::Shader ? Admission::ShaderList : Admission::List;
}
constexpr bool IsEligible(Admission admission) {
    return admission == Admission::List || admission == Admission::ShaderList;
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

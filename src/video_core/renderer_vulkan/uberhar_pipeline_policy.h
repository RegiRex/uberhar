// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Small shared policies for bounded CPU routing and failure-aware waits.
#pragma once
#include "common/async_handle.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/shader_recovery_error.h" // CodexAstraUlt-2: Terminal selection.

namespace Vulkan {

// AstraEH: Bound both CPU input work and expanded triangle output. The PICA core
// isolates admitted batches from persistent assembly state before loading vertices.
enum class CpuBridgeAdmission : u32 {
    Eligible,
    TooSmall,
    InputLimit,
    IncompleteList,
    OutputLimit,
    UnsupportedTopology,
    Count,
};

constexpr CpuBridgeAdmission CheckCpuBridgeAdmission(Pica::PipelineRegs::TriangleTopology topology,
                                                     u32 vertices) {
    using Topology = Pica::PipelineRegs::TriangleTopology;
    if (vertices < 3)
        return CpuBridgeAdmission::TooSmall;
    if (vertices > 4096)
        return CpuBridgeAdmission::InputLimit;
    switch (topology) {
    case Topology::List:
    case Topology::Shader:
        return vertices % 3 == 0 ? CpuBridgeAdmission::Eligible
                                 : CpuBridgeAdmission::IncompleteList;
    case Topology::Strip:
    case Topology::Fan:
        return (vertices - 2) * 3 <= 4096 ? CpuBridgeAdmission::Eligible
                                          : CpuBridgeAdmission::OutputLimit;
    default:
        return CpuBridgeAdmission::UnsupportedTopology;
    }
}

constexpr bool CpuBridgeEligible(Pica::PipelineRegs::TriangleTopology topology, u32 vertices) {
    return CheckCpuBridgeAdmission(topology, vertices) == CpuBridgeAdmission::Eligible;
}

template <typename Pipeline>
bool PipelineWaitRequired(const Pipeline& preferred, const Pipeline* alternative, bool force) {
    if (force && alternative) {
        return !alternative->IsDone() || (alternative->HasFailed() && !preferred.IsDone());
    }
    return (!preferred.IsDone() || preferred.HasFailed()) &&
           (!alternative || !alternative->IsDone() || alternative->HasFailed());
}

// AstraEH: Completion is distinct from usability. A failed experiment must wake
// waiters and select the accurate preferred pipeline, never a null Vulkan handle.
// The preferred path is the established specialized renderer (or a verified ready bridge).
template <typename Pipeline>
Pipeline* SelectUsablePipeline(Common::AsyncCompletion& completion, Pipeline& preferred,
                               Pipeline* alternative, bool force) {
    if (alternative) {
        if (force) {
            if (!alternative->IsDone())
                alternative->WaitDone();
        } else if (!preferred.IsDone() && !alternative->IsDone()) {
            completion.WaitAny(preferred, *alternative);
        }
        // CodexAstraUlt-2: Failed mandatory work may still use an exact fallback.
        if (preferred.IsDone() && preferred.HasFailed() && !alternative->IsDone())
            alternative->WaitDone();
        if (alternative->IsDone() && !alternative->HasFailed() &&
            (force || !preferred.IsDone() || preferred.HasFailed())) {
            return alternative;
        }
    }
    if (!preferred.IsDone())
        preferred.WaitDone();
    // CodexAstraUlt-2: Both routes failed; callers must terminate this stream.
    if (preferred.HasFailed())
        throw VideoCore::ShaderRecoveryError{};
    return &preferred;
}

} // namespace Vulkan

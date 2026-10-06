// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <cstddef>
#include "common/alignment.h"
#include "common/common_types.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica/regs_shader.h"

namespace Vulkan::ReadyVertexPolicy {
// CodexAstraUlt: Replace AstraPro's obsolete Automatic-only description: both Combo presets
// use the same existing guards. Only complete lists with an empty CPU assembler can move
// without strip/fan tail loss; small batches, uploads and speculative caches remain bounded.
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
// CodexAstraUlt: CPU triangle submission aligns opposite quaternion signs before
// interpolation. A lit GPU draw needs the enabled geometry or barycentric path
// to preserve that result; raw per-vertex quaternions are not equivalent.
constexpr bool CanPreserveQuaternionInterpolation(bool lighting_enabled, bool geometry_shader,
                                                  bool barycentric) {
    return !lighting_enabled || geometry_shader || barycentric;
}
// CodexAstraUlt-2: CPU input transport repeats an active zero-stride source,
// while the inherited GPU uploader omits it. Keep these draws on CPU until
// their input translation is equivalent; unused loader slots remain eligible.
inline bool HasActiveZeroStrideLoader(const Pica::PipelineRegs& pipeline) {
    for (const auto& loader : pipeline.vertex_attributes.attribute_loaders) {
        if (loader.component_count != 0 && loader.byte_count == 0)
            return true;
    }
    return false;
}
// CodexAstraUlt: Complement the CodexAstraUlt-2 zero-stride fallback with
// reproduced CPU/GPU input differences. The inherited uploader copies stride
// bytes, ignores default flags on loaded attributes, and resolves register aliases
// in loader order instead of ascending attribute order. Keep those draws on CPU;
// this classifier does not alter the inherited Custom-mode uploader.
enum class InputLayoutIssue : u32 { None, ShortStride, DefaultAttribute, RegisterAlias, Count };

constexpr const char* InputLayoutIssueName(InputLayoutIssue issue) {
    switch (issue) {
    case InputLayoutIssue::ShortStride: return "short_stride";
    case InputLayoutIssue::DefaultAttribute: return "default_attribute";
    case InputLayoutIssue::RegisterAlias: return "register_alias";
    default: return "none";
    }
}

inline InputLayoutIssue ClassifyInputLayout(const Pica::PipelineRegs& pipeline,
                                          const Pica::ShaderRegs& shader) {
    const u32 requested = shader.max_input_attribute_index + 1;
    u32 requested_registers = 0;
    for (u32 attribute = 0; attribute < requested; ++attribute) {
        const u32 bit = 1U << shader.GetRegisterForAttribute(attribute);
        if (requested_registers & bit)
            return InputLayoutIssue::RegisterAlias;
        requested_registers |= bit;
    }

    const auto& attributes = pipeline.vertex_attributes;
    for (const auto& loader : attributes.attribute_loaders) {
        if (loader.component_count == 0 || loader.byte_count == 0)
            continue; // CodexAstraUlt: Active zero strides retain their existing guard/accounting.
        u32 offset = 0;
        for (u32 component = 0; component < loader.component_count && component < 12; ++component) {
            const u32 attribute = loader.GetComponent(component);
            if (attribute >= 12) {
                offset = Common::AlignUp(offset, 4);
                offset += (attribute - 11) * 4;
                continue;
            }
            offset = Common::AlignUp(offset, attributes.GetElementSizeInBytes(attribute));
            offset += attributes.GetStride(attribute);
            if (offset > loader.byte_count)
                return InputLayoutIssue::ShortStride;
            if (attribute < requested && attributes.IsDefaultAttribute(attribute))
                return InputLayoutIssue::DefaultAttribute;
            // CodexAstraUlt: A loader outside the CPU's requested attribute range
            // can still overwrite a requested GPU register; it is also an alias.
            if (attribute >= requested &&
                (requested_registers & (1U << shader.GetRegisterForAttribute(attribute))))
                return InputLayoutIssue::RegisterAlias;
        }
    }
    return InputLayoutIssue::None;
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

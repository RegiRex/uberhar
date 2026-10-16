// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <string>
// CodexAstraLocal: Shared revision/masks bind generated variants and owner caches.
#include "common/calculated_features.h"
#include "video_core/pica/shader_setup.h"

namespace Pica::Shader::Generator::GLSL {
// CodexAstraLocal TEST-ONLY LOG: Fixed unpublished sideband stores the first
// nonfinite invocation's actual register values, never repaired guest output.
// Remove after the captured FEA tuple has a qualified correction/regression.
inline constexpr u32 ComputeGuestDiagnosticWords = 256;
// CodexAstraLocal: Keep the GPU's first clipping refusal and host explanation in
// one numbered contract. Failure operands reuse the unpublished 64-word header;
// status/tail contract and 88-byte stream stay unchanged; diagnostics may use
// the reserved success-only words60..63 after every failure exit.
enum class ComputeClipFailure : u32 {
    None,
    PlaneDistance,
    PolygonCapacity,
    IntersectionParameter,
    InterpolatedField,
    CustomDistance,
    NonpositiveW,
    Reciprocal,
    OutsidePlane,
    OutputCapacity,
    LiveSemantic,
};

// CodexAstraLocal: Stage one performs original byte loads, FIFO-ordered guest
// invocations and semantic mapping. Stage two consumes only a complete semantic
// stream and publishes ordered assembly/clipping. Neither stage has a fallback.
std::string GenerateComputeGuestVertex(const ProgramCode& program, const SwizzleData& swizzles,
                                       u32 entry, bool sanitize_mul);
// CodexAstraLocal: Explicit inputs allow independent feature-off comparisons;
// the renderer passes its immutable session snapshot, never live mutable settings.
std::string GenerateComputeAssembly(u32 feature_mask=CalculatedFeatures::Default,
                                    bool diagnostics=false);
} // namespace Pica::Shader::Generator::GLSL

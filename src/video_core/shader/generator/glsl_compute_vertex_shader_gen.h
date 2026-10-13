// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <string>
#include "video_core/pica/shader_setup.h"

namespace Pica::Shader::Generator::GLSL {
// CodexAstraLocal: Keep the GPU's first clipping refusal and host explanation in
// one numbered contract. Failure operands reuse the unpublished 64-word header;
// successful status/tail bytes and the 88-byte consumer stream are unchanged.
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
};

// CodexAstraLocal: Original byte loads, FIFO-ordered guest shader invocations,
// semantic mapping, quaternion fixup and homogeneous clipping all run in this
// compute module. An empty translation is a terminal unsupported program.
std::string GenerateComputeGuestVertex(const ProgramCode& program, const SwizzleData& swizzles,
                                       u32 entry, bool sanitize_mul);
} // namespace Pica::Shader::Generator::GLSL

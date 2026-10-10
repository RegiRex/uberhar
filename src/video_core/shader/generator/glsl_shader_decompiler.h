// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <string>
#include "video_core/pica/shader_setup.h"

namespace Pica::Shader::Generator::GLSL {

using RegGetter = std::function<std::string(u32)>;

std::string DecompileProgram(const Pica::ProgramCode& program_code,
                             const Pica::SwizzleData& swizzle_data, u32 main_offset,
                             const RegGetter& inputreg_getter, const RegGetter& outputreg_getter,
                             // CodexAstraLocal: The optional policy affects DP4/DPH only.
                             bool sanitize_mul, bool precise_jit_dot = false,
                             // CodexAstraLocal: The independent compute producer
                             // additionally uses the retained A64 DP3/RSQ tree;
                             // existing graphics callers retain their source.
                             bool compute_a64_math = false);

} // namespace Pica::Shader::Generator::GLSL

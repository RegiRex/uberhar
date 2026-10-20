// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <string_view>

namespace Vulkan::ComputeShadow {

// CodexAstraLocal: Preserve the production GLSL WriteShadow/UpdateShadow
// arithmetic on its native R32 word (depth in the high 24 bits, shade in the
// low 8). The caller owns this pixel exclusively and applies primitives in
// guest order, so the graphics path's CAS/interlock is unnecessary here.
// This is deliberately not the Software renderer's intermediate f16 formula.
// Inputs have the same contract as the graphics shader: finite depth, final
// TEV green in [0,1], and representable nonnegative float-to-uint conversions.
// Do not replace truncation with UNORM rounding or clamp the bias quotient;
// neither transformation is present in the existing GLSL shadow update.
inline constexpr std::string_view KernelFunctions = R"(
uint ShadowDepth(float depth) {
    return uint(clamp(depth, 0.0, 1.0) * float(0xFFFFFF));
}

uint ShadowShade(float green) {
    return uint(green * float(0xFF));
}

uint UpdateShadow(uint pixel, uint d, uint s, float bias_constant, float bias_linear) {
    uvec2 ref = uvec2(pixel >> 8, pixel & 0xFFu);
    if (d < ref.x) {
        if (s == 0u) {
            ref.x = d;
        } else {
            s = uint(float(s) / (bias_constant + bias_linear * float(d) / float(ref.x)));
            ref.y = min(s, ref.y);
        }
    }
    return (ref.x << 8) | ref.y;
}
)";

} // namespace Vulkan::ComputeShadow

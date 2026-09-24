// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <string_view>

namespace Vulkan {
// AstraEH: One immutable compute program writes all admitted rectangle states.
// Bounds/color are data; no per-draw shader or pipeline is generated. Each pixel
// has a single owner, so ordered submissions and barriers need no pixel atomics.
inline constexpr std::string_view ComputeRectShader = R"glsl(#version 450
layout(local_size_x=8, local_size_y=8) in;
layout(set=0, binding=0, r32ui) uniform writeonly uimage2D target_image;
layout(push_constant) uniform RectState {
    ivec4 rect;
    uint color;
} state;
void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, state.rect.zw))) return;
    ivec2 pixel = state.rect.xy + local;
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, imageSize(target_image)))) return;
    imageStore(target_image, pixel, uvec4(state.color, 0, 0, 0));
}
)glsl";
} // namespace Vulkan

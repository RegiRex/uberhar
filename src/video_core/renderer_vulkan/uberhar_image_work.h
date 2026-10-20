// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <cstdint>

namespace Vulkan::ImageWork {
// CodexAstraLocal: A smaller scissored draw can retain an existing non-clear
// graphics pass only inside its ORIGINAL render area. Widened arithmetic avoids
// wraparound authorizing out-of-area rendering; callers retain attachment and
// pass-kind identity, clear semantics, and the current dynamic scissor.
constexpr bool ContainsArea(std::int32_t outer_x, std::int32_t outer_y,
                            std::uint32_t outer_w, std::uint32_t outer_h,
                            std::int32_t inner_x, std::int32_t inner_y,
                            std::uint32_t inner_w, std::uint32_t inner_h) {
    return outer_x >= 0 && outer_y >= 0 && inner_x >= outer_x && inner_y >= outer_y &&
           outer_w && outer_h && inner_w && inner_h &&
           std::int64_t{inner_x} + inner_w <= std::int64_t{outer_x} + outer_w &&
           std::int64_t{inner_y} + inner_h <= std::int64_t{outer_y} + outer_h;
}

// CodexAstraLocal: A positive, equally sized, unflipped pair preserves exact
// texel correspondence. The caller additionally requires separate non-custom
// RGBA8 images and valid mip/layer ranges; other blits keep their old path.
constexpr bool EqualPositiveRegions(std::uint32_t sl, std::uint32_t sb,
                                    std::uint32_t sr, std::uint32_t st,
                                    std::uint32_t dl, std::uint32_t db,
                                    std::uint32_t dr, std::uint32_t dt) {
    return sr > sl && st > sb && dr > dl && dt > db && sr - sl == dr - dl &&
           st - sb == dt - db;
}
} // namespace Vulkan::ImageWork

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <algorithm>
#include <array>
#include "common/common_types.h"
namespace Vulkan {
// CodexAstraLocal: Dispatch only the clipped active rectangle while every shader
// invocation retains absolute pixel/quad coordinates and ordered primitive visits.
// Work counts are scheduling bounds, never measured GPU savings or bandwidth.
struct ComputeRasterDispatch {
    std::array<u32,2> groups;
    u64 full_groups{},active_pixels{};
};
inline ComputeRasterDispatch SelectComputeRasterDispatch(
    const std::array<u32,4>& extent,const std::array<s32,4>& rectangle,bool tiled) {
    const u64 full_x=(u64{extent[0]}+7)/8,full_y=(u64{extent[1]}+7)/8;
    const s64 left=std::clamp<s64>(rectangle[0],0,extent[0]);
    const s64 bottom=std::clamp<s64>(rectangle[1],0,extent[1]);
    const s64 right=std::clamp<s64>(rectangle[2],0,extent[0]);
    const s64 top=std::clamp<s64>(rectangle[3],0,extent[1]);
    const u64 width=std::max<s64>(0,right-left),height=std::max<s64>(0,top-bottom);
    return {{{static_cast<u32>(tiled?(width+7)/8:full_x),
              static_cast<u32>(tiled?(height+7)/8:full_y)}},full_x*full_y,width*height};
}
} // namespace Vulkan

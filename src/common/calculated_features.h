// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include "common/common_types.h"
namespace CalculatedFeatures {
// CodexAstraLocal: Stable diagnostic identities; output-sensitive variants are
// immutable for one renderer lifetime. Color remains refused until exact parity.
inline constexpr u32 AssemblyStorage=1u<<0, InsideTriangle=1u<<1, StructuralRetry=1u<<2,
                     ClipStorage=1u<<3, PrimaryColor=1u<<4, RasterTiles=1u<<5;
inline constexpr u32 Revision=3;
inline constexpr u32 Implemented=AssemblyStorage|InsideTriangle|StructuralRetry|ClipStorage|RasterTiles;
inline constexpr u32 Default=Implemented;
constexpr u32 Effective(u32 requested) {
    u32 result=requested&Implemented;
    if(!(result&AssemblyStorage))result&=~ClipStorage;
    return result;
}
} // namespace CalculatedFeatures

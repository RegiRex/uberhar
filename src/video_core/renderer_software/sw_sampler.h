// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <span>
#include "common/settings.h"
#include "video_core/texture/texture_decode.h"

namespace SwRenderer {

// CodexAstraLocal: One checked, value-only mip layout is shared by all samples
// of a triangle. Its byte extent includes every reachable level for feedback
// admission; texture contents remain live guest bytes in serial feedback mode.
struct TextureLayout {
    std::array<Pica::Texture::TextureInfo, 16> levels{};
    std::array<std::size_t, 16> offsets{};
    u32 max_level{};
    std::size_t bytes{};
};
TextureLayout MakeTextureLayout(const Pica::TexturingRegs::TextureConfig& config,
                                Pica::TexturingRegs::TextureFormat format, bool shadow);

// CodexAstraLocal: Filtering happens before the final byte rounding. Shadow
// taps compare packed depth individually and interpolate only their densities.
Common::Vec4<u8> SampleTexture(std::span<const u8> data, const TextureLayout& layout,
                               const Pica::TexturingRegs::TextureConfig& config,
                               float u, float v, float lod,
                               Settings::TextureSampling sampling);
Common::Vec4<u8> SampleShadow(std::span<const u8> data, const TextureLayout& layout,
                              float u, float v, u32 depth, bool cube);
Common::Vec4<u8> SampleCubeTexture(const std::array<std::span<const u8>, 6>& faces,
                                   const TextureLayout& layout,
                                   const Pica::TexturingRegs::TextureConfig& config,
                                   u32 face, float u, float v, float lod,
                                   Settings::TextureSampling sampling);

// CodexAstraLocal: Quad derivatives use the same maximum-component footprint
// as the existing shader generator; this is not a hardware-precision promise.
float TextureLod(const std::array<Common::Vec2f, 4>& quad, u32 lane,
                 u32 width, u32 height);

} // namespace SwRenderer

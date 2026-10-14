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
    // CodexAstraLocal: Every mip uses this format-sized tile; avoid resolving
    // the format again at each memoized tap.
    std::size_t tile_bytes{};
};
TextureLayout MakeTextureLayout(const Pica::TexturingRegs::TextureConfig& config,
                                Pica::TexturingRegs::TextureFormat format, bool shadow);

// CodexAstraLocal: Mapped nonaliasing bytes are not immutable while DSP/RPC or
// asynchronous guest writes can run. Both owner-side proofs are required.
constexpr bool CanMemoizeTexels(bool nonaliasing, bool concurrent_writers) {
    return nonaliasing && !concurrent_writers;
}

// CodexAstraLocal: One joined row group owns this bounded lazy memo. It reads
// only requested texels through the original decoder, never retains data across
// groups/draws and is bypassed for live feedback/concurrent writers.
class TexelMemo {
public:
    // CodexAstraLocal: Initialize keys/validity only, not all 512 unused colors.
    TexelMemo() {}
    TexelMemo(const TexelMemo&) = delete;
    TexelMemo& operator=(const TexelMemo&) = delete;
    Common::Vec4<u8> Read(const u8* source, const Pica::Texture::TextureInfo& info,
                         std::size_t tile_bytes, u32 x, u32 y);

private:
    struct Entry {
        const u8* tile{};
        Pica::TexturingRegs::TextureFormat format{};
        bool shadow{};
        u64 valid{};
        // Unrequested entries are deliberately not initialized or decoded.
        std::array<Common::Vec4<u8>, 64> colors;
    };
    std::array<Entry, 8> entries;
};
static_assert(sizeof(TexelMemo) <= 3 * 1024); // CodexAstraLocal: Fixed per-group stack budget.

// CodexAstraLocal: Filtering happens before the final byte rounding. Shadow
// taps compare packed depth individually and interpolate only their densities.
Common::Vec4<u8> SampleTexture(std::span<const u8> data, const TextureLayout& layout,
                               const Pica::TexturingRegs::TextureConfig& config,
                               float u, float v, float lod,
                               Settings::TextureSampling sampling, TexelMemo* memo = nullptr);
Common::Vec4<u8> SampleShadow(std::span<const u8> data, const TextureLayout& layout,
                              float u, float v, u32 depth, bool cube, TexelMemo* memo = nullptr);
Common::Vec4<u8> SampleCubeTexture(const std::array<std::span<const u8>, 6>& faces,
                                   const TextureLayout& layout,
                                   const Pica::TexturingRegs::TextureConfig& config,
                                   u32 face, float u, float v, float lod,
                                   Settings::TextureSampling sampling, TexelMemo* memo = nullptr);

// CodexAstraLocal: Quad derivatives use the same maximum-component footprint
// as the existing shader generator; this is not a hardware-precision promise.
float TextureLod(const std::array<Common::Vec2f, 4>& quad, u32 lane,
                 u32 width, u32 height);

} // namespace SwRenderer

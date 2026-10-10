// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#include "video_core/renderer_software/sw_sampler.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "video_core/renderer_software/sw_texturing.h"
#include "video_core/shader_recovery_error.h"

namespace SwRenderer {
using Config = Pica::TexturingRegs::TextureConfig;
using Format = Pica::TexturingRegs::TextureFormat;
namespace {
// CodexAstraLocal: Validate coordinates before float-to-integer conversion;
// invalid shader coordinates become a contained error, never an invalid read.
s32 FloorCoordinate(float value) {
    constexpr float limit = static_cast<float>(std::numeric_limits<s32>::max() / 2);
    if (!std::isfinite(value) || value < -limit || value > limit) {
        throw VideoCore::ShaderRecoveryError{"CPU Software texture coordinate is not finite/bounded"};
    }
    return static_cast<s32>(std::floor(value));
}
Common::Vec4<u8> RoundColor(const Common::Vec4f& color) {
    Common::Vec4<u8> result;
    for (u32 c = 0; c < 4; ++c)
        result[c] = static_cast<u8>(std::clamp(std::round(color[c]), 0.f, 255.f));
    return result;
}
Common::Vec4f Mix(const Common::Vec4f& a, const Common::Vec4f& b, float f) {
    return a + (b - a) * f;
}
bool Border(Config::WrapMode mode, s32 coordinate, u32 size) {
    return (mode == Config::ClampToBorder && coordinate < 0) ||
           ((mode == Config::ClampToBorder || mode == Config::ClampToBorder2) &&
            coordinate >= static_cast<s32>(size));
}
Common::Vec4f SampleLevel(std::span<const u8> data, const TextureLayout& layout,
                         const Config& config, u32 level, float u, float v, bool linear,
                         const std::array<std::span<const u8>, 6>* faces, u32 face) {
    const auto& info = layout.levels[level];
    // CodexAstraLocal: Ordinary cubemap filtering carries taps across the
    // selected face's edges. Three incident corner texels share a corner tap;
    // shadow cubemaps deliberately use their separate clamped-face PCF rule.
    const auto cube_tap = [&](s32 x, s32 y) {
        const float s = 2.f * (x + .5f) / info.width - 1.f;
        const float t = 2.f * (y + .5f) / info.height - 1.f;
        Common::Vec3f direction;
        switch (face) {
        case 0: direction = {1.f, -t, -s}; break;
        case 1: direction = {-1.f, -t, s}; break;
        case 2: direction = {s, 1.f, t}; break;
        case 3: direction = {s, -1.f, -t}; break;
        case 4: direction = {s, -t, 1.f}; break;
        default: direction = {-s, -t, -1.f}; break;
        }
        const float a = direction.x, b = direction.y, c = direction.z;
        u32 selected;
        float px, py, z;
        if (std::abs(a) > std::abs(b) && std::abs(a) > std::abs(c)) {
            selected = a > 0 ? 0 : 1; px = -c; py = a > 0 ? -b : b; z = a;
        } else if (std::abs(b) > std::abs(c)) {
            selected = b > 0 ? 2 : 3; px = b > 0 ? a : -a; py = c; z = b;
        } else {
            selected = c > 0 ? 4 : 5; px = a; py = c > 0 ? -b : b; z = c;
        }
        const auto tx = std::clamp(FloorCoordinate((px / z * .5f + .5f) * info.width), 0,
                                   static_cast<s32>(info.width) - 1);
        const auto ty = std::clamp(FloorCoordinate((py / z * .5f + .5f) * info.height), 0,
                                   static_cast<s32>(info.height) - 1);
        return Pica::Texture::LookupTexture((*faces)[selected].data() + layout.offsets[level],
                                            tx, info.height - 1 - ty, info).Cast<float>();
    };
    const auto tap = [&](s32 x, s32 y) {
        if (faces) {
            const s32 cx = std::clamp(x, 0, static_cast<s32>(info.width) - 1);
            const s32 cy = std::clamp(y, 0, static_cast<s32>(info.height) - 1);
            if (x != cx && y != cy)
                return (cube_tap(cx, cy) + cube_tap(x, cy) + cube_tap(cx, y)) / 3.f;
            return cube_tap(x, y);
        }
        if (Border(config.wrap_s, x, info.width) || Border(config.wrap_t, y, info.height)) {
            return Common::Vec4f{static_cast<float>(config.border_color.r.Value()),
                                  static_cast<float>(config.border_color.g.Value()),
                                  static_cast<float>(config.border_color.b.Value()),
                                  static_cast<float>(config.border_color.a.Value())};
        }
        x = GetWrappedTexCoord(config.wrap_s, x, info.width);
        y = GetWrappedTexCoord(config.wrap_t, y, info.height);
        return Pica::Texture::LookupTexture(data.data() + layout.offsets[level], x,
                                            info.height - 1 - y, info).Cast<float>();
    };
    const float x = u * info.width - (linear ? .5f : 0.f);
    const float y = v * info.height - (linear ? .5f : 0.f);
    const s32 ix = FloorCoordinate(x), iy = FloorCoordinate(y);
    if (!linear) return tap(ix, iy);
    const float fx = x - ix, fy = y - iy;
    return Mix(Mix(tap(ix, iy), tap(ix + 1, iy), fx),
               Mix(tap(ix, iy + 1), tap(ix + 1, iy + 1), fx), fy);
}
} // namespace

TextureLayout MakeTextureLayout(const Config& config, Format format, bool shadow) {
    if (static_cast<u32>(format) > static_cast<u32>(Format::ETC1A4) ||
        config.width < 8 || config.height < 8 || config.width % 8 || config.height % 8 ||
        (!shadow && config.lod.min_level > config.lod.max_level) ||
        (shadow && format != Format::RGBA8)) {
        throw VideoCore::ShaderRecoveryError{"CPU Software invalid tiled texture dimensions/format"};
    }
    TextureLayout result;
    u32 width = config.width, height = config.height;
    for (u32 level = 0; ; ++level) {
        auto& info = result.levels[level];
        info = {0, width, height, 0, format, shadow};
        info.SetDefaultStride();
        result.offsets[level] = result.bytes;
        result.bytes += Pica::Texture::CalculateTileSize(format) * (width / 8) * (height / 8);
        result.max_level = level;
        if (shadow || level == config.lod.max_level || width <= 8 || height <= 8) break;
        width >>= 1;
        height >>= 1;
        if (width % 8 || height % 8) {
            throw VideoCore::ShaderRecoveryError{"CPU Software invalid tiled mip dimensions"};
        }
    }
    return result;
}

// CodexAstraLocal: Share min/mag/mip selection between 2D and cube samples;
// only the checked tap resolver differs, not filter rounding or LOD policy.
static Common::Vec4<u8> FilterTexture(std::span<const u8> data, const TextureLayout& layout,
                               const Config& config, float u, float v, float lod,
                               Settings::TextureSampling sampling,
                               const std::array<std::span<const u8>, 6>* faces, u32 face) {
    if (data.size() < layout.bytes || std::isnan(lod)) {
        throw VideoCore::ShaderRecoveryError{"CPU Software texture/mip range is unavailable"};
    }
    const auto linear = [&](Config::TextureFilter filter) {
        return sampling == Settings::TextureSampling::Linear ||
               (sampling == Settings::TextureSampling::GameControlled && filter == Config::Linear);
    };
    lod += static_cast<float>(config.lod.bias.Value()) / 256.f;
    // CodexAstraLocal: Match sampler MIN_LOD/MAX_LOD before selecting min/mag;
    // image-level availability clamps afterwards. In particular maxLod=0 uses
    // magnification even for a large footprint (Khronos Vulkan Samplers mapping).
    lod = std::clamp(lod, static_cast<float>(config.lod.min_level.Value()),
                    static_cast<float>(config.lod.max_level.Value()));
    const bool minify = lod > 0.f;
    lod = std::min(lod, static_cast<float>(layout.max_level));
    const bool filter = linear(minify ? config.min_filter.Value() : config.mag_filter.Value());
    if (linear(config.mip_filter) && minify) {
        const u32 low = static_cast<u32>(std::floor(lod));
        const u32 high = std::min(low + 1, layout.max_level);
        return RoundColor(Mix(SampleLevel(data, layout, config, low, u, v, filter, faces, face),
                              SampleLevel(data, layout, config, high, u, v, filter, faces, face), lod - low));
    }
    const u32 level = static_cast<u32>(std::floor(lod + .5f));
    return RoundColor(SampleLevel(data, layout, config, level, u, v, filter, faces, face));
}

Common::Vec4<u8> SampleTexture(std::span<const u8> data, const TextureLayout& layout,
                               const Config& config, float u, float v, float lod,
                               Settings::TextureSampling sampling) {
    return FilterTexture(data, layout, config, u, v, lod, sampling, nullptr, 0);
}

Common::Vec4<u8> SampleCubeTexture(const std::array<std::span<const u8>, 6>& faces,
                                   const TextureLayout& layout, const Config& config,
                                   u32 face, float u, float v, float lod,
                                   Settings::TextureSampling sampling) {
    if (face >= 6 || std::any_of(faces.begin(), faces.end(), [&](auto data) { return data.size() < layout.bytes; }))
        throw VideoCore::ShaderRecoveryError{"CPU Software cube range is unavailable"};
    return FilterTexture(faces[face], layout, config, u, v, lod, sampling, &faces, face);
}

Common::Vec4<u8> SampleShadow(std::span<const u8> data, const TextureLayout& layout,
                              float u, float v, u32 depth, bool cube) {
    if (data.size() < layout.bytes) {
        throw VideoCore::ShaderRecoveryError{"CPU Software shadow range is unavailable"};
    }
    const auto& info = layout.levels[0];
    const auto tap = [&](s32 x, s32 y) -> float {
        if (cube) {
            x = std::clamp(x, 0, static_cast<s32>(info.width) - 1);
            y = std::clamp(y, 0, static_cast<s32>(info.height) - 1);
        } else if (x < 0 || y < 0 || x >= static_cast<s32>(info.width) ||
                   y >= static_cast<s32>(info.height)) {
            return 255.f;
        }
        const auto color = Pica::Texture::LookupTexture(data.data(), x, info.height - 1 - y, info);
        const u32 reference = (u32{color.w} << 16) | (u32{color.z} << 8) | color.y;
        return reference > depth ? static_cast<float>(color.x) : 0.f;
    };
    const float x = u * info.width - .5f, y = v * info.height - .5f;
    const s32 ix = FloorCoordinate(x), iy = FloorCoordinate(y);
    const float fx = x - ix, fy = y - iy;
    const float bottom = tap(ix, iy) * (1.f - fx) + tap(ix + 1, iy) * fx;
    const float top = tap(ix, iy + 1) * (1.f - fx) + tap(ix + 1, iy + 1) * fx;
    const u8 density = static_cast<u8>(std::clamp(std::round(bottom * (1.f - fy) + top * fy), 0.f, 255.f));
    return {density, density, density, density};
}

float TextureLod(const std::array<Common::Vec2f, 4>& quad, u32 lane,
                 u32 width, u32 height) {
    const auto dx = quad[(lane & 2) | 1] - quad[lane & 2];
    const auto dy = quad[(lane & 1) | 2] - quad[lane & 1];
    const float footprint = std::max({std::abs(dx.x) * width, std::abs(dy.x) * width,
                                      std::abs(dx.y) * height, std::abs(dy.y) * height});
    return footprint == 0.f ? -std::numeric_limits<float>::infinity() : std::log2(footprint);
}
} // namespace SwRenderer

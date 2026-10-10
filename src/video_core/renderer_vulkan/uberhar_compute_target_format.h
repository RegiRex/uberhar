// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <optional>
#include <string_view>
#include <vulkan/vulkan.hpp>
#include "common/common_types.h"
#include "video_core/rasterizer_cache/pixel_format.h"

namespace Vulkan::ComputeTargetFormat {

// CodexAstraLocal: These codes describe bytes copied from the real Vulkan
// cache image, not guest tiled memory. In particular native RGBA8 is R,G,B,A;
// the guest Common::Color RGBA8 helper stores its bytes in the opposite order.
enum class Encoding : u32 {
    Rgba8 = 0,
    Bgr8 = 1,
    Rgb565 = 2,
    Rgb5A1 = 3,
    Rgba4 = 4,
    D16 = 16,
    D24 = 17,
    D32Float = 18,
};

struct Descriptor {
    VideoCore::PixelFormat logical;
    vk::Format native;
    Encoding encoding;
    u32 transfer_bytes;
    bool has_stencil;
};

// CodexAstraLocal: Preserve the cache's actual fallback representation. RGB8
// currently always falls back to RGBA8; optional packed color/depth support can
// fall back too. A logical format alone is insufficient to decode transfer data.
// Unknown combinations are refused before recording attachment modifications.
inline constexpr std::optional<Descriptor> Describe(VideoCore::PixelFormat logical,
                                                     vk::Format native) {
    using Pixel = VideoCore::PixelFormat;
    const auto color = [&](Encoding encoding, u32 bytes) -> std::optional<Descriptor> {
        return Descriptor{logical, native, encoding, bytes, false};
    };
    switch (logical) {
    case Pixel::RGBA8:
    case Pixel::RGB8:
    case Pixel::RGB565:
    case Pixel::RGB5A1:
    case Pixel::RGBA4:
        if (native == vk::Format::eR8G8B8A8Unorm) return color(Encoding::Rgba8, 4);
        if (logical == Pixel::RGB8 && native == vk::Format::eB8G8R8Unorm)
            return color(Encoding::Bgr8, 3);
        if (logical == Pixel::RGB565 && native == vk::Format::eR5G6B5UnormPack16)
            return color(Encoding::Rgb565, 2);
        if (logical == Pixel::RGB5A1 && native == vk::Format::eR5G5B5A1UnormPack16)
            return color(Encoding::Rgb5A1, 2);
        if (logical == Pixel::RGBA4 && native == vk::Format::eR4G4B4A4UnormPack16)
            return color(Encoding::Rgba4, 2);
        return std::nullopt;
    case Pixel::D16:
        if (native == vk::Format::eD16Unorm)
            return Descriptor{logical, native, Encoding::D16, 2, false};
        if (native == vk::Format::eD32Sfloat)
            return Descriptor{logical, native, Encoding::D32Float, 4, false};
        return std::nullopt;
    case Pixel::D24:
        if (native == vk::Format::eX8D24UnormPack32)
            return Descriptor{logical, native, Encoding::D24, 4, false};
        if (native == vk::Format::eD32Sfloat)
            return Descriptor{logical, native, Encoding::D32Float, 4, false};
        return std::nullopt;
    case Pixel::D24S8:
        if (native == vk::Format::eD24UnormS8Uint)
            return Descriptor{logical, native, Encoding::D24, 4, true};
        if (native == vk::Format::eD32SfloatS8Uint)
            return Descriptor{logical, native, Encoding::D32Float, 4, true};
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

// CodexAstraLocal: The raster invocation keeps native packed bits between
// successive primitives. Quantize each fragment directly to that precision;
// packing a final RGBA8 result would double-round low-bit blending and apply
// logic/write masks to the wrong component widths. Only Describe codes enter.
// Fixed-point ties use roundEven; this is a deterministic conversion policy,
// not a claim that every physical GPU uses the same tie choice for attachments.
inline constexpr std::string_view KernelFunctions = R"(
vec4 DecodeColor(uint word, uint encoding) {
    switch (encoding) {
    case 0u: return unpackUnorm4x8(word);
    case 1u: return vec4(vec3((word>>16u)&255u,(word>>8u)&255u,word&255u)/255.0,1.0);
    case 2u: return vec4(vec3((word>>11u)&31u,(word>>5u)&63u,word&31u)/vec3(31.0,63.0,31.0),1.0);
    case 3u: return vec4(vec3((word>>11u)&31u,(word>>6u)&31u,(word>>1u)&31u)/31.0,float(word&1u));
    case 4u: return vec4((word>>12u)&15u,(word>>8u)&15u,(word>>4u)&15u,word&15u)/15.0;
    } return vec4(0.0);
}
uint EncodeColor(vec4 color, uint encoding) {
    precise vec4 value=clamp(color,vec4(0.0),vec4(1.0));
    switch (encoding) {
    case 0u: return packUnorm4x8(value);
    case 1u: {
        uvec3 c=uvec3(roundEven(value.rgb*255.0));
        return (c.r<<16u)|(c.g<<8u)|c.b;
    }
    case 2u: {
        uvec3 c=uvec3(roundEven(value.rgb*vec3(31.0,63.0,31.0)));
        return (c.r<<11u)|(c.g<<5u)|c.b;
    }
    case 3u: {
        uvec4 c=uvec4(roundEven(value*vec4(31.0,31.0,31.0,1.0)));
        return (c.r<<11u)|(c.g<<6u)|(c.b<<1u)|c.a;
    }
    case 4u: {
        uvec4 c=uvec4(roundEven(value*15.0));
        return (c.r<<12u)|(c.g<<8u)|(c.b<<4u)|c.a;
    }
    } return 0u;
}
uint ColorWriteMask(uint channels, uint encoding) {
    uvec4 bits;
    switch (encoding) {
    case 0u: bits=uvec4(0xffu,0xff00u,0xff0000u,0xff000000u);break;
    case 1u: bits=uvec4(0xff0000u,0xff00u,0xffu,0u);break;
    case 2u: bits=uvec4(0xf800u,0x07e0u,0x001fu,0u);break;
    case 3u: bits=uvec4(0xf800u,0x07c0u,0x003eu,0x0001u);break;
    case 4u: bits=uvec4(0xf000u,0x0f00u,0x00f0u,0x000fu);break;
    default: return 0u;
    }
    uint mask=0u;
    for(uint channel=0u;channel<4u;++channel)
        if((channels&(1u<<channel))!=0u) mask|=bits[channel];
    return mask;
}
float DecodeDepth(uint word, uint encoding) {
    if(encoding==16u) return float(word&65535u)/65535.0;
    if(encoding==17u) return float(word&16777215u)/16777215.0;
    return uintBitsToFloat(word);
}
uint EncodeDepth(float depth, uint encoding) {
    precise float value=clamp(depth,0.0,1.0);
    if(encoding==16u) return uint(roundEven(value*65535.0));
    if(encoding==17u) return uint(roundEven(value*16777215.0));
    return floatBitsToUint(value);
}
)";

// CodexAstraLocal: Transfers use actual native bytes. Expand before raster so
// each invocation owns a whole uint even for D16/RGB565; pack afterward with one
// invocation per destination word, avoiding adjacent-pixel read/modify races.
// Caller validates bytes_per_pixel in [2,4], nonzero bounded pixels, and byte
// arithmetic fitting uint; both buffers include the rounded-up last word.
// Caller owns dispatch barriers, image copies, allocation and completion ticks.
inline constexpr std::string_view ExpandTransferKernel = R"(
#version 450
layout(local_size_x=64) in;
layout(push_constant) uniform Transfer {uint pixels;uint bytes_per_pixel;} transfer;
layout(set=0,binding=0,std430) readonly buffer Input {uint words[];} source;
layout(set=0,binding=1,std430) writeonly buffer Output {uint words[];} destination;
void main() {
    // CodexAstraLocal: Flatten bounded 2D dispatches when a scaled attachment
    // needs more than the device's maximum X workgroup count. Y=1 is unchanged.
    uint pixel=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*64u;
    if(pixel>=transfer.pixels) return;
    uint first=pixel*transfer.bytes_per_pixel;
    uint result=0u;
    for(uint byte=0u;byte<transfer.bytes_per_pixel;++byte) {
        uint offset=first+byte;
        result|=((source.words[offset/4u]>>((offset%4u)*8u))&255u)<<(byte*8u);
    }
    destination.words[pixel]=result;
}
)";

inline constexpr std::string_view PackTransferKernel = R"(
#version 450
layout(local_size_x=64) in;
layout(push_constant) uniform Transfer {uint pixels;uint bytes_per_pixel;} transfer;
layout(set=0,binding=0,std430) readonly buffer Input {uint words[];} source;
layout(set=0,binding=1,std430) writeonly buffer Output {uint words[];} destination;
void main() {
    uint invocation=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*64u;
    uint total=transfer.pixels*transfer.bytes_per_pixel;
    // CodexAstraLocal: Reject padded 2D invocations before multiplying by four
    // so a final partial row cannot wrap a large byte index back to the start.
    if(invocation>=(total+3u)/4u) return;
    uint first=invocation*4u;
    uint result=0u;
    for(uint byte=0u;byte<4u;++byte) {
        uint offset=first+byte;
        if(offset<total) {
            uint pixel=offset/transfer.bytes_per_pixel;
            uint lane=offset%transfer.bytes_per_pixel;
            result|=((source.words[pixel]>>(lane*8u))&255u)<<(byte*8u);
        }
    }
    destination.words[first/4u]=result;
}
)";
} // namespace Vulkan::ComputeTargetFormat

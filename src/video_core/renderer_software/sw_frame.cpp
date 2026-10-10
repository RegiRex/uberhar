// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include "video_core/renderer_software/sw_frame.h"
#include <cstring>
#include <limits>
#include "common/color.h"
#include "video_core/shader_recovery_error.h"

namespace SwRenderer {

PAddr ScreenAddress(const Pica::FramebufferConfig& fb, bool right_eye) {
    const bool right = right_eye && fb.address_right1 != 0 && fb.address_right2 != 0;
    return right ? (fb.active_fb == 0 ? fb.address_right1 : fb.address_right2)
                 : (fb.active_fb == 0 ? fb.address_left1 : fb.address_left2);
}
ScreenInfo CaptureScreen(const Pica::FramebufferConfig& framebuffer,
                         const Pica::ColorFill& color_fill, std::span<const u8> memory) {
    // CodexAstraLocal: LCD fill never requires configured guest framebuffer memory.
    if (color_fill.is_enabled) {
        return {1, 1, {static_cast<u8>(color_fill.color_r), static_cast<u8>(color_fill.color_g),
                       static_cast<u8>(color_fill.color_b), 255}};
    }
    const u32 width = framebuffer.width;
    const u32 height = framebuffer.height;
    if (width == 0 || height == 0) return {};
    if (static_cast<u32>(framebuffer.color_format.Value()) > static_cast<u32>(Pica::PixelFormat::RGBA4)) {
        throw VideoCore::ShaderRecoveryError{"CPU Software: invalid LCD pixel format"};
    }
    const u32 bpp = Pica::BytesPerPixel(framebuffer.color_format);
    const u64 row_bytes = static_cast<u64>(width) * bpp;
    const u64 required = static_cast<u64>(height - 1) * framebuffer.stride + row_bytes;
    const u64 output_bytes = static_cast<u64>(width) * height * 4;
    // CodexAstraLocal: Validate the visible final row before reading; stride
    // padding must never become visible pixels or an unchecked host overread.
    if (framebuffer.stride < row_bytes || required > memory.size() ||
        output_bytes > std::numeric_limits<std::size_t>::max()) {
        throw VideoCore::ShaderRecoveryError{"CPU Software: invalid LCD framebuffer span"};
    }
    ScreenInfo screen{width, height, std::vector<u8>(static_cast<std::size_t>(output_bytes))};
    for (u32 y = 0; y < width; ++y) {
        for (u32 x = 0; x < height; ++x) {
            // CodexAstraLocal: width-1-y fixes the inherited width-y overread
            // while preserving the LCD's quarter-turn and visible dimensions.
            const u8* pixel = memory.data() + static_cast<std::size_t>(x) * framebuffer.stride +
                              static_cast<std::size_t>(width - 1 - y) * bpp;
            const auto color = [&]() -> Common::Vec4<u8> {
                switch (framebuffer.color_format) {
                case Pica::PixelFormat::RGBA8: return Common::Color::DecodeRGBA8(pixel);
                case Pica::PixelFormat::RGB8: return Common::Color::DecodeRGB8(pixel);
                case Pica::PixelFormat::RGB565: return Common::Color::DecodeRGB565(pixel);
                case Pica::PixelFormat::RGB5A1: return Common::Color::DecodeRGB5A1(pixel);
                case Pica::PixelFormat::RGBA4: return Common::Color::DecodeRGBA4(pixel);
                }
                return {}; // Validated before allocating or reading.
            }();
            std::memcpy(screen.pixels.data() + (static_cast<std::size_t>(y) * height + x) * 4,
                        color.AsArray(), 4);
        }
    }
    return screen;
}
} // namespace SwRenderer

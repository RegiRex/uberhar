// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <array>
#include <span>
#include <vector>
#include "common/common_types.h"
#include "video_core/pica/regs_external.h"
#include "video_core/pica/regs_lcd.h" // CodexAstraLocal: LCD fill is independent of guest memory.

namespace SwRenderer {
// CodexAstraLocal: Preserve Qt's software convention: width/height are visible
// unrotated LCD dimensions; RGBA pixels have height columns and width rows.
struct ScreenInfo {
    u32 width{};
    u32 height{};
    std::vector<u8> pixels;
};
// CodexAstraLocal: A presenter can lease one immutable complete publication
// while the CPU prepares the next; no guest framebuffer pointer escapes.
struct SoftwareFrame {
    std::array<ScreenInfo, 3> screens;
};
// CodexAstraLocal: Match both active LCD buffers and the established mono fallback.
PAddr ScreenAddress(const Pica::FramebufferConfig& framebuffer, bool right_eye);
ScreenInfo CaptureScreen(const Pica::FramebufferConfig& framebuffer,
                         const Pica::ColorFill& color_fill, std::span<const u8> memory);
} // namespace SwRenderer

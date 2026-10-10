// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <array>
#include <atomic>
#include "common/common_types.h"
#include "core/frontend/framebuffer_layout.h"
#include "video_core/renderer_software/sw_frame.h"

namespace SwRenderer {
// CodexAstraLocal: Presentation consumes already-rasterized owned pixels. These
// shareable GL objects never contain guest PICA state or execute guest shaders.
class SoftwarePresenter {
public:
    SoftwarePresenter();
    ~SoftwarePresenter();
    // CodexAstraLocal: Failed teardown binding leaves GL reclamation to the
    // shared EGL group's destruction; never delete in an unrelated context.
    void Abandon() noexcept;
    SoftwarePresenter(const SoftwarePresenter&) = delete;
    SoftwarePresenter& operator=(const SoftwarePresenter&) = delete;
    void Present(const SoftwareFrame* frame, const Layout::FramebufferLayout& layout);
    bool Failed() const { return failed.load(std::memory_order_acquire); }
private:
    void Release();
    void Draw(const ScreenInfo& screen, u32 index, const Common::Rectangle<u32>& rect,
              const Layout::FramebufferLayout& layout, float opacity);
    u32 program{};
    std::array<u32, 3> textures{};
    std::array<std::array<u32, 2>, 3> dimensions{};
    s32 rectangle_uniform{}, rotation_uniform{}, opacity_uniform{};
    std::atomic<bool> failed{};
};
} // namespace SwRenderer

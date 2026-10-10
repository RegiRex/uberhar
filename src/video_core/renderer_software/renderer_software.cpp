// Copyright 2023-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_software/renderer_software.h"
#include "video_core/shader_recovery_error.h"
#ifdef ANDROID
#include "video_core/renderer_software/sw_presenter_gl.h"
#endif

namespace SwRenderer {

RendererSoftware::RendererSoftware(Core::System& system, Pica::PicaCore& pica_,
                                   Frontend::EmuWindow& window,
                                   Frontend::EmuWindow* secondary_window)
    : VideoCore::RendererBase{system, window, secondary_window}, memory{system.Memory()}, pica{pica_},
      rasterizer{memory, pica} {
#ifdef ANDROID
    // CodexAstraLocal: Only context-shared screen-copy resources are created;
    // no guest shaders, PICA state or raster operations enter this presenter.
    presenter = std::make_unique<SoftwarePresenter>();
#endif
    LOG_INFO(Render_Software, "CPU Software renderer: scale=1 guest_graphics=cpu presentation={}",
#ifdef ANDROID
             "gles_screen_copy"
#else
             "frontend_screen_copy"
#endif
    );
}

RendererSoftware::~RendererSoftware() {
#ifdef ANDROID
    // CodexAstraLocal: JNI excludes UI presentation before teardown. Shared GL
    // objects are released with the surviving core context current.
    try {
        const auto context = render_window.Acquire();
        presenter.reset();
    } catch (const VideoCore::ShaderRecoveryError& error) {
        // CodexAstraLocal: Destruction cannot propagate a failed EGL bind or
        // issue GL calls in an unknown context. EGL owns final group reclamation.
        LOG_ERROR(Render_Software, "CPU Software teardown context unavailable: {}", error.what());
        if (presenter) presenter->Abandon();
        presenter.reset();
    }
#endif
}

void RendererSoftware::SwapBuffers() {
#ifdef ANDROID
    if (presenter->Failed()) {
        throw VideoCore::ShaderRecoveryError{"CPU Software presentation failed; see renderer log"};
    }
#endif
    system.perf_stats->StartSwap();
    PrepareRenderTarget();
    if (secondary_window) {
        secondary_window->PollEvents();
    }
    system.perf_stats->EndSwap();
    EndFrame();
}

void RendererSoftware::PrepareRenderTarget() {
    // CodexAstraLocal: Keep only the latest complete frame plus an upload's
    // temporary lease and this capture; guest triangles already joined.
    auto frame = std::make_shared<SoftwareFrame>();
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;
        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        const auto& fb = pica.regs.framebuffer_config[fb_id];
        // CodexAstraLocal: Preserve distinct active right/left addresses. Fill
        // never reads memory, including boot's unconfigured framebuffer.
        // CodexAstraLocal: Match the existing GL mono fallback when no complete
        // right-eye pair is configured; valid right-eye pairs remain distinct.
        const PAddr address = ScreenAddress(fb, i == 1);
        const auto ref = color_fill.is_enabled ? MemoryRef{} : memory.GetPhysicalRef(address);
        frame->screens[i] = CaptureScreen(fb, color_fill, {ref.GetPtr(), ref.GetSize()});
    }
    std::scoped_lock lock{frame_mutex};
    published_frame = std::move(frame);
}

std::shared_ptr<const SoftwareFrame> RendererSoftware::Snapshot() const {
    std::scoped_lock lock{frame_mutex};
    return published_frame;
}

ScreenInfo RendererSoftware::Screen(VideoCore::ScreenId id) const {
    const auto frame = Snapshot();
    return frame ? frame->screens[static_cast<u32>(id)] : ScreenInfo{};
}

void RendererSoftware::TryPresent(int, bool is_secondary) {
#ifdef ANDROID
    if (is_secondary && !secondary_window) {
        return;
    }
    const auto& window = is_secondary ? *secondary_window : render_window;
    const auto frame = Snapshot();
    presenter->Present(frame.get(), window.GetFramebufferLayout());
#endif
}

} // namespace SwRenderer

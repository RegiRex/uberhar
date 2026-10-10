// Copyright 2023-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <mutex>
#include "video_core/renderer_base.h"
#include "video_core/renderer_software/sw_frame.h"
#include "video_core/renderer_software/sw_rasterizer.h"

namespace Core {
class System;
}

namespace SwRenderer {

class SoftwarePresenter;

class RendererSoftware : public VideoCore::RendererBase {
public:
    explicit RendererSoftware(Core::System& system, Pica::PicaCore& pica,
                              Frontend::EmuWindow& window,
                              Frontend::EmuWindow* secondary_window = nullptr);
    ~RendererSoftware() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    // CodexAstraLocal: Qt takes a stable copy; Android leases an immutable frame.
    [[nodiscard]] ScreenInfo Screen(VideoCore::ScreenId id) const;
    [[nodiscard]] std::shared_ptr<const SoftwareFrame> Snapshot() const;

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override;

private:
    void PrepareRenderTarget();

private:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    RasterizerSoftware rasterizer;
    mutable std::mutex frame_mutex;
    std::shared_ptr<const SoftwareFrame> published_frame;
#ifdef ANDROID
    // CodexAstraLocal: Only completed-screen textures/program are GPU resources.
    std::unique_ptr<SoftwarePresenter> presenter;
#endif
};

} // namespace SwRenderer

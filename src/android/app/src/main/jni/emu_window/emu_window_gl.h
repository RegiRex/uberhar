// Copyright 2019-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <atomic> // CodexAstraLocal: Transfer presentation errors to the core thread.
#include <mutex> // CodexAstraLocal: Optional Software surface lifecycle exclusion.

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "jni/emu_window/emu_window.h"

namespace Core {
class System;
}

struct ANativeWindow;

class EmuWindow_Android_OpenGL : public EmuWindow_Android {
public:
    EmuWindow_Android_OpenGL(Core::System& system, ANativeWindow* surface, bool is_secondary,
                             EGLContext* sharedContext = nullptr,
                             std::recursive_mutex* lifecycle_mutex = nullptr);
    ~EmuWindow_Android_OpenGL() override;

    void TryPresenting() override;
    void StopPresenting() override;
    void PollEvents() override;
    EGLContext* GetEGLContext() override;
    std::unique_ptr<GraphicsContext> CreateSharedContext() const override;

private:
    bool CreateWindowSurface() override;
    void DestroyWindowSurface() override;
    void DestroyContext() override;

private:
    Core::System& system;
    // CodexAstraLocal: Both Software windows borrow the same frontend lock;
    // only the primary owns the shared presentation context and EGL display.
    std::recursive_mutex* lifecycle_mutex{};
    bool owns_context{true};
    std::atomic<bool> presentation_failed{};
    EGLConfig egl_config{};
    EGLSurface egl_surface{};
    EGLContext egl_context{};
    EGLDisplay egl_display{};

    enum class PresentingState {
        Initial,
        Running,
        Stopped,
    };
    PresentingState presenting_state{};
};

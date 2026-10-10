// Copyright 2019-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

#include <android/native_window_jni.h>
#include <glad/glad.h>

#include "common/logging/log.h"
#include "common/settings.h"
#include "common/scope_exit.h" // CodexAstraLocal: Release partial startup/UI context on all exits.
#include "core/core.h"
#include "input_common/main.h"
#include "jni/emu_window/emu_window_gl.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/shader_recovery_error.h" // CodexAstraLocal: Fail Software startup on the core thread.

static constexpr std::array<EGLint, 15> egl_attribs{EGL_SURFACE_TYPE,
                                                    EGL_WINDOW_BIT,
                                                    EGL_RENDERABLE_TYPE,
                                                    EGL_OPENGL_ES3_BIT_KHR,
                                                    EGL_BLUE_SIZE,
                                                    8,
                                                    EGL_GREEN_SIZE,
                                                    8,
                                                    EGL_RED_SIZE,
                                                    8,
                                                    EGL_DEPTH_SIZE,
                                                    0,
                                                    EGL_STENCIL_SIZE,
                                                    0,
                                                    EGL_NONE};
static constexpr std::array<EGLint, 5> egl_empty_attribs{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
static constexpr std::array<EGLint, 4> egl_context_attribs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

class SharedContext_Android : public Frontend::GraphicsContext {
public:
    SharedContext_Android(EGLDisplay egl_display, EGLConfig egl_config,
                          EGLContext egl_share_context, bool strict = false)
        : strict{strict}, egl_display{egl_display},
          egl_surface{eglCreatePbufferSurface(egl_display, egl_config, egl_empty_attribs.data())},
          egl_context{eglCreateContext(egl_display, egl_config, egl_share_context,
                                       egl_context_attribs.data())} {
        // CodexAstraLocal: Software startup reports an actionable error instead
        // of leaving a null core context or aborting the Android process.
        if (strict && (!egl_surface || !egl_context)) {
            if (egl_surface) eglDestroySurface(egl_display, egl_surface);
            if (egl_context) eglDestroyContext(egl_display, egl_context);
            throw VideoCore::ShaderRecoveryError{"CPU Software shared EGL context creation failed"};
        }
        ASSERT_MSG(egl_surface, "eglCreatePbufferSurface() failed!");
        ASSERT_MSG(egl_context, "eglCreateContext() failed!");
    }

    ~SharedContext_Android() override {
        // CodexAstraLocal: Partial Software startup can still own this context;
        // unbind before deleting its pbuffer/context on the constructing thread.
        if (eglGetCurrentContext() == egl_context) {
            eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (!eglDestroySurface(egl_display, egl_surface)) {
            LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
        }

        if (!eglDestroyContext(egl_display, egl_context)) {
            LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
        }
    }

    void MakeCurrent() override {
        // CodexAstraLocal: A failed Software bind must not use whichever other
        // context happened to be current on this thread. Owner callers contain
        // this terminal error; cleanup/DoneCurrent never throw.
        if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) && strict) {
            throw VideoCore::ShaderRecoveryError{"CPU Software core EGL context binding failed"};
        }
    }

    void DoneCurrent() override {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

private:
    bool strict{};
    EGLDisplay egl_display{};
    EGLSurface egl_surface{};
    EGLContext egl_context{};
};

EmuWindow_Android_OpenGL::EmuWindow_Android_OpenGL(Core::System& system_, ANativeWindow* surface,
                                                   bool is_secondary, EGLContext* sharedContext,
                                                   std::recursive_mutex* lifecycle_mutex_)
    : EmuWindow_Android{surface, is_secondary}, system{system_},
      lifecycle_mutex{lifecycle_mutex_}, owns_context{sharedContext == nullptr} {
    // CodexAstraLocal: Constructor exceptions do not run the derived destructor.
    // Release partially created Software resources before native reports failure.
    bool initialized = false;
    SCOPE_EXIT({
        if (lifecycle_mutex && !initialized) {
            core_context.reset();
            DestroyWindowSurface();
            DestroyContext();
        }
    });
    const auto fail = [&](const char* message) {
        LOG_CRITICAL(Frontend, "{}", message);
        if (lifecycle_mutex) throw VideoCore::ShaderRecoveryError{message};
    };
    if (egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY); egl_display == EGL_NO_DISPLAY) {
        fail("eglGetDisplay() failed");
        return;
    }
    if (eglInitialize(egl_display, 0, 0) != EGL_TRUE) {
        fail("eglInitialize() failed");
        return;
    }
    auto attributes = egl_attribs;
    // CodexAstraLocal: The CPU owner needs its pbuffer even with no bottom screen.
    if (lifecycle_mutex) attributes[1] |= EGL_PBUFFER_BIT;
    if (EGLint egl_num_configs{}; eglChooseConfig(egl_display, attributes.data(), &egl_config, 1,
                                                  &egl_num_configs) != EGL_TRUE || !egl_num_configs) {
        fail("eglChooseConfig() failed");
        return;
    }

    // CodexAstraLocal: Software has a complete core context independent of an
    // optional Android surface. A missing secondary screen is a valid idle state.
    if (lifecycle_mutex) {
        egl_context = sharedContext ? *sharedContext :
            eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, egl_context_attribs.data());
        if (!egl_context) fail("CPU Software presentation context creation failed");
        core_context = CreateSharedContext();
        core_context->MakeCurrent();
        if (!gladLoadGLES2Loader((GLADloadproc)eglGetProcAddress))
            fail("CPU Software GLES loader initialization failed");
        if (host_window) {
            if (!CreateWindowSurface()) fail("CPU Software window surface creation failed");
            if (!eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &window_width) ||
                !eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &window_height))
                fail("CPU Software surface dimensions unavailable");
            OnFramebufferSizeChanged();
        } else {
            presenting_state = PresentingState::Stopped;
        }
        initialized = true;
        return;
    }

    CreateWindowSurface();

    if (eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &window_width) != EGL_TRUE) {
        return;
    }
    if (eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &window_height) != EGL_TRUE) {
        return;
    }
    if (sharedContext) {
        egl_context = *sharedContext;
    } else if (egl_context =
                   eglCreateContext(egl_display, egl_config, 0, egl_context_attribs.data());
               egl_context == EGL_NO_CONTEXT) {
        LOG_CRITICAL(Frontend, "eglCreateContext() failed");
        return;
    }
    if (eglSurfaceAttrib(egl_display, egl_surface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED) !=
        EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglSurfaceAttrib() failed");
        return;
    }
    if (core_context = CreateSharedContext(); !core_context) {
        LOG_CRITICAL(Frontend, "CreateSharedContext() failed");
        return;
    }
    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglMakeCurrent() failed");
        return;
    }
    if (!gladLoadGLES2Loader((GLADloadproc)eglGetProcAddress)) {
        LOG_CRITICAL(Frontend, "gladLoadGLES2Loader() failed");
        return;
    }
    if (!eglSwapInterval(egl_display, Settings::values.use_vsync ? 1 : 0)) {
        LOG_CRITICAL(Frontend, "eglSwapInterval() failed");
        return;
    }

    OnFramebufferSizeChanged();
}

EmuWindow_Android_OpenGL::~EmuWindow_Android_OpenGL() {
    // CodexAstraLocal: Base-destructor virtual calls cannot release derived EGL
    // objects. Software tears down explicitly, secondary first under native lock.
    if (lifecycle_mutex) {
        std::scoped_lock lock{*lifecycle_mutex};
        core_context.reset();
        DestroyWindowSurface();
        DestroyContext();
    }
}

EGLContext* EmuWindow_Android_OpenGL::GetEGLContext() {
    return &egl_context;
}

bool EmuWindow_Android_OpenGL::CreateWindowSurface() {
    if (!host_window) {
        return true;
    }

    EGLint format{};
    eglGetConfigAttrib(egl_display, egl_config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(host_window, 0, 0, format);

    if (egl_surface = eglCreateWindowSurface(egl_display, egl_config, host_window, 0);
        egl_surface == EGL_NO_SURFACE) {
        return {};
    }

    return egl_surface;
}

void EmuWindow_Android_OpenGL::DestroyWindowSurface() {
    if (!egl_surface) {
        return;
    }
    if (eglGetCurrentSurface(EGL_DRAW) == egl_surface) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (!eglDestroySurface(egl_display, egl_surface)) {
        LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
    }
    egl_surface = EGL_NO_SURFACE;
}

void EmuWindow_Android_OpenGL::DestroyContext() {
    // CodexAstraLocal: A secondary Software window borrows the primary's UI
    // context/display. Partial primary startup still releases its EGL display.
    if (lifecycle_mutex) {
        if (owns_context) {
            if (egl_context && eglGetCurrentContext() == egl_context)
                eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (egl_context) eglDestroyContext(egl_display, egl_context);
            if (egl_display) eglTerminate(egl_display);
        }
        egl_context = EGL_NO_CONTEXT;
        egl_display = EGL_NO_DISPLAY;
        return;
    }
    if (!egl_context) {
        return;
    }
    if (eglGetCurrentContext() == egl_context) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (!eglDestroyContext(egl_display, egl_context)) {
        LOG_CRITICAL(Frontend, "eglDestroySurface() failed");
    }
    if (!eglTerminate(egl_display)) {
        LOG_CRITICAL(Frontend, "eglTerminate() failed");
    }
    egl_context = EGL_NO_CONTEXT;
    egl_display = EGL_NO_DISPLAY;
}

std::unique_ptr<Frontend::GraphicsContext> EmuWindow_Android_OpenGL::CreateSharedContext() const {
    return std::make_unique<SharedContext_Android>(egl_display, egl_config, egl_context,
                                                  lifecycle_mutex != nullptr);
}

void EmuWindow_Android_OpenGL::PollEvents() {
    // CodexAstraLocal: EndFrame calls this on the owner, outside JNI doFrame.
    // Reuse its recursive lock only for Software surface/context transitions.
    std::unique_lock<std::recursive_mutex> lock;
    if (lifecycle_mutex) lock = std::unique_lock{*lifecycle_mutex};
    if (presentation_failed.load(std::memory_order_acquire))
        throw VideoCore::ShaderRecoveryError{"CPU Software EGL presentation failed; see frontend log"};
    if (lifecycle_mutex && !render_window && host_window && window_width == 0 && window_height == 0) {
        // A null surface notification must release the old EGL surface too.
        host_window = nullptr;
        DestroyWindowSurface();
        presenting_state = PresentingState::Stopped;
        return;
    }
    if (!render_window) {
        return;
    }

    host_window = render_window;
    render_window = nullptr;

    DestroyWindowSurface();
    if (!CreateWindowSurface() && lifecycle_mutex)
        throw VideoCore::ShaderRecoveryError{"CPU Software replacement surface creation failed"};
    OnFramebufferSizeChanged();
    presenting_state = PresentingState::Initial;
}

void EmuWindow_Android_OpenGL::StopPresenting() {
    // CodexAstraLocal: Software callers may already hold the same recursive lock.
    std::unique_lock<std::recursive_mutex> lock;
    if (lifecycle_mutex) lock = std::unique_lock{*lifecycle_mutex};
    if (presenting_state == PresentingState::Running) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    presenting_state = PresentingState::Stopped;
}

void EmuWindow_Android_OpenGL::TryPresenting() {
    // CodexAstraLocal: Presentation never keeps the shared UI context current
    // outside this bounded callback; owner-side surface recreation is then safe.
    std::unique_lock<std::recursive_mutex> lock;
    if (lifecycle_mutex) lock = std::unique_lock{*lifecycle_mutex};
    if (lifecycle_mutex && (!egl_surface || presentation_failed.load(std::memory_order_acquire)))
        return;
    if (!system.IsPoweredOn()) {
        return;
    }
    if (presenting_state == PresentingState::Initial) [[unlikely]] {
        presenting_state = PresentingState::Running;
    }
    if (presenting_state != PresentingState::Running) [[unlikely]] {
        return;
    }
    const bool current = eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    SCOPE_EXIT({
        if (lifecycle_mutex && current)
            eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    });
    if (lifecycle_mutex && !current) {
        LOG_ERROR(Frontend, "CPU Software eglMakeCurrent failed: {}", eglGetError());
        presentation_failed.store(true, std::memory_order_release);
        return;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    eglSwapInterval(egl_display, Settings::values.use_vsync ? 1 : 0);
    system.GPU().Renderer().TryPresent(0, is_secondary);
    if (!eglSwapBuffers(egl_display, egl_surface) && lifecycle_mutex) {
        LOG_ERROR(Frontend, "CPU Software eglSwapBuffers failed: {}", eglGetError());
        presentation_failed.store(true, std::memory_order_release);
    }
}

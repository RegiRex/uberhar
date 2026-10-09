// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/gpu.h"
#include "video_core/shader_recovery_error.h" // CodexAstraLocal: Contained strict-backend failure.
#ifdef ENABLE_OPENGL
#include "video_core/renderer_opengl/renderer_opengl.h"
#endif
#ifdef ENABLE_SOFTWARE_RENDERER
#include "video_core/renderer_software/renderer_software.h"
#endif
#ifdef ENABLE_VULKAN
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#endif
#include "video_core/video_core.h"

#ifdef ENABLE_SDL2
#include <SDL.h>
#endif

namespace VideoCore {

std::unique_ptr<RendererBase> CreateRenderer(Frontend::EmuWindow& emu_window,
                                             Frontend::EmuWindow* secondary_window,
                                             Pica::PicaCore& pica, Core::System& system) {
    const auto graphics_api = Settings::GetWorkingGraphicsAPI();
    // CodexAstraLocal: Calculated isolation cannot silently select OpenGL or
    // software when Vulkan is unavailable. Other profiles retain the existing
    // backend resolver and default behavior; initial-load callers contain this
    // typed failure before any substitute renderer can be constructed.
    if (Settings::values.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Compute) {
#ifdef ENABLE_VULKAN
        const bool available = graphics_api == Settings::GraphicsAPI::Vulkan;
#else
        const bool available = false;
#endif
        if (!available) {
            throw ShaderRecoveryError{
                "Isolated Calculated rendering requires the Vulkan backend; fallback is disabled"};
        }
    }
    switch (graphics_api) {
#ifdef ENABLE_SOFTWARE_RENDERER
    case Settings::GraphicsAPI::Software:
        return std::make_unique<SwRenderer::RendererSoftware>(system, pica, emu_window);
#endif
#ifdef ENABLE_VULKAN
    case Settings::GraphicsAPI::Vulkan:
#if defined(ENABLE_SDL2) && !defined(__APPLE__)
        // TODO: When we migrate to SDL3, refactor so that we don't need to init here.
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
            SDL_Init(SDL_INIT_VIDEO);
        }
#endif // ENABLE_SDL2
        return std::make_unique<Vulkan::RendererVulkan>(system, pica, emu_window, secondary_window);
#endif
#ifdef ENABLE_OPENGL
    case Settings::GraphicsAPI::OpenGL:
        return std::make_unique<OpenGL::RendererOpenGL>(system, pica, emu_window, secondary_window);
#endif
    default:
        LOG_CRITICAL(Render,
                     "Unknown or unsupported graphics API {}, falling back to available default",
                     graphics_api);
#ifdef ENABLE_OPENGL
        return std::make_unique<OpenGL::RendererOpenGL>(system, pica, emu_window, secondary_window);
#elif ENABLE_VULKAN
        return std::make_unique<Vulkan::RendererVulkan>(system, pica, emu_window, secondary_window);
#elif ENABLE_SOFTWARE_RENDERER
        return std::make_unique<SwRenderer::RendererSoftware>(system, pica, emu_window);
#else
// TODO: Add a null renderer backend for this, perhaps.
#error "At least one renderer must be enabled."
#endif
    }
}

} // namespace VideoCore

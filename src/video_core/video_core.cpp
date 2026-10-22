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
    // CodexAstraLocal: Retired profiles are migrated before this factory. Keep
    // the reusable internal Software backend, without a standalone UI model.
    // CodexAstraLocal: Only supported renderers reach factory selection after profile migration.
    switch (graphics_api) {
// CodexAstraLocal: Retain the desktop/reference core, but compile no Android selection route.
#if defined(ENABLE_SOFTWARE_RENDERER) && !defined(ANDROID)
    case Settings::GraphicsAPI::Software:
        // CodexAstraLocal: Keep the reusable desktop/reference CPU renderer isolated
        // from guest GPU rasterization; Android cannot select this constructor.
        return std::make_unique<SwRenderer::RendererSoftware>(system, pica, emu_window,
                                                             secondary_window);
#else
    case Settings::GraphicsAPI::Software:
        // CodexAstraLocal: A disabled build cannot silently change CPU graphics.
        throw ShaderRecoveryError{"CPU Software renderer is unavailable in this build"};
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
#elif defined(ENABLE_SOFTWARE_RENDERER) && !defined(ANDROID)
        return std::make_unique<SwRenderer::RendererSoftware>(system, pica, emu_window,
                                                             secondary_window);
#else
// TODO: Add a null renderer backend for this, perhaps.
#error "At least one renderer must be enabled."
#endif
    }
}

} // namespace VideoCore

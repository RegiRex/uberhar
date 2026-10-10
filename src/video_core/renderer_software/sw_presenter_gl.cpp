// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include "video_core/renderer_software/sw_presenter_gl.h"
#include <algorithm>
#include <array>
#include <glad/glad.h>
#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/shader_recovery_error.h"

namespace SwRenderer {
namespace {
// CodexAstraLocal: Two constant screen-copy shaders are the only GPU programs.
// They select no PICA operation and are independent of game/shader-cache state.
constexpr char VertexSource[] = R"(#version 300 es
precision highp float;
uniform vec4 rectangle;
uniform int rotated;
out vec2 uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(rectangle.xy + p * rectangle.zw, 0.0, 1.0);
    uv = rotated != 0 ? p : vec2(1.0 - p.y, p.x);
}
)";
constexpr char FragmentSource[] = R"(#version 300 es
precision highp float;
uniform sampler2D screen_texture;
uniform float opacity;
in vec2 uv;
out vec4 color;
void main() { color = vec4(texture(screen_texture, uv).rgb, opacity); }
)";

GLuint Compile(GLenum kind, const char* source) {
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint okay{};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        std::array<char, 1024> message{};
        glGetShaderInfoLog(shader, message.size(), nullptr, message.data());
        glDeleteShader(shader);
        LOG_CRITICAL(Render_Software, "CPU Software screen-copy shader: {}", message.data());
        throw VideoCore::ShaderRecoveryError{"CPU Software screen-copy shader compilation failed"};
    }
    return shader;
}
} // namespace

SoftwarePresenter::SoftwarePresenter() {
    GLuint vertex{}, fragment{};
    try {
        vertex = Compile(GL_VERTEX_SHADER, VertexSource);
        fragment = Compile(GL_FRAGMENT_SHADER, FragmentSource);
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint okay{};
        glGetProgramiv(program, GL_LINK_STATUS, &okay);
        if (!okay) {
            throw VideoCore::ShaderRecoveryError{"CPU Software screen-copy program failed to link"};
        }
        rectangle_uniform = glGetUniformLocation(program, "rectangle");
        rotation_uniform = glGetUniformLocation(program, "rotated");
        opacity_uniform = glGetUniformLocation(program, "opacity");
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "screen_texture"), 0);
        glUseProgram(0);
        glGenTextures(textures.size(), textures.data());
        // CodexAstraLocal: Initialization runs on the existing shared core
        // context. Flush its object creation before the UI uses those objects.
        glFlush();
        if (glGetError() != GL_NO_ERROR) {
            throw VideoCore::ShaderRecoveryError{"CPU Software GLES presentation initialization failed"};
        }
    } catch (...) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        Release();
        throw;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

SoftwarePresenter::~SoftwarePresenter() { Release(); }

void SoftwarePresenter::Abandon() noexcept {
    program = 0;
    textures.fill(0);
}

void SoftwarePresenter::Release() {
    if (std::any_of(textures.begin(), textures.end(), [](auto texture) { return texture != 0; }))
        glDeleteTextures(textures.size(), textures.data());
    textures.fill(0);
    if (program) glDeleteProgram(program);
    program = 0;
}

void SoftwarePresenter::Draw(const ScreenInfo& screen, u32 index,
                             const Common::Rectangle<u32>& rect,
                             const Layout::FramebufferLayout& layout, float opacity) {
    if (screen.pixels.empty() || rect.GetWidth() == 0 || rect.GetHeight() == 0) return;
    glBindTexture(GL_TEXTURE_2D, textures[index]);
    const auto filter = Settings::values.filter_mode.GetValue() ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const std::array<u32, 2> size{screen.height, screen.width};
    // CodexAstraLocal: Direct client-memory upload consumes the leased bytes
    // before return. No PBO, mapped buffer or extra GPU fence owns CPU storage.
    if (dimensions[index] != size) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size[0], size[1], 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, screen.pixels.data());
        dimensions[index] = size;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size[0], size[1], GL_RGBA,
                        GL_UNSIGNED_BYTE, screen.pixels.data());
    }
    glUniform4f(rectangle_uniform, 2.f * rect.left / layout.width - 1.f,
                 1.f - 2.f * rect.top / layout.height, 2.f * rect.GetWidth() / layout.width,
                 -2.f * rect.GetHeight() / layout.height);
    glUniform1i(rotation_uniform, layout.is_rotated ? 1 : 0);
    glUniform1f(opacity_uniform, std::clamp(opacity, 0.f, 1.f));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void SoftwarePresenter::Present(const SoftwareFrame* frame,
                                const Layout::FramebufferLayout& layout) {
    if (Failed() || layout.width == 0 || layout.height == 0) return;
    // CodexAstraLocal: All mutable display GL state is set in the current UI
    // context. No context-local VAO/FBO is transferred from the CPU thread.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBindVertexArray(0); // GLES has a default vertex array; gl_VertexID needs no attributes.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glViewport(0, 0, layout.width, layout.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                 Settings::values.bg_blue.GetValue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    if (frame) {
        // CodexAstraLocal: Software's mono preset still honors the selected eye.
        const u32 eye = Settings::values.mono_render_option.GetValue() ==
            Settings::MonoRenderOption::RightEye ? 1 : 0;
        const auto top = [&](float opacity) {
            if (layout.top_screen_enabled)
                Draw(frame->screens[eye], eye, layout.top_screen, layout, opacity);
        };
        const auto bottom = [&](float opacity) {
            if (layout.bottom_screen_enabled)
                Draw(frame->screens[2], 2, layout.bottom_screen, layout, opacity);
        };
        if (Settings::values.swap_screen.GetValue()) {
            bottom(1.f);
            top(layout.top_opacity);
        } else {
            top(1.f);
            bottom(layout.bottom_opacity);
        }
        if (layout.additional_screen_enabled) {
            const u32 index = layout.additional_screen_is_bottom ? 2 : eye;
            Draw(frame->screens[index], index, layout.additional_screen, layout, 1.f);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        // CodexAstraLocal: Latch UI-context errors for the emulation thread's
        // typed terminal boundary; never throw across JNI or switch renderers.
        LOG_CRITICAL(Render_Software, "CPU Software presentation failed: GL error {}", error);
        failed.store(true, std::memory_order_release);
    }
}
} // namespace SwRenderer

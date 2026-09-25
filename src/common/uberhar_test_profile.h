// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

#pragma once

#include "common/settings.h"

namespace Settings {

// AstraEH: Apply ONLY after the frontend has reloaded the user's saved settings.
// These are effective session values; Android persists its separate Kotlin model.
// Disabling a profile therefore restores the INI values without a lossy backup.
inline void ApplyUberharTestProfile() {
    const auto mode = values.uberhar_test_mode.GetValue();
    if (mode != UberharTestMode::Native && mode != UberharTestMode::Compute &&
        mode != UberharTestMode::Automatic) {
        values.uberhar_test_mode = UberharTestMode::Custom;
        return;
    }
    values.graphics_api = GraphicsAPI::Vulkan;
    values.spirv_shader_gen = true;
    // AstraEH: Generic first use must not spend seconds in optional frontend optimization.
    values.disable_spirv_optimizer = true;
    values.async_shader_compilation = false;
    values.uberhar_hybrid_tev = true;
    values.uberhar_force_tev = true;
    values.uberhar_cpu_vertex_bridge = false;
    // AstraEH: 0.0.10's reference interpreter limited battle speed even at 1x.
    // Reuse the established CPU JIT while the GPU interpreter is unfinished.
    // This still compiles CPU code on first encounter; its cost is logged separately.
    values.use_hw_shader = false;
    values.use_shader_jit = true;
    values.shaders_accurate_mul = true;
    values.use_disk_shader_cache = true;
    values.texture_filter = TextureFilter::NoFilter;
    values.texture_sampling = TextureSampling::GameControlled;
    values.delay_game_render_thread_us = 0;
    values.filter_mode = true;
    values.render_3d = StereoRenderOption::Off;
    values.factor_3d = 0;
    values.render_3d_which_display = StereoWhichDisplay::None;
    values.disable_right_eye_render = false;
    values.swap_eyes_3d = false;
    values.dump_textures = false;
    values.custom_textures = false;
    values.async_custom_loading = true;
    values.use_skip_duplicate_frames = false;
    // Resolution factor and integer scaling deliberately retain the user's values.
}

} // namespace Settings

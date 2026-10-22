// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

#pragma once

#include "common/settings.h"

namespace Settings {

// CodexAstraLocal: Shared policy for the remaining Native/Combo profiles.
constexpr bool UsesReadyGpuVertices(UberharTestMode mode) {
    // CodexAstraLocal: Retire the generic-only profile while keeping Combo capability.
    return mode == UberharTestMode::Automatic;
}

constexpr bool AllowsSpecializedFragments(UberharTestMode mode) {
    return mode == UberharTestMode::Automatic;
}

// CodexAstraLocal: CPU fragment preparation is independent of GPU vertex
// promotion. Native and Combo may reuse bounded static TEV.
constexpr bool AllowsStaticCpuTev(UberharTestMode mode) {
    return mode == UberharTestMode::Native || mode == UberharTestMode::Automatic;
}

// CodexAstraLocal: CPU-produced vertices may use an exact ready full fragment
// without granting GPU vertex execution.
// CodexAstraLocal: Native-only fragment control leaves Combo policy independent.
constexpr bool AllowsFullCpuFragments(UberharTestMode mode, bool native_full = true) {
    return (mode == UberharTestMode::Native && native_full) ||
           mode == UberharTestMode::Automatic;
}

constexpr bool UsesAutomaticCompute(UberharTestMode mode) {
    // CodexAstraLocal: Retire the generic-only profile while keeping Combo capability.
    return mode == UberharTestMode::Automatic;
}

constexpr bool AllowsComputeRendering(UberharTestMode mode) {
    // CodexAstraLocal: Only Combo retains optional compute rectangle rendering.
    return UsesAutomaticCompute(mode);
}

// CodexAstraLocal: Retired persisted IDs migrate without reusing their values.
constexpr UberharTestMode MigrateUberharTestMode(UberharTestMode mode) {
    // CodexAstraLocal: Existing IDs are never recycled. Software is retired,
    // not relabeled as CPU pixels; frontend migration reports the new Native route.
    const auto id = static_cast<u32>(mode);
    return id == 2 || id == 5 ? UberharTestMode::Native
                             : id == 4 ? UberharTestMode::Automatic : mode;
}

constexpr bool IsUberharTestProfile(UberharTestMode mode) {
    return mode == UberharTestMode::Native || AllowsComputeRendering(mode);
}

// AstraEH: Apply ONLY after the frontend has reloaded the user's saved settings.
// These are effective session values; Android persists its separate Kotlin model.
// Disabling a profile therefore restores the INI values without a lossy backup.
inline void ApplyUberharTestProfile() {
    // CodexAstraLocal: Migrate before applying any effective renderer settings.
    const auto mode = MigrateUberharTestMode(values.uberhar_test_mode.GetValue());
    values.uberhar_test_mode = mode;
    // CodexAstraLocal: Preserve invalid-value recovery after retiring legacy profiles.
    if (!IsUberharTestProfile(mode)) {
        values.uberhar_test_mode = UberharTestMode::Custom;
        return;
    }
    values.graphics_api = GraphicsAPI::Vulkan;
    // CodexAstraLocal: Preserve the saved generator comparison for ready full
    // fragments; the renderer freezes it before any module/cache lookup.
    // AstraEH: Generic first use must not spend seconds in optional frontend optimization.
    values.disable_spirv_optimizer = true;
    values.async_shader_compilation = false;
    values.uberhar_hybrid_tev = true;
    // CodexAstraLocal: Native/Combo preserve their original fragment policies.
    values.uberhar_force_tev = !AllowsSpecializedFragments(mode);
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

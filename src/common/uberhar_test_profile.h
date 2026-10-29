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

// CodexAstraLocal: Frontends resolve defaults and explicit settings before this
// shared contract. Profiles only enforce renderer requirements; independent
// preferences remain exactly as resolved, including explicit false/zero values.
inline void ApplyUberharTestProfile() {
    const auto mode = MigrateUberharTestMode(values.uberhar_test_mode.GetValue());
    values.uberhar_test_mode = mode;
    if (!IsUberharTestProfile(mode)) {
        values.uberhar_test_mode = UberharTestMode::Custom;
        return;
    }
    // CodexAstraLocal: Native/Combo require the Vulkan hybrid route and their
    // own fragment policy. Pending optional shaders must never skip draws.
    values.graphics_api = GraphicsAPI::Vulkan;
    values.async_shader_compilation = false;
    values.uberhar_hybrid_tev = true;
    values.uberhar_force_tev = !AllowsSpecializedFragments(mode);
    values.uberhar_cpu_vertex_bridge = false;
    // CodexAstraLocal: Combo's explicit CPU-only placement excludes both GPU
    // vertex routes. Otherwise respect the user's ordinary hardware preference.
    if (UsesReadyGpuVertices(mode) && values.uberhar_cpu_vertex_only.GetValue()) {
        values.use_hw_shader = false;
    }
}

} // namespace Settings

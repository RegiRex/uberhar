// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <cstdint>

namespace Vulkan::UnusedEyePolicy {
// CodexAstraLocal: The opt-in experiment recognizes only dedicated right scanout
// targets, not guessed alternating lists or reused offscreen intermediates. This
// local eligibility test does not prove absence of future guest reads; runtime
// diagnostics must report that experimental limitation and any observed hazard.
struct Input {
    std::uint32_t color{}, right{}, width{}, height{}, scan_width{}, scan_height{};
    std::array<std::uint32_t, 2> left{};
    bool mono_left{}, allowed{}, writes_depth_stencil{}, geometry{}, shadow{}, feedback{};
};
enum class Decision : std::uint32_t { Disabled, NotRight, Shape, SharedState, Eligible, Count };
constexpr Decision Classify(const Input& in) {
    if (!in.mono_left || !in.allowed) return Decision::Disabled;
    if (!in.color || !in.right || in.color != in.right ||
        in.color == in.left[0] || in.color == in.left[1]) return Decision::NotRight;
    if (!in.width || !in.height || in.width != in.scan_width || in.height != in.scan_height)
        return Decision::Shape;
    if (in.writes_depth_stencil || in.geometry || in.shadow || in.feedback)
        return Decision::SharedState;
    return Decision::Eligible;
}
} // namespace Vulkan::UnusedEyePolicy

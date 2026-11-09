// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
#pragma once

#include <algorithm>
#include <cstdint>

namespace Vulkan::PassUnionPolicy {

// CodexAstraLocal: Geometry only; framebuffer identity, hazards and submission
// are checked by RenderManager. A union changes the NEXT pass's LOAD/STORE area,
// never the currently active render pass or any draw's viewport/scissor/dirty rect.
struct Rect {
    std::int32_t x{}, y{};
    std::uint32_t width{}, height{};
    bool operator==(const Rect&) const = default;
};
enum class Decision : std::uint8_t { Accepted, Empty, Bounds, NonRectangular, AreaBudget, NoExpansion, Count };
struct Result {
    Decision decision;
    Rect area;
    explicit operator bool() const { return decision == Decision::Accepted; }
};

inline Result MakeUnion(Rect old, Rect next, std::uint32_t width, std::uint32_t height) noexcept {
    const auto refuse = [next](Decision reason) { return Result{reason, next}; };
    if (!old.width || !old.height || !next.width || !next.height || !width || !height)
        return refuse(Decision::Empty);
    const auto right = [](Rect r) { return std::int64_t{r.x} + r.width; };
    const auto bottom = [](Rect r) { return std::int64_t{r.y} + r.height; };
    const auto inside = [&](Rect r) {
        return r.x >= 0 && r.y >= 0 && right(r) <= width && bottom(r) <= height;
    };
    if (!inside(old) || !inside(next)) return refuse(Decision::Bounds);
    // Equal spans on one axis plus overlap/touch on the other prove a rectangle
    // with no gap or newly included corner. Bounding-box-only unions are unsafe.
    const bool same_x = old.x == next.x && old.width == next.width;
    const bool same_y = old.y == next.y && old.height == next.height;
    if (!((same_x && old.y <= bottom(next) && next.y <= bottom(old)) ||
          (same_y && old.x <= right(next) && next.x <= right(old))))
        return refuse(Decision::NonRectangular);
    const auto x = std::min(old.x, next.x), y = std::min(old.y, next.y);
    const Rect united{x, y, static_cast<std::uint32_t>(std::max(right(old), right(next)) - x),
                      static_cast<std::uint32_t>(std::max(bottom(old), bottom(next)) - y)};
    const auto before = std::uint64_t{next.width} * next.height;
    const auto after = std::uint64_t{united.width} * united.height;
    if (after == before) return refuse(Decision::NoExpansion);
    // Subtraction avoids overflow in 2*before at large integer boundary tests.
    if (after - before > before) return refuse(Decision::AreaBudget);
    return {Decision::Accepted, united};
}

} // namespace Vulkan::PassUnionPolicy

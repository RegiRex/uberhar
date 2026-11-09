// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>

namespace VideoCore::CpuClipReject {
// CodexAstraLocal: Exact rejection applies only to the ordinary Vulkan trivial-VS
// list AFTER guest shader execution/assembly and compute/capture route decisions.
// The caller supplies all positive proofs; default construction cannot reject.
struct Guard {
    bool enabled{}, vulkan{}, trivial_vertex_shader{}, no_geometry_shader{}, triangle_list{},
        no_compute_consumer{}, no_capture{}, no_deferred{}, no_bridge{};
    bool Eligible() const {
        return enabled && vulkan && trivial_vertex_shader && no_geometry_shader && triangle_list &&
               no_compute_consumer && no_capture && no_deferred && no_bridge;
    }
};
enum class Decision : std::uint32_t {
    Ineligible, NonFinite, NonPositiveW, Subnormal, VisibleOrCrossing, Rejected, Count
};
using Position = std::array<float, 4>;
struct Result {
    Decision decision{Decision::Ineligible};
    std::uint32_t outside_planes{}; // Original input +X,-X,+Y,-Y; Y flip swaps, never invalidates.
    bool Rejected() const { return decision == Decision::Rejected; }
};

inline Result Classify(const Guard& guard, const std::array<Position, 3>& vertices) noexcept {
    if (!guard.Eligible()) return {};
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    std::uint32_t shared = 15;
    for (const auto& vertex : vertices) {
        std::array<std::uint32_t, 4> bits{};
        for (std::size_t i = 0; i < bits.size(); ++i) {
            bits[i] = std::bit_cast<std::uint32_t>(vertex[i]);
            const auto magnitude = bits[i] & 0x7fffffffU;
            if ((magnitude & 0x7f800000U) == 0x7f800000U) return {Decision::NonFinite, 0};
            // GPU denorm preservation is not established. This is a strict fallback,
            // not an epsilon/area test; preserve even apparently invisible subnormals.
            if (magnitude && !(magnitude & 0x7f800000U)) return {Decision::Subnormal, 0};
        }
        if ((bits[3] & 0x80000000U) || !(bits[3] & 0x7fffffffU))
            return {Decision::NonPositiveW, 0};
        // CodexAstraLocal: Positive normal W permits exact sign/magnitude ordering.
        // No float arithmetic, divide, Z clipping, rounding mode or FP status changes.
        std::uint32_t outside = 0;
        for (unsigned i = 0; i < 2; ++i) {
            if ((bits[i] & 0x7fffffffU) > bits[3])
                outside |= 1U << (2 * i + ((bits[i] >> 31) & 1U));
        }
        shared &= outside;
    }
    return {shared ? Decision::Rejected : Decision::VisibleOrCrossing, shared};
}

struct Compaction {
    std::size_t input_vertices{}, output_vertices{}, inspected_triangles{}, rejected_triangles{},
        avoided_upload_bytes{};
    std::array<std::uint64_t, static_cast<std::size_t>(Decision::Count)> decisions{};
    bool malformed{};
};

// CodexAstraLocal: Stable whole-triangle compaction of already packed records.
// Untouched/kept bytes are identical, including quaternion correction and padding.
// OFF/ineligible returns before position reads, copies, allocations or timing.
template <typename Vertex, std::size_t Extent, typename GetPosition>
Compaction Compact(std::span<Vertex, Extent> vertices, const Guard& guard, GetPosition&& position) noexcept {
    static_assert(std::is_trivially_copyable_v<Vertex>);
    Compaction result;
    result.input_vertices = result.output_vertices = vertices.size();
    if (!guard.Eligible()) return result;
    if (vertices.size() % 3) { result.malformed = true; return result; }
    std::size_t output = 0;
    for (std::size_t input = 0; input < vertices.size(); input += 3) {
        const auto decision = Classify(guard, {position(vertices[input]), position(vertices[input + 1]),
                                              position(vertices[input + 2])});
        ++result.inspected_triangles;
        ++result.decisions[static_cast<std::size_t>(decision.decision)];
        if (decision.Rejected()) { ++result.rejected_triangles; continue; }
        if (output != input)
            std::memmove(vertices.data() + output, vertices.data() + input, 3 * sizeof(Vertex));
        output += 3;
    }
    result.output_vertices = output;
    result.avoided_upload_bytes = (vertices.size() - output) * sizeof(Vertex);
    return result;
}
} // namespace VideoCore::CpuClipReject

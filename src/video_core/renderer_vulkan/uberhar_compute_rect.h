// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include "video_core/pica/regs_internal.h"

namespace Vulkan {

// AstraEH: The first compute rasterizer accepts only an exactly recognizable solid
// rectangle. No textures, clipping, depth, stencil, blending or interpolation are
// approximated. Unsupported states return to the native interpreter before submission.
struct ComputeRectPacket {
    std::array<s32, 4> rect; // x, y, width, height in the current surface's pixels.
    u32 color;               // RGBA8 in the R32_UINT storage view's little-endian order.
    std::array<u32, 3> padding{};

    // AstraEH: Keep area arithmetic unsigned even for the largest admitted rectangle.
    u64 PixelCount() const {
        return static_cast<u64>(rect[2]) * static_cast<u64>(rect[3]);
    }
};
static_assert(sizeof(ComputeRectPacket) == 32);

// AstraEH: Non-exclusive reasons explain zero coverage without logging every draw.
// Admission is unchanged: every unsafe component still selects native rendering.
enum class ComputeRectReject : unsigned {
    Shadow,
    ColorWrite,
    DepthTest,
    DepthWrite,
    Stencil,
    Alpha,
    Clip,
    Scissor,
    Cull,
    Fog,
    Blend,
    Count
};
inline u32 ComputeRectStateRejections(const Pica::RegsInternal& regs) {
    using FB = Pica::FramebufferRegs;
    using R = Pica::RasterizerRegs;
    const auto& fb = regs.framebuffer;
    const auto& om = fb.output_merger;
    const auto cull = regs.rasterizer.cull_mode.Value();
    u32 reasons = 0;
    const auto reject = [&](ComputeRectReject reason, bool condition) {
        if (condition)
            reasons |= 1U << static_cast<unsigned>(reason);
    };
    reject(ComputeRectReject::Shadow, fb.IsShadowRendering());
    reject(ComputeRectReject::ColorWrite,
           fb.framebuffer.allow_color_write == 0 || ((om.depth_color_mask >> 8) & 15) != 15);
    reject(ComputeRectReject::DepthTest, om.depth_test_enable != 0);
    reject(ComputeRectReject::DepthWrite, om.depth_write_enable != 0);
    reject(ComputeRectReject::Stencil, om.stencil_test.enable != 0);
    reject(ComputeRectReject::Alpha, om.alpha_test.enable != 0);
    reject(ComputeRectReject::Clip, regs.rasterizer.clip_enable != 0);
    reject(ComputeRectReject::Scissor,
           regs.rasterizer.scissor_test.mode != R::ScissorMode::Disabled);
    reject(ComputeRectReject::Cull, cull != R::CullMode::KeepAll && cull != R::CullMode::KeepAll2);
    reject(ComputeRectReject::Fog, regs.texturing.fog_mode != Pica::TexturingRegs::FogMode::None);
    const bool replace = om.alphablend_enable
                             ? om.alpha_blending.blend_equation_rgb == FB::BlendEquation::Add &&
                                   om.alpha_blending.blend_equation_a == FB::BlendEquation::Add &&
                                   om.alpha_blending.factor_source_rgb == FB::BlendFactor::One &&
                                   om.alpha_blending.factor_source_a == FB::BlendFactor::One &&
                                   om.alpha_blending.factor_dest_rgb == FB::BlendFactor::Zero &&
                                   om.alpha_blending.factor_dest_a == FB::BlendFactor::Zero
                             : om.logic_op == FB::LogicOp::Copy;
    reject(ComputeRectReject::Blend, !replace);
    return reasons;
}
inline bool SupportsComputeRectState(const Pica::RegsInternal& regs) {
    return ComputeRectStateRejections(regs) == 0;
}

template <typename Vertex>
std::optional<ComputeRectPacket> MakeComputeRect(const Pica::RegsInternal& regs,
                                                 std::span<const Vertex> vertices,
                                                 const std::array<s32, 4>& viewport,
                                                 const std::array<s32, 4>& scissor) {
    if (vertices.size() != 6 || !SupportsComputeRectState(regs))
        return {};

    // AstraEH: Project exactly as the trivial Vulkan VS does, and accept only
    // integral edges wholly inside the clip volume. This avoids emulating edge
    // precision or clipping rules before they have independent correctness tests.
    std::array<std::array<s32, 2>, 6> points;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& p = vertices[i].position;
        for (unsigned c = 0; c < 4; ++c) {
            if (!std::isfinite(p[c]) || !std::isfinite(vertices[i].color[c]) ||
                vertices[i].color[c] != vertices[0].color[c])
                return {};
        }
        if (p.w <= 0 || std::abs(p.x) > p.w || std::abs(p.y) > p.w || p.z > 0 || p.z < -p.w)
            return {};
        const float y = regs.framebuffer.framebuffer.IsFlipped() ? -p.y : p.y;
        const std::array<float, 2> screen{viewport[0] + (p.x / p.w + 1.f) * (viewport[2] * 0.5f),
                                          viewport[1] + (y / p.w + 1.f) * (viewport[3] * 0.5f)};
        for (unsigned c = 0; c < 2; ++c) {
            if (!std::isfinite(screen[c]) || std::abs(screen[c]) > 65536.f ||
                std::abs(screen[c] - std::round(screen[c])) > 0.0001f)
                return {};
            points[i][c] = static_cast<s32>(std::round(screen[c]));
        }
    }
    s32 x0 = points[0][0], x1 = x0, y0 = points[0][1], y1 = y0;
    for (const auto& p : points) {
        x0 = std::min(x0, p[0]);
        x1 = std::max(x1, p[0]);
        y0 = std::min(y0, p[1]);
        y1 = std::max(y1, p[1]);
    }
    if (x0 == x1 || y0 == y1)
        return {};
    std::array<unsigned, 2> masks{};
    for (unsigned i = 0; i < 6; ++i) {
        const auto& p = points[i];
        if ((p[0] != x0 && p[0] != x1) || (p[1] != y0 && p[1] != y1))
            return {};
        const unsigned bit = 1u << ((p[0] == x1 ? 1 : 0) + (p[1] == y1 ? 2 : 0));
        if (masks[i / 3] & bit)
            return {};
        masks[i / 3] |= bit;
    }
    const unsigned common = masks[0] & masks[1];
    if ((masks[0] | masks[1]) != 15 || (common != 6 && common != 9))
        return {};

    // AstraEH: Evaluate constant Replace-only combiners as data. Source/operand
    // decoding is bounded to six stages; no executable shader code is generated.
    using T = Pica::TexturingRegs::TevStageConfig;
    std::array<u32, 4> primary{}, previous{};
    for (unsigned c = 0; c < 4; ++c) {
        const float color = vertices[0].color[c];
        if (color < 0.f || color > 1.f)
            return {};
        const float scaled = color * 255.f;
        // Tie rounding differs across hardware; defer those ambiguous inputs.
        if (std::abs((scaled - std::floor(scaled)) - 0.5f) < 0.0001f)
            return {};
        primary[c] = static_cast<u32>(std::round(scaled));
    }
    unsigned index = 0;
    for (const auto& stage : regs.texturing.GetTevStages()) {
        if (stage.color_op != T::Operation::Replace || stage.alpha_op != T::Operation::Replace ||
            stage.color_modifier1 != T::ColorModifier::SourceColor ||
            stage.alpha_modifier1 != T::AlphaModifier::SourceAlpha || stage.color_scale != 0 ||
            stage.alpha_scale != 0)
            return {};
        const auto source = [&](T::Source src, unsigned c) -> std::optional<u32> {
            if (src == T::Source::PrimaryColor)
                return primary[c];
            if (src == T::Source::Constant)
                return (stage.const_color >> (c * 8)) & 255;
            if (src == T::Source::Previous && index != 0)
                return previous[c];
            return {};
        };
        std::array<u32, 4> output{};
        for (unsigned c = 0; c < 4; ++c) {
            const auto value =
                source(c == 3 ? stage.alpha_source1.Value() : stage.color_source1.Value(), c);
            if (!value)
                return {};
            output[c] = *value;
        }
        previous = output;
        ++index;
    }
    x0 = std::max(x0, scissor[0]);
    y0 = std::max(y0, scissor[1]);
    x1 = std::min(x1, scissor[2]);
    y1 = std::min(y1, scissor[3]);
    if (x1 <= x0 || y1 <= y0 || x0 < 0 || y0 < 0)
        return {};
    return ComputeRectPacket{{x0, y0, x1 - x0, y1 - y0},
                             previous[0] | previous[1] << 8 | previous[2] << 16 |
                                 previous[3] << 24};
}

// AstraEH: Learn GPU execution costs only from comparable solid-rectangle size
// buckets. No duplicate rendering or blocking benchmark is needed. Exploration
// continues occasionally because clocks and workload costs can change.
class ComputeRectSelector {
public:
    static constexpr unsigned Buckets = 8;
    static unsigned Bucket(u64 pixels) {
        unsigned b = 0;
        for (; b + 1 < Buckets && pixels > 256; ++b)
            pixels = pixels / 4 + (pixels % 4 != 0);
        return b;
    }
    bool Select(unsigned bucket) {
        auto& b = buckets[bucket];
        const auto turn = b.turn++;
        if (b.count[0] < 2 || b.count[1] < 2 || turn % 64 < 2)
            return (turn & 1) != 0;
        return b.ns[1] < b.ns[0];
    }
    // AstraEH: Periodic exploration must actually be sampled. A global every-N
    // throttle can otherwise alias with alternating routes and never learn one.
    bool NeedsSample(unsigned bucket) const {
        const auto& b = buckets[bucket];
        return b.count[0] < 2 || b.count[1] < 2 || (b.turn && (b.turn - 1) % 64 < 2);
    }
    void Record(unsigned bucket, bool compute, double ns) {
        if (!std::isfinite(ns) || ns <= 0)
            return;
        auto& b = buckets[bucket];
        b.ns[compute] = b.count[compute]++ ? b.ns[compute] * 0.875 + ns * 0.125 : ns;
    }

private:
    struct BucketData {
        std::array<double, 2> ns{};
        std::array<u64, 2> count{};
        u64 turn{};
    };
    std::array<BucketData, Buckets> buckets{};
};

} // namespace Vulkan

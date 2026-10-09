// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <optional>
#include <span>
#include "video_core/pica/regs_internal.h"

namespace Vulkan {

// AstraEH: The first compute rasterizer accepts only an exactly recognizable solid
// rectangle. No textures, clipping, depth, stencil, blending or interpolation are
// approximated.
// CodexAstraLocal: This helper proves only the compute packet. The caller keeps
// graphics recovery in other modes, while strict Calculated records omissions;
// both policies follow CPU vertices and neither invokes a native interpreter here.
struct ComputeRectPacket {
    std::array<s32, 4> rect; // x, y, width, height in the current surface's pixels.
    u32 color;               // RGBA8 in the R32_UINT storage view's little-endian order.
    // CodexAstraLocal: Replace one unused padding word with a byte-lane mask;
    // old full-color packets retain their exact overwrite behavior and 32B ABI.
    u32 byte_mask{0xffffffff};
    std::array<u32, 2> padding{};

    // AstraEH: Keep area arithmetic unsigned even for the largest admitted rectangle.
    u64 PixelCount() const {
        return static_cast<u64>(rect[2]) * static_cast<u64>(rect[3]);
    }
};
static_assert(sizeof(ComputeRectPacket) == 32);

// AstraEH: Non-exclusive reasons explain zero coverage without logging every draw.
// CodexAstraLocal: Unsafe components still reject this subset; caller policy
// chooses complete graphics recovery or an explicit strict-mode omission.
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
// CodexAstraLocal: Internal exact proofs serve only newly admitted
// masked/endpoint replacements. Prepared state replaces the unused boolean-only
// wrapper while retaining raw rejection diagnostics and the inherited shape path.
namespace ComputeRectSupport {
using FB = Pica::FramebufferRegs;
using T = Pica::TexturingRegs::TevStageConfig;
// CodexAstraLocal: Decode a canonical signed 2^-16 grid with integer operations.
// The supported viewport bounds make each admitted projection intermediate an
// exactly representable binary32 value; no fuzzy endpoint comparison is used.
inline std::optional<s32> Grid(float value) {
    const u32 bits = std::bit_cast<u32>(value);
    if (bits == 0)
        return 0;
    const u32 magnitude = bits & 0x7fffffff;
    const u32 exponent = magnitude >> 23;
    if (exponent < 111 || exponent > 127 || magnitude > 0x3f800000)
        return {};
    const u32 shift = 134 - exponent; // 7..23, never an undefined-width shift.
    const u32 significand = (magnitude & 0x7fffff) | 0x800000;
    if (significand & ((u32{1} << shift) - 1))
        return {};
    const s32 q = static_cast<s32>(significand >> shift);
    return bits >> 31 ? -q : q;
}

template <typename Vertex>
bool StrictGeometry(std::span<const Vertex> vertices, const std::array<s32, 4>& viewport,
                    bool flip) {
    if (vertices.size() != 6)
        return false;
    for (unsigned axis = 0; axis < 2; ++axis) {
        if (viewport[axis] < -65536 || viewport[axis] > 65536 ||
            viewport[axis + 2] <= 0 || viewport[axis + 2] > 65536)
            return false;
    }
    std::array<std::array<s32, 2>, 6> points{};
    std::array<std::array<u32, 4>, 6> original{};
    for (unsigned i = 0; i < 6; ++i) {
        const auto& p = vertices[i].position;
        if (std::bit_cast<u32>(p.w) != 0x3f800000 || !std::isfinite(p.z) ||
            p.z < -1.f || p.z > -0.25f)
            return false;
        // CodexAstraLocal: Keeping Z in this clip interval and away from zero
        // avoids the inherited epsilon sanitation branch in this new-route proof.
        original[i] = {std::bit_cast<u32>(p.x), std::bit_cast<u32>(p.y),
                       std::bit_cast<u32>(p.z), std::bit_cast<u32>(p.w)};
        const std::array<float, 2> xy{p.x, p.y};
        for (unsigned axis = 0; axis < 2; ++axis) {
            auto q = Grid(xy[axis]);
            if (!q)
                return false;
            if (axis == 1 && flip)
                *q = -*q;
            const std::int64_t numerator = (std::int64_t(*q) + 65536) * viewport[axis + 2];
            if (numerator % 131072 != 0)
                return false;
            const std::int64_t screen = viewport[axis] + numerator / 131072;
            if (screen < -65536 || screen > 65536)
                return false;
            points[i][axis] = static_cast<s32>(screen);
        }
    }
    // CodexAstraLocal: Equal projected duplicates must really share all original
    // homogeneous coordinates. This is independent of the old rounded shape test.
    for (unsigned i = 0; i < 6; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (points[i] == points[j] && original[i] != original[j])
                return false;
    return true;
}

// CodexAstraLocal: This is a sufficient endpoint proof through the actual
// supported Replace chain, not an inference from packet byte quantization.
// Arbitrary near-zero/one primary values remain conservatively unknown even
// where the production shader's first byte-round could produce an endpoint.
template <typename Vertex>
std::optional<unsigned> FinalAlphaEndpoint(const Pica::RegsInternal& regs,
                                          std::span<const Vertex> vertices) {
    if (vertices.size() != 6)
        return {};
    std::optional<unsigned> previous;
    unsigned index = 0;
    for (const auto& stage : regs.texturing.GetTevStages()) {
        if (stage.alpha_op != T::Operation::Replace ||
            stage.alpha_modifier1 != T::AlphaModifier::SourceAlpha || stage.alpha_scale != 0)
            return {};
        std::optional<unsigned> output;
        switch (stage.alpha_source1.Value()) {
        case T::Source::PrimaryColor: {
            const float alpha = vertices[0].color[3];
            bool equal = true;
            for (const auto& vertex : vertices)
                equal &= vertex.color[3] == alpha;
            if (equal && (alpha == 0.f || alpha == 1.f))
                output = alpha == 1.f ? 1 : 0;
            break;
        }
        case T::Source::Constant: {
            const u32 alpha = stage.const_color >> 24;
            if (alpha == 0 || alpha == 255)
                output = alpha == 255 ? 1 : 0;
            break;
        }
        case T::Source::Previous:
            if (index == 0)
                return {};
            output = previous;
            break;
        default:
            return {};
        }
        previous = output;
        ++index;
    }
    return previous;
}

inline std::optional<unsigned> Factor(FB::BlendFactor factor,
                                      std::optional<unsigned> alpha) {
    switch (factor) {
    case FB::BlendFactor::Zero: return 0;
    case FB::BlendFactor::One: return 1;
    case FB::BlendFactor::SourceAlpha: return alpha;
    case FB::BlendFactor::OneMinusSourceAlpha:
        return alpha ? std::optional<unsigned>{1 - *alpha} : std::nullopt;
    default: return {};
    }
}

} // namespace ComputeRectSupport

// CodexAstraLocal: Retain the raw diagnostic mask separately from effective
// admission and carry only immutable draw-local proof data, never a register copy.
struct ComputeRectState {
    u32 raw_rejections{};
    u32 byte_mask{};
    bool exact_geometry{};
    explicit operator bool() const { return byte_mask != 0; }
};

template <typename Vertex>
ComputeRectState PrepareComputeRectState(const Pica::RegsInternal& regs,
                                        std::span<const Vertex> vertices,
                                        bool allow_expanded = true) {
    using FB = Pica::FramebufferRegs;
    const u32 raw = ComputeRectStateRejections(regs);
    if (!raw)
        return {0, 0xffffffff, false};
    constexpr u32 allowed = (1U << unsigned(ComputeRectReject::ColorWrite)) |
                            (1U << unsigned(ComputeRectReject::Blend));
    // CodexAstraLocal: Native preserves its inherited state gate and performs no
    // new endpoint/strict-geometry work. Unsupported modes never enable compute.
    if (!allow_expanded || (raw & ~allowed))
        return {raw, 0, true};
    const auto& om = regs.framebuffer.output_merger;
    const u32 channels = regs.framebuffer.framebuffer.allow_color_write != 0
                             ? (om.depth_color_mask >> 8) & 15 : 0;
    if (!channels)
        return {raw, 0, true};
    if (raw & (1U << unsigned(ComputeRectReject::Blend))) {
        if (!om.alphablend_enable)
            return {raw, 0, true};
        const auto alpha = ComputeRectSupport::FinalAlphaEndpoint(regs, vertices);
        const auto& blend = om.alpha_blending;
        const bool replace = blend.blend_equation_rgb == FB::BlendEquation::Add &&
                             blend.blend_equation_a == FB::BlendEquation::Add &&
                             ComputeRectSupport::Factor(blend.factor_source_rgb, alpha) == 1 &&
                             ComputeRectSupport::Factor(blend.factor_source_a, alpha) == 1 &&
                             ComputeRectSupport::Factor(blend.factor_dest_rgb, alpha) == 0 &&
                             ComputeRectSupport::Factor(blend.factor_dest_a, alpha) == 0;
        if (!replace)
            return {raw, 0, true};
    }
    // CodexAstraLocal: A nonzero mask changes enabled bytes and preserves the
    // others. Source-zero/destination-one no-op blending was rejected above.
    u32 mask = 0;
    for (unsigned channel = 0; channel < 4; ++channel)
        if (channels & (1U << channel))
            mask |= 0xffU << (channel * 8);
    return {raw, mask, true};
}

// CodexAstraLocal: Reuse the original shape/color work after state preparation,
// without normalizing or copying all 3072 bytes of guest registers per draw.
template <typename Vertex>
std::optional<ComputeRectPacket> MakeComputeRectPrepared(const Pica::RegsInternal& regs,
                                                 std::span<const Vertex> vertices,
                                                 const std::array<s32, 4>& viewport,
                                                 const std::array<s32, 4>& scissor,
                                                 const ComputeRectState& state) {
    if (vertices.size() != 6 || !state)
        return {};
    // CodexAstraLocal: Only new masked/endpoint admissions require the stricter
    // dyadic/shared-endpoint certificate; old accepts keep their inherited test.
    if (state.exact_geometry && !ComputeRectSupport::StrictGeometry(
            vertices, viewport, regs.framebuffer.framebuffer.IsFlipped()))
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
                                 previous[3] << 24,
                             state.byte_mask};
}

// CodexAstraLocal: Standalone callers still use one complete admission entry;
// the renderer carries its already-prepared state to avoid duplicate decoding.
template <typename Vertex>
std::optional<ComputeRectPacket> MakeComputeRect(const Pica::RegsInternal& regs,
                                               std::span<const Vertex> vertices,
                                               const std::array<s32, 4>& viewport,
                                               const std::array<s32, 4>& scissor) {
    return MakeComputeRectPrepared(regs, vertices, viewport, scissor,
                                   PrepareComputeRectState(regs, vertices));
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

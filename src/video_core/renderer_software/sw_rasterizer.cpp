// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/container/static_vector.hpp>
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/quaternion.h"
#include "common/settings.h" // CodexAstraLocal: Restart-only software participant budget.
#include "common/thread.h" // CodexAstraLocal: Identify sleeping raster workers in ordinary logs.
#include "common/vector_math.h"
#include "core/memory.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_software/sw_framebuffer.h"
#include "video_core/renderer_software/sw_lighting.h"
#include "video_core/renderer_software/sw_proctex.h"
#include "video_core/renderer_software/sw_rasterizer.h"
#include "video_core/renderer_software/sw_texturing.h"
#include "video_core/texture/texture_decode.h"
#include "video_core/shader_recovery_error.h"

namespace SwRenderer {

using Pica::f24;
using Pica::FramebufferRegs;
using Pica::RasterizerRegs;
using Pica::TexturingRegs;
using Pica::Texture::LookupTexture;
using Pica::Texture::TextureInfo;

// Certain games render 2D elements very close to clip plane 0 resulting in very tiny
// negative/positive z values when computing with f32 precision,
// causing some vertices to get erroneously clipped. To workaround this problem,
// we can use a very small epsilon value for clip plane comparison.
constexpr f32 EPSILON_Z = 0.00000001f;

struct Vertex : Pica::OutputVertex {
    Vertex(const OutputVertex& v) : OutputVertex(v) {}

    /// Attributes used to store intermediate results position after perspective divide.
    Common::Vec3<f24> screenpos;

    /**
     * Linear interpolation
     * factor: 0=this, 1=vtx
     * Note: This function cannot be called after perspective divide.
     **/
    void Lerp(f24 factor, const Vertex& vtx) {
        pos = pos * factor + vtx.pos * (f24::One() - factor);
        quat = quat * factor + vtx.quat * (f24::One() - factor);
        color = color * factor + vtx.color * (f24::One() - factor);
        tc0 = tc0 * factor + vtx.tc0 * (f24::One() - factor);
        tc1 = tc1 * factor + vtx.tc1 * (f24::One() - factor);
        tc0_w = tc0_w * factor + vtx.tc0_w * (f24::One() - factor);
        view = view * factor + vtx.view * (f24::One() - factor);
        tc2 = tc2 * factor + vtx.tc2 * (f24::One() - factor);
    }

    /**
     * Linear interpolation
     * factor: 0=v0, 1=v1
     * Note: This function cannot be called after perspective divide.
     **/
    static Vertex Lerp(f24 factor, const Vertex& v0, const Vertex& v1) {
        Vertex ret = v0;
        ret.Lerp(factor, v1);
        return ret;
    }
};

namespace {

MICROPROFILE_DEFINE(GPU_Rasterization, "GPU", "Rasterization", MP_RGB(50, 50, 240));

struct ClippingEdge {
public:
    constexpr ClippingEdge(Common::Vec4<f24> coeffs,
                           Common::Vec4<f24> bias = Common::Vec4<f24>(f24::Zero(), f24::Zero(),
                                                                      f24::Zero(), f24::Zero()))
        : pos(f24::Zero()), coeffs(coeffs), bias(bias) {}

    bool IsInside(const Vertex& vertex) const {
        return Common::Dot(vertex.pos + bias, coeffs) >= f24::FromFloat32(-EPSILON_Z);
    }

    bool IsOutSide(const Vertex& vertex) const {
        return !IsInside(vertex);
    }

    Vertex GetIntersection(const Vertex& v0, const Vertex& v1) const {
        const f24 dp = Common::Dot(v0.pos + bias, coeffs);
        const f24 dp_prev = Common::Dot(v1.pos + bias, coeffs);
        const f24 factor = dp_prev / (dp_prev - dp);
        return Vertex::Lerp(factor, v0, v1);
    }

private:
    [[maybe_unused]] f24 pos;
    Common::Vec4<f24> coeffs;
    Common::Vec4<f24> bias;
};

} // Anonymous namespace

RasterizerSoftware::RasterizerSoftware(Memory::MemorySystem& memory_, Pica::PicaCore& pica_)
    : memory{memory_}, pica{pica_}, regs{pica.regs.internal},
      // CodexAstraLocal: Query the process allowance once, without fixed core
      // IDs. Auto can use every permitted CPU; explicit 1/2 includes the owner.
      sw_workers{[] {
          const auto available = Common::Uberhar::AvailableProcessors();
          const auto requested = Settings::values.software_renderer_workers.GetValue();
          return requested == 0 ? available : std::min(available, requested);
      }(), +[] { Common::SetCurrentThreadName("Software raster"); }},
      fb{memory, regs.framebuffer} {
    // CodexAstraLocal: Permanent effective configuration, emitted once per renderer.
    LOG_INFO(Render_Software, "CPU Software rendering participants={} (owner included), wait={}",
             sw_workers.Available(), sw_workers.WaitTransport());
}

void RasterizerSoftware::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                                     const Pica::OutputVertex& v2) {
    /**
     * Clipping a planar n-gon against a plane will remove at least 1 vertex and introduces 2 at
     * the new edge (or less in degenerate cases). As such, we can say that each clipping plane
     * introduces at most 1 new vertex to the polygon. Since we start with a triangle and have a
     * fixed 6 clipping planes, the maximum number of vertices of the clipped polygon is 3 + 6 = 9.
     **/
    static constexpr std::size_t MAX_VERTICES = 9;

    boost::container::static_vector<Vertex, MAX_VERTICES> buffer_a = {v0, v1, v2};
    boost::container::static_vector<Vertex, MAX_VERTICES> buffer_b;

    FlipQuaternionIfOpposite(buffer_a[1].quat, buffer_a[0].quat);
    FlipQuaternionIfOpposite(buffer_a[2].quat, buffer_a[0].quat);

    auto* output_list = &buffer_a;
    auto* input_list = &buffer_b;

    // NOTE: We clip against a w=epsilon plane to guarantee that the output has a positive w value.
    // TODO: Not sure if this is a valid approach. Also should probably instead use the smallest
    //       epsilon possible within f24 accuracy.
    static constexpr f24 EPSILON = f24::FromFloat32(0.00001f);
    static constexpr f24 f0 = f24::Zero();
    static constexpr f24 f1 = f24::One();
    static constexpr std::array<ClippingEdge, 7> clipping_edges = {{
        {Common::MakeVec(-f1, f0, f0, f1)},                                        // x = +w
        {Common::MakeVec(f1, f0, f0, f1)},                                         // x = -w
        {Common::MakeVec(f0, -f1, f0, f1)},                                        // y = +w
        {Common::MakeVec(f0, f1, f0, f1)},                                         // y = -w
        {Common::MakeVec(f0, f0, -f1, f0)},                                        // z =  0
        {Common::MakeVec(f0, f0, f1, f1)},                                         // z = -w
        {Common::MakeVec(f0, f0, f0, f1), Common::Vec4<f24>(f0, f0, f0, EPSILON)}, // w = EPSILON
    }};

    // Simple implementation of the Sutherland-Hodgman clipping algorithm.
    // TODO: Make this less inefficient (currently lots of useless buffering overhead happens here)
    const auto clip = [&](const ClippingEdge& edge) {
        std::swap(input_list, output_list);
        output_list->clear();

        const Vertex* reference_vertex = &input_list->back();
        for (const auto& vertex : *input_list) {
            // NOTE: This algorithm changes vertex order in some cases!
            if (edge.IsInside(vertex)) {
                if (edge.IsOutSide(*reference_vertex)) {
                    output_list->push_back(edge.GetIntersection(vertex, *reference_vertex));
                }
                output_list->push_back(vertex);
            } else if (edge.IsInside(*reference_vertex)) {
                output_list->push_back(edge.GetIntersection(vertex, *reference_vertex));
            }
            reference_vertex = &vertex;
        }
    };

    for (const ClippingEdge& edge : clipping_edges) {
        clip(edge);
        if (output_list->size() < 3) {
            return;
        }
    }

    if (regs.rasterizer.clip_enable) {
        const ClippingEdge custom_edge{regs.rasterizer.GetClipCoef()};
        clip(custom_edge);
        if (output_list->size() < 3) {
            return;
        }
    }

    MakeScreenCoords((*output_list)[0]);
    MakeScreenCoords((*output_list)[1]);

    for (std::size_t i = 0; i < output_list->size() - 2; i++) {
        Vertex& vtx0 = (*output_list)[0];
        Vertex& vtx1 = (*output_list)[i + 1];
        Vertex& vtx2 = (*output_list)[i + 2];

        MakeScreenCoords(vtx2);

        LOG_TRACE(
            Render_Software,
            "Triangle {}/{} at position ({:.3}, {:.3}, {:.3}, {:.3f}), "
            "({:.3}, {:.3}, {:.3}, {:.3}), ({:.3}, {:.3}, {:.3}, {:.3}) and "
            "screen position ({:.2}, {:.2}, {:.2}), ({:.2}, {:.2}, {:.2}), ({:.2}, {:.2}, {:.2})",
            i + 1, output_list->size() - 2, vtx0.pos.x.ToFloat32(), vtx0.pos.y.ToFloat32(),
            vtx0.pos.z.ToFloat32(), vtx0.pos.w.ToFloat32(), vtx1.pos.x.ToFloat32(),
            vtx1.pos.y.ToFloat32(), vtx1.pos.z.ToFloat32(), vtx1.pos.w.ToFloat32(),
            vtx2.pos.x.ToFloat32(), vtx2.pos.y.ToFloat32(), vtx2.pos.z.ToFloat32(),
            vtx2.pos.w.ToFloat32(), vtx0.screenpos.x.ToFloat32(), vtx0.screenpos.y.ToFloat32(),
            vtx0.screenpos.z.ToFloat32(), vtx1.screenpos.x.ToFloat32(),
            vtx1.screenpos.y.ToFloat32(), vtx1.screenpos.z.ToFloat32(),
            vtx2.screenpos.x.ToFloat32(), vtx2.screenpos.y.ToFloat32(),
            vtx2.screenpos.z.ToFloat32());

        ProcessTriangle(vtx0, vtx1, vtx2);
    }
}

void RasterizerSoftware::MakeScreenCoords(Vertex& vtx) {
    Viewport viewport{};
    viewport.halfsize_x = f24::FromRaw(regs.rasterizer.viewport_size_x);
    viewport.halfsize_y = f24::FromRaw(regs.rasterizer.viewport_size_y);
    viewport.offset_x = f24::FromFloat32(static_cast<f32>(regs.rasterizer.viewport_corner.x));
    viewport.offset_y = f24::FromFloat32(static_cast<f32>(regs.rasterizer.viewport_corner.y));

    f24 inv_w = f24::One() / vtx.pos.w;
    vtx.pos.w = inv_w;
    vtx.quat *= inv_w;
    vtx.color *= inv_w;
    vtx.tc0 *= inv_w;
    vtx.tc1 *= inv_w;
    vtx.tc0_w *= inv_w;
    vtx.view *= inv_w;
    vtx.tc2 *= inv_w;

    vtx.screenpos[0] = (vtx.pos.x * inv_w + f24::One()) * viewport.halfsize_x + viewport.offset_x;
    vtx.screenpos[1] = (vtx.pos.y * inv_w + f24::One()) * viewport.halfsize_y + viewport.offset_y;
    vtx.screenpos[2] = vtx.pos.z * inv_w;
}

void RasterizerSoftware::ProcessTriangle(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                                         bool reversed) {
    MICROPROFILE_SCOPE(GPU_Rasterization);

    // Vertex positions in rasterizer coordinates
    static auto screen_to_rasterizer_coords = [](const Common::Vec3<f24>& vec) {
        return Common::Vec3{Fix12P4::FromFloat24(vec.x), Fix12P4::FromFloat24(vec.y),
                            Fix12P4::FromFloat24(vec.z)};
    };

    const std::array<Common::Vec3<Fix12P4>, 3> vtxpos = {
        screen_to_rasterizer_coords(v0.screenpos),
        screen_to_rasterizer_coords(v1.screenpos),
        screen_to_rasterizer_coords(v2.screenpos),
    };

    if (regs.rasterizer.cull_mode == RasterizerRegs::CullMode::KeepAll ||
        regs.rasterizer.cull_mode == RasterizerRegs::CullMode::KeepAll2) {
        // Make sure we always end up with a triangle wound counter-clockwise
        if (!reversed && SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), vtxpos[2].xy()) <= 0) {
            ProcessTriangle(v0, v2, v1, true);
            return;
        }
    } else {
        if (!reversed && regs.rasterizer.cull_mode == RasterizerRegs::CullMode::KeepClockWise) {
            // Reverse vertex order and use the CCW code path.
            ProcessTriangle(v0, v2, v1, true);
            return;
        }
        // Cull away triangles which are wound clockwise.
        if (SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), vtxpos[2].xy()) <= 0) {
            return;
        }
    }

    u16 min_x = std::min({vtxpos[0].x, vtxpos[1].x, vtxpos[2].x});
    u16 min_y = std::min({vtxpos[0].y, vtxpos[1].y, vtxpos[2].y});
    u16 max_x = std::max({vtxpos[0].x, vtxpos[1].x, vtxpos[2].x});
    u16 max_y = std::max({vtxpos[0].y, vtxpos[1].y, vtxpos[2].y});

    // Convert the scissor box coordinates to 12.4 fixed point
    const u16 scissor_x1 = static_cast<u16>(regs.rasterizer.scissor_test.x1 << 4);
    const u16 scissor_y1 = static_cast<u16>(regs.rasterizer.scissor_test.y1 << 4);
    // x2,y2 have +1 added to cover the entire sub-pixel area
    const u16 scissor_x2 = static_cast<u16>((regs.rasterizer.scissor_test.x2 + 1) << 4);
    const u16 scissor_y2 = static_cast<u16>((regs.rasterizer.scissor_test.y2 + 1) << 4);

    if (regs.rasterizer.scissor_test.mode == RasterizerRegs::ScissorMode::Include) {
        // Calculate the new bounds
        min_x = std::max(min_x, scissor_x1);
        min_y = std::max(min_y, scissor_y1);
        max_x = std::min(max_x, scissor_x2);
        max_y = std::min(max_y, scissor_y2);
    }

    min_x &= Fix12P4::IntMask();
    min_y &= Fix12P4::IntMask();
    max_x = ((max_x + Fix12P4::FracMask()) & Fix12P4::IntMask());
    max_y = ((max_y + Fix12P4::FracMask()) & Fix12P4::IntMask());

    // CodexAstraLocal: The CPU framebuffer's visible dimensions bound every
    // Morton write; viewport/scissor alone need not stay within that storage.
    max_x = std::min<u16>(max_x, regs.framebuffer.framebuffer.GetWidth() << 4);
    max_y = std::min<u16>(max_y, regs.framebuffer.framebuffer.GetHeight() << 4);
    if (min_x >= max_x || min_y >= max_y) return;

    const int bias0 =
        IsRightSideOrFlatBottomEdge(vtxpos[0].xy(), vtxpos[1].xy(), vtxpos[2].xy()) ? -1 : 0;
    const int bias1 =
        IsRightSideOrFlatBottomEdge(vtxpos[1].xy(), vtxpos[2].xy(), vtxpos[0].xy()) ? -1 : 0;
    const int bias2 =
        IsRightSideOrFlatBottomEdge(vtxpos[2].xy(), vtxpos[0].xy(), vtxpos[1].xy()) ? -1 : 0;

    const auto w_inverse = Common::MakeVec(v0.pos.w, v1.pos.w, v2.pos.w);

    const auto textures = regs.texturing.GetTextures();
    const auto tev_stages = regs.texturing.GetTevStages();
    bool needs_footprint = false;
    for (u32 i = 0; i < 3; ++i) {
        const auto& t = textures[i];
        needs_footprint |= t.enabled && t.config.address != 0 &&
            (i != 0 || t.config.type == TexturingRegs::TextureConfig::Texture2D ||
             t.config.type == TexturingRegs::TextureConfig::TextureCube ||
             t.config.type == TexturingRegs::TextureConfig::Projection2D);
    }

    std::array<PreparedTexture, 3> prepared_textures;
    const bool parallel_safe = PrepareTextures(textures, prepared_textures);

    // CodexAstraLocal: Evaluate all four perspective-correct quad positions,
    // including uncovered helper lanes, before deriving a texture footprint.
    // This preserves the original covered-pixel interpolation arithmetic.
    const auto texture_coordinates = [&](u16 x, u16 y) {
        const auto barycentric = Common::MakeVec(
            f24::FromFloat32(static_cast<float>(bias0 + SignedArea(vtxpos[1].xy(), vtxpos[2].xy(), {x, y}))),
            f24::FromFloat32(static_cast<float>(bias1 + SignedArea(vtxpos[2].xy(), vtxpos[0].xy(), {x, y}))),
            f24::FromFloat32(static_cast<float>(bias2 + SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), {x, y}))));
        const f24 inverse = f24::One() / Common::Dot(w_inverse, barycentric);
        const auto attr = [&](f24 a, f24 b, f24 c) {
            return Common::Dot(Common::MakeVec(a, b, c), barycentric) * inverse;
        };
        TextureCoordinates result;
        result.uv[0] = {attr(v0.tc0.u(), v1.tc0.u(), v2.tc0.u()), attr(v0.tc0.v(), v1.tc0.v(), v2.tc0.v())};
        result.uv[1] = {attr(v0.tc1.u(), v1.tc1.u(), v2.tc1.u()), attr(v0.tc1.v(), v1.tc1.v(), v2.tc1.v())};
        result.uv[2] = {attr(v0.tc2.u(), v1.tc2.u(), v2.tc2.u()), attr(v0.tc2.v(), v1.tc2.v(), v2.tc2.v())};
        result.w = attr(v0.tc0_w, v1.tc0_w, v2.tc0_w);
        return result;
    };

    fb.Bind();

    // CodexAstraLocal: The owner and sleeping workers steal disjoint small row
    // groups. Preserve the inherited pixel-center traversal inside each group;
    // no per-scanline heap task or extra owner wait thread is needed.
    const u32 rows = max_y > min_y ? (max_y - min_y) / 0x10 : 0;
    const auto process_rows = [&](u32 first, u32 end) {
        for (u32 row = first; row < end; ++row) {
            const u16 y = static_cast<u16>(min_y + 8 + row * 0x10);
            // CodexAstraLocal: Adjacent covered pixels share immutable helper
            // coordinates; only texture byte reads remain ordered/live for feedback.
            std::array<TextureCoordinates, 4> quad{};
            u32 cached_quad = ~u32{0};
            for (u16 x = min_x + 8; x < max_x; x += 0x10) {
                // Do not process the pixel if it's inside the scissor box and the scissor mode is
                // set to Exclude.
                if (regs.rasterizer.scissor_test.mode == RasterizerRegs::ScissorMode::Exclude) {
                    if (x >= scissor_x1 && x < scissor_x2 && y >= scissor_y1 && y < scissor_y2) {
                        continue;
                    }
                }

                // Calculate the barycentric coordinates w0, w1 and w2
                const s32 w0 = bias0 + SignedArea(vtxpos[1].xy(), vtxpos[2].xy(), {x, y});
                const s32 w1 = bias1 + SignedArea(vtxpos[2].xy(), vtxpos[0].xy(), {x, y});
                const s32 w2 = bias2 + SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), {x, y});
                const s32 wsum = w0 + w1 + w2;

                // If current pixel is not covered by the current primitive
                if (w0 < 0 || w1 < 0 || w2 < 0) {
                    continue;
                }

                const auto baricentric_coordinates = Common::MakeVec(
                    f24::FromFloat32(static_cast<f32>(w0)), f24::FromFloat32(static_cast<f32>(w1)),
                    f24::FromFloat32(static_cast<f32>(w2)));
                const f24 interpolated_w_inverse =
                    f24::One() / Common::Dot(w_inverse, baricentric_coordinates);

                // interpolated_z = z / w
                const float interpolated_z_over_w =
                    (v0.screenpos[2].ToFloat32() * w0 + v1.screenpos[2].ToFloat32() * w1 +
                     v2.screenpos[2].ToFloat32() * w2) /
                    wsum;

                // Not fully accurate. About 3 bits in precision are missing.
                // Z-Buffer (z / w * scale + offset)
                const float depth_scale =
                    f24::FromRaw(regs.rasterizer.viewport_depth_range).ToFloat32();
                const float depth_offset =
                    f24::FromRaw(regs.rasterizer.viewport_depth_near_plane).ToFloat32();
                float depth = interpolated_z_over_w * depth_scale + depth_offset;

                // Potentially switch to W-Buffer
                if (regs.rasterizer.depthmap_enable ==
                    Pica::RasterizerRegs::DepthBuffering::WBuffering) {
                    // W-Buffer (z * scale + w * offset = (z / w * scale + offset) * w)
                    depth *= interpolated_w_inverse.ToFloat32() * wsum;
                }

                // Clamp the result
                depth = std::clamp(depth, 0.0f, 1.0f);

                /**
                 * Perspective correct attribute interpolation:
                 * Attribute values cannot be calculated by simple linear interpolation since
                 * they are not linear in screen space. For example, when interpolating a
                 * texture coordinate across two vertices, something simple like
                 *     u = (u0*w0 + u1*w1)/(w0+w1)
                 * will not work. However, the attribute value divided by the
                 * clipspace w-coordinate (u/w) and and the inverse w-coordinate (1/w) are linear
                 * in screenspace. Hence, we can linearly interpolate these two independently and
                 * calculate the interpolated attribute by dividing the results.
                 * I.e.
                 *     u_over_w   = ((u0/v0.pos.w)*w0 + (u1/v1.pos.w)*w1)/(w0+w1)
                 *     one_over_w = (( 1/v0.pos.w)*w0 + ( 1/v1.pos.w)*w1)/(w0+w1)
                 *     u = u_over_w / one_over_w
                 *
                 * The generalization to three vertices is straightforward in baricentric
                 *coordinates.
                 **/
                const auto get_interpolated_attribute = [&](f24 attr0, f24 attr1, f24 attr2) {
                    auto attr_over_w = Common::MakeVec(attr0, attr1, attr2);
                    f24 interpolated_attr_over_w =
                        Common::Dot(attr_over_w, baricentric_coordinates);
                    return interpolated_attr_over_w * interpolated_w_inverse;
                };

                const Common::Vec4<u8> primary_color{
                    static_cast<u8>(
                        round(get_interpolated_attribute(v0.color.r(), v1.color.r(), v2.color.r())
                                  .ToFloat32() *
                              255)),
                    static_cast<u8>(
                        round(get_interpolated_attribute(v0.color.g(), v1.color.g(), v2.color.g())
                                  .ToFloat32() *
                              255)),
                    static_cast<u8>(
                        round(get_interpolated_attribute(v0.color.b(), v1.color.b(), v2.color.b())
                                  .ToFloat32() *
                              255)),
                    static_cast<u8>(
                        round(get_interpolated_attribute(v0.color.a(), v1.color.a(), v2.color.a())
                                  .ToFloat32() *
                              255)),
                };

                std::array<Common::Vec2<f24>, 3> uv;
                uv[0].u() = get_interpolated_attribute(v0.tc0.u(), v1.tc0.u(), v2.tc0.u());
                uv[0].v() = get_interpolated_attribute(v0.tc0.v(), v1.tc0.v(), v2.tc0.v());
                uv[1].u() = get_interpolated_attribute(v0.tc1.u(), v1.tc1.u(), v2.tc1.u());
                uv[1].v() = get_interpolated_attribute(v0.tc1.v(), v1.tc1.v(), v2.tc1.v());
                uv[2].u() = get_interpolated_attribute(v0.tc2.u(), v1.tc2.u(), v2.tc2.u());
                uv[2].v() = get_interpolated_attribute(v0.tc2.v(), v1.tc2.v(), v2.tc2.v());

                // Sample bound texture units.
                const f24 tc0_w = get_interpolated_attribute(v0.tc0_w, v1.tc0_w, v2.tc0_w);
                const u16 qx = ((x >> 4) & ~1U) * 16 + 8;
                const u16 qy = ((y >> 4) & ~1U) * 16 + 8;
                // CodexAstraLocal: Untextured/shadow-only triangles need no LOD work.
                if (needs_footprint && cached_quad != qx) {
                    quad = {texture_coordinates(qx, qy), texture_coordinates(qx + 16, qy),
                            texture_coordinates(qx, qy + 16), texture_coordinates(qx + 16, qy + 16)};
                    cached_quad = qx;
                }
                const u32 lane = ((x >> 4) & 1U) | (((y >> 4) & 1U) << 1);
                const auto texture_color = TextureColor(uv, textures, tc0_w, prepared_textures, quad, lane);

                Common::Vec4<u8> primary_fragment_color = {0, 0, 0, 0};
                Common::Vec4<u8> secondary_fragment_color = {0, 0, 0, 0};

                if (!regs.lighting.disable) {
                    const auto normquat =
                        Common::Quaternion<f32>{
                            {get_interpolated_attribute(v0.quat.x, v1.quat.x, v2.quat.x)
                                 .ToFloat32(),
                             get_interpolated_attribute(v0.quat.y, v1.quat.y, v2.quat.y)
                                 .ToFloat32(),
                             get_interpolated_attribute(v0.quat.z, v1.quat.z, v2.quat.z)
                                 .ToFloat32()},
                            get_interpolated_attribute(v0.quat.w, v1.quat.w, v2.quat.w).ToFloat32(),
                        }
                            .Normalized();

                    const Common::Vec3f view{
                        get_interpolated_attribute(v0.view.x, v1.view.x, v2.view.x).ToFloat32(),
                        get_interpolated_attribute(v0.view.y, v1.view.y, v2.view.y).ToFloat32(),
                        get_interpolated_attribute(v0.view.z, v1.view.z, v2.view.z).ToFloat32(),
                    };
                    std::tie(primary_fragment_color, secondary_fragment_color) =
                        ComputeFragmentsColors(regs.lighting, pica.lighting, normquat, view,
                                               texture_color);
                }

                // Write the TEV stages.
                auto combiner_output =
                    WriteTevConfig(texture_color, tev_stages, primary_color, primary_fragment_color,
                                   secondary_fragment_color);

                const auto& output_merger = regs.framebuffer.output_merger;
                if (output_merger.fragment_operation_mode ==
                    FramebufferRegs::FragmentOperationMode::Shadow) {
                    const u32 depth_int = static_cast<u32>(depth * 0xFFFFFF);
                    // Use green color as the shadow intensity
                    const u8 stencil = combiner_output.y;
                    fb.DrawShadowMapPixel(x >> 4, y >> 4, depth_int, stencil);
                    // Skip the normal output merger pipeline if it is in shadow mode
                    continue;
                }

                // Does alpha testing happen before or after stencil?
                if (!DoAlphaTest(combiner_output.a())) {
                    continue;
                }
                WriteFog(depth, combiner_output);
                if (!DoDepthStencilTest(x, y, depth)) {
                    continue;
                }
                if (regs.framebuffer.framebuffer.allow_color_write != 0) {
                    // CodexAstraLocal: A depth-only draw has no color read or
                    // write; an absent unused color target is not an error.
                    const auto result = PixelColor(x, y, combiner_output);
                    fb.DrawPixel(x >> 4, y >> 4, result);
                }
            }
        }
    };
    // CodexAstraLocal: Framebuffer feedback and overlapping attachments require
    // exact ascending y/x serial evaluation. A safe triangle still joins every
    // participant before later guest draws, memory writes or presentation.
    if (parallel_safe) {
        sw_workers.Run(rows, 4, process_rows);
    } else {
        process_rows(0, rows);
    }
}

// CodexAstraLocal: Prove disjoint live reads/writes before releasing scanlines.
// Compare mapped pointer ranges as well as checked sizes, covering physical
// aliases. Unknown memory is a typed failure; valid feedback keeps serial order.
bool RasterizerSoftware::PrepareTextures(
    std::span<const TexturingRegs::FullTextureConfig, 3> textures,
    std::array<PreparedTexture, 3>& prepared) const {
    const auto& target = regs.framebuffer.framebuffer;
    const auto& merger = regs.framebuffer.output_merger;
    const bool shadow = merger.fragment_operation_mode == FramebufferRegs::FragmentOperationMode::Shadow;
    const bool uses_color = shadow || target.allow_color_write != 0;
    const bool uses_depth = !shadow && (merger.depth_test_enable ||
        (merger.stencil_test.enable && target.depth_format == FramebufferRegs::DepthFormat::D24S8) ||
        (target.allow_depth_stencil_write && merger.depth_write_enable));
    if ((uses_color && !shadow && static_cast<u32>(target.color_format.Value()) > 4) ||
        (uses_depth && target.depth_format != FramebufferRegs::DepthFormat::D16 &&
         target.depth_format != FramebufferRegs::DepthFormat::D24 &&
         target.depth_format != FramebufferRegs::DepthFormat::D24S8)) {
        throw VideoCore::ShaderRecoveryError{"CPU Software framebuffer format is invalid"};
    }
    const auto range = [&](PAddr address, std::size_t bytes) -> std::span<const u8> {
        const auto ref = memory.GetPhysicalRef(address);
        if (!ref || bytes > ref.GetSize()) {
            throw VideoCore::ShaderRecoveryError{"CPU Software guest framebuffer/texture range is unavailable"};
        }
        return {ref.GetPtr(), bytes};
    };
    const auto overlaps = [](std::span<const u8> a, std::span<const u8> b) {
        if (a.empty() || b.empty()) return false;
        const auto x = reinterpret_cast<std::uintptr_t>(a.data());
        const auto y = reinterpret_cast<std::uintptr_t>(b.data());
        return x <= y ? y - x < a.size() : x - y < b.size();
    };
    // CodexAstraLocal: The final partial tile still addresses a full Morton
    // tile. A non-tile-aligned pitch is valid only for serial traversal because
    // distinct scanlines can then alias; never assume width*height is sufficient.
    const std::size_t pixels = ((target.GetHeight() - 1) / 8) * 8 * target.GetWidth() +
                               ((target.GetWidth() + 7) / 8) * 64;
    const auto color = uses_color ? range(target.GetColorBufferPhysicalAddress(), pixels *
        (shadow ? 4 : FramebufferRegs::BytesPerColorPixel(target.color_format))) : std::span<const u8>{};
    const auto depth = uses_depth ? range(target.GetDepthBufferPhysicalAddress(),
        pixels * FramebufferRegs::BytesPerDepthPixel(target.depth_format)) : std::span<const u8>{};
    bool parallel_safe = target.GetWidth() % 8 == 0 && !overlaps(color, depth);
    for (u32 i = 0; i < 3; ++i) {
        const auto& texture = textures[i];
        auto& out = prepared[i];
        if (!texture.enabled || texture.config.address == 0 ||
            (i == 0 && texture.config.type == TexturingRegs::TextureConfig::Disabled)) continue;
        const auto type = i == 0 ? texture.config.type.Value() : TexturingRegs::TextureConfig::Texture2D;
        if (type > TexturingRegs::TextureConfig::Disabled) {
            throw VideoCore::ShaderRecoveryError{"CPU Software unsupported texture type"};
        }
        const bool cube = type == TexturingRegs::TextureConfig::TextureCube ||
                          type == TexturingRegs::TextureConfig::ShadowCube;
        const bool shadow_read = type == TexturingRegs::TextureConfig::Shadow2D ||
                                 type == TexturingRegs::TextureConfig::ShadowCube;
        if (cube && texture.config.width != texture.config.height)
            throw VideoCore::ShaderRecoveryError{"CPU Software cube faces must be square"};
        out.layout = MakeTextureLayout(texture.config, texture.format, shadow_read);
        out.face_count = cube ? 6 : 1;
        for (u32 face = 0; face < out.face_count; ++face) {
            out.addresses[face] = cube ? regs.texturing.GetCubePhysicalAddress(
                static_cast<TexturingRegs::CubeFace>(face)) : texture.config.GetPhysicalAddress();
            out.faces[face] = range(out.addresses[face], out.layout.bytes);
            parallel_safe &= !overlaps(out.faces[face], color) && !overlaps(out.faces[face], depth);
        }
    }
    return parallel_safe;
}

std::array<Common::Vec4<u8>, 4> RasterizerSoftware::TextureColor(
    std::span<const Common::Vec2<f24>, 3> uv,
    std::span<const TexturingRegs::FullTextureConfig, 3> textures, f24 tc0_w,
    const std::array<PreparedTexture, 3>& prepared,
    const std::array<TextureCoordinates, 4>& quad, u32 lane) const {
    std::array<Common::Vec4<u8>, 4> texture_color{};
    for (u32 i = 0; i < 3; ++i) {
        const auto& texture = textures[i];
        if (!texture.enabled) continue;
        if (texture.config.address == 0) {
            texture_color[i] = {0, 0, 0, 255};
            continue;
        }
        const auto type = i == 0 ? texture.config.type.Value() : TexturingRegs::TextureConfig::Texture2D;
        if (type == TexturingRegs::TextureConfig::Disabled) continue;
        const u32 coordinate = i == 2 && regs.texturing.main_config.texture2_use_coord1 ? 1 : i;
        f24 u = uv[coordinate].u(), v = uv[coordinate].v();
        float shadow_z = std::abs(tc0_w.ToFloat32());
        const bool cube = type == TexturingRegs::TextureConfig::TextureCube ||
                          type == TexturingRegs::TextureConfig::ShadowCube;
        const bool shadow = type == TexturingRegs::TextureConfig::Shadow2D ||
                            type == TexturingRegs::TextureConfig::ShadowCube;
        u32 face = 0;
        if (cube) {
            // CodexAstraLocal: Distinct cube faces may alias the same guest
            // address, so face identity comes from direction, never the pointer.
            const float a = std::abs(u.ToFloat32()), b = std::abs(v.ToFloat32());
            const float c = std::abs(tc0_w.ToFloat32());
            if (a > b && a > c) face = u > f24::Zero() ? 0 : 1;
            else if (b > c) face = v > f24::Zero() ? 2 : 3;
            else face = tc0_w > f24::Zero() ? 4 : 5;
            f24 z;
            PAddr address;
            std::tie(u, v, z, address) = ConvertCubeCoord(u, v, tc0_w, regs.texturing);
            shadow_z = z.ToFloat32();
        } else if (type == TexturingRegs::TextureConfig::Projection2D ||
                   (type == TexturingRegs::TextureConfig::Shadow2D && !regs.texturing.shadow.orthographic)) {
            u /= tc0_w;
            v /= tc0_w;
        }
        if (shadow) {
            if (!std::isfinite(shadow_z)) {
                throw VideoCore::ShaderRecoveryError{"CPU Software shadow depth is not finite"};
            }
            const s32 query = std::max(0, static_cast<s32>(std::min(shadow_z, 1.f) * 0xFFFFFF) -
                                           static_cast<s32>(regs.texturing.shadow.bias.Value() * 2));
            texture_color[i] = SampleShadow(prepared[i].faces[face], prepared[i].layout,
                                              u.ToFloat32(), v.ToFloat32(), query, cube);
            continue;
        }
        // CodexAstraLocal: Project all helper lanes onto the selected cube face
        // before differentiation; choosing a different face per lane creates a
        // discontinuous LOD. Projection textures divide each helper lane too.
        std::array<Common::Vec2f, 4> mapped;
        for (u32 q = 0; q < 4; ++q) {
            f24 a = quad[q].uv[coordinate].u(), b = quad[q].uv[coordinate].v(), w = quad[q].w;
            if (cube) {
                f24 x, y, z;
                switch (static_cast<TexturingRegs::CubeFace>(face)) {
                case TexturingRegs::CubeFace::PositiveX: x = -w; y = -b; z = a; break;
                case TexturingRegs::CubeFace::NegativeX: x = -w; y = b; z = a; break;
                case TexturingRegs::CubeFace::PositiveY: x = a; y = w; z = b; break;
                case TexturingRegs::CubeFace::NegativeY: x = -a; y = w; z = b; break;
                case TexturingRegs::CubeFace::PositiveZ: x = a; y = -b; z = w; break;
                case TexturingRegs::CubeFace::NegativeZ: x = a; y = b; z = w; break;
                }
                const auto half = f24::FromFloat32(.5f);
                a = x / z * half + half;
                b = y / z * half + half;
            } else if (type == TexturingRegs::TextureConfig::Projection2D) {
                a /= w; b /= w;
            }
            mapped[q] = {a.ToFloat32(), b.ToFloat32()};
        }
        const float lod = TextureLod(mapped, lane, texture.config.width, texture.config.height);
        if (cube) {
            texture_color[i] = SampleCubeTexture(prepared[i].faces, prepared[i].layout,
                texture.config, face, u.ToFloat32(), v.ToFloat32(), lod,
                Settings::values.texture_sampling.GetValue());
        } else {
            texture_color[i] = SampleTexture(prepared[i].faces[face], prepared[i].layout, texture.config,
                                               u.ToFloat32(), v.ToFloat32(), lod,
                                               Settings::values.texture_sampling.GetValue());
        }
    }
    if (regs.texturing.main_config.texture3_enable) {
        const auto& proctex_uv = uv[regs.texturing.main_config.texture3_coordinates];
        texture_color[3] = ProcTex(proctex_uv.u().ToFloat32(), proctex_uv.v().ToFloat32(),
                                   regs.texturing, pica.proctex);
    }
    return texture_color;
}

Common::Vec4<u8> RasterizerSoftware::PixelColor(u16 x, u16 y,
                                                Common::Vec4<u8> combiner_output) const {
    const auto dest = fb.GetPixel(x >> 4, y >> 4);
    Common::Vec4<u8> blend_output = combiner_output;

    const auto& output_merger = regs.framebuffer.output_merger;
    if (output_merger.alphablend_enable) {
        const auto params = output_merger.alpha_blending;
        const auto lookup_factor = [&](u32 channel, FramebufferRegs::BlendFactor factor) -> u8 {
            DEBUG_ASSERT(channel < 4);

            const Common::Vec4<u8> blend_const =
                Common::MakeVec(
                    output_merger.blend_const.r.Value(), output_merger.blend_const.g.Value(),
                    output_merger.blend_const.b.Value(), output_merger.blend_const.a.Value())
                    .Cast<u8>();

            switch (factor) {
            case FramebufferRegs::BlendFactor::Zero:
                return 0;
            case FramebufferRegs::BlendFactor::One:
                return 255;
            case FramebufferRegs::BlendFactor::SourceColor:
                return combiner_output[channel];
            case FramebufferRegs::BlendFactor::OneMinusSourceColor:
                return 255 - combiner_output[channel];
            case FramebufferRegs::BlendFactor::DestColor:
                return dest[channel];
            case FramebufferRegs::BlendFactor::OneMinusDestColor:
                return 255 - dest[channel];
            case FramebufferRegs::BlendFactor::SourceAlpha:
                return combiner_output.a();
            case FramebufferRegs::BlendFactor::OneMinusSourceAlpha:
                return 255 - combiner_output.a();
            case FramebufferRegs::BlendFactor::DestAlpha:
                return dest.a();
            case FramebufferRegs::BlendFactor::OneMinusDestAlpha:
                return 255 - dest.a();
            case FramebufferRegs::BlendFactor::ConstantColor:
                return blend_const[channel];
            case FramebufferRegs::BlendFactor::OneMinusConstantColor:
                return 255 - blend_const[channel];
            case FramebufferRegs::BlendFactor::ConstantAlpha:
                return blend_const.a();
            case FramebufferRegs::BlendFactor::OneMinusConstantAlpha:
                return 255 - blend_const.a();
            case FramebufferRegs::BlendFactor::SourceAlphaSaturate:
                // Returns 1.0 for the alpha channel
                if (channel == 3) {
                    return 255;
                }
                return std::min(combiner_output.a(), static_cast<u8>(255 - dest.a()));
            default:
                LOG_CRITICAL(HW_GPU, "Unknown blend factor {:x}", factor);
                UNIMPLEMENTED();
                break;
            }
            return combiner_output[channel];
        };

        const auto srcfactor = Common::MakeVec(
            lookup_factor(0, params.factor_source_rgb), lookup_factor(1, params.factor_source_rgb),
            lookup_factor(2, params.factor_source_rgb), lookup_factor(3, params.factor_source_a));

        const auto dstfactor = Common::MakeVec(
            lookup_factor(0, params.factor_dest_rgb), lookup_factor(1, params.factor_dest_rgb),
            lookup_factor(2, params.factor_dest_rgb), lookup_factor(3, params.factor_dest_a));

        blend_output = EvaluateBlendEquation(combiner_output, srcfactor, dest, dstfactor,
                                             params.blend_equation_rgb);
        blend_output.a() = EvaluateBlendEquation(combiner_output, srcfactor, dest, dstfactor,
                                                 params.blend_equation_a)
                               .a();
    } else {
        blend_output =
            Common::MakeVec(LogicOp(combiner_output.r(), dest.r(), output_merger.logic_op),
                            LogicOp(combiner_output.g(), dest.g(), output_merger.logic_op),
                            LogicOp(combiner_output.b(), dest.b(), output_merger.logic_op),
                            LogicOp(combiner_output.a(), dest.a(), output_merger.logic_op));
    }

    const Common::Vec4<u8> result = {
        output_merger.red_enable ? blend_output.r() : dest.r(),
        output_merger.green_enable ? blend_output.g() : dest.g(),
        output_merger.blue_enable ? blend_output.b() : dest.b(),
        output_merger.alpha_enable ? blend_output.a() : dest.a(),
    };

    return result;
}

Common::Vec4<u8> RasterizerSoftware::WriteTevConfig(
    std::span<const Common::Vec4<u8>, 4> texture_color,
    std::span<const Pica::TexturingRegs::TevStageConfig, 6> tev_stages,
    Common::Vec4<u8> primary_color, Common::Vec4<u8> primary_fragment_color,
    Common::Vec4<u8> secondary_fragment_color) {
    /**
     * Texture environment - consists of 6 stages of color and alpha combining.
     * Color combiners take three input color values from some source (e.g. interpolated
     * vertex color, texture color, previous stage, etc), perform some very simple
     * operations on each of them (e.g. inversion) and then calculate the output color
     * with some basic arithmetic. Alpha combiners can be configured separately but work
     * analogously.
     **/
    Common::Vec4<u8> combiner_output = {0, 0, 0, 0};
    Common::Vec4<u8> combiner_buffer = {0, 0, 0, 0};
    Common::Vec4<u8> next_combiner_buffer =
        Common::MakeVec(regs.texturing.tev_combiner_buffer_color.r.Value(),
                        regs.texturing.tev_combiner_buffer_color.g.Value(),
                        regs.texturing.tev_combiner_buffer_color.b.Value(),
                        regs.texturing.tev_combiner_buffer_color.a.Value())
            .Cast<u8>();

    for (u32 tev_stage_index = 0; tev_stage_index < tev_stages.size(); ++tev_stage_index) {
        const auto& tev_stage = tev_stages[tev_stage_index];
        using Source = TexturingRegs::TevStageConfig::Source;

        auto get_source = [&](Source source) -> Common::Vec4<u8> {
            switch (source) {
            case Source::PrimaryColor:
                return primary_color;
            case Source::PrimaryFragmentColor:
                return primary_fragment_color;
            case Source::SecondaryFragmentColor:
                return secondary_fragment_color;
            case Source::Texture0:
                return texture_color[0];
            case Source::Texture1:
                return texture_color[1];
            case Source::Texture2:
                return texture_color[2];
            case Source::Texture3:
                return texture_color[3];
            case Source::PreviousBuffer:
                return combiner_buffer;
            case Source::Constant:
                return Common::MakeVec(tev_stage.const_r.Value(), tev_stage.const_g.Value(),
                                       tev_stage.const_b.Value(), tev_stage.const_a.Value())
                    .Cast<u8>();
            case Source::Previous:
                return combiner_output;
            default:
                LOG_ERROR(HW_GPU, "Unknown color combiner source {}", (int)source);
                UNIMPLEMENTED();
                return {0, 0, 0, 0};
            }
        };

        /**
         * Color combiner
         * NOTE: Not sure if the alpha combiner might use the color output of the previous
         *       stage as input. Hence, we currently don't directly write the result to
         *       combiner_output.rgb(), but instead store it in a temporary variable until
         *       alpha combining has been done.
         **/
        const auto source1 = tev_stage_index == 0 && tev_stage.color_source1 == Source::Previous
                                 ? tev_stage.color_source3.Value()
                                 : tev_stage.color_source1.Value();
        const auto source2 = tev_stage_index == 0 && tev_stage.color_source2 == Source::Previous
                                 ? tev_stage.color_source3.Value()
                                 : tev_stage.color_source2.Value();
        const std::array<Common::Vec3<u8>, 3> color_result = {
            GetColorModifier(tev_stage.color_modifier1, get_source(source1)),
            GetColorModifier(tev_stage.color_modifier2, get_source(source2)),
            GetColorModifier(tev_stage.color_modifier3, get_source(tev_stage.color_source3)),
        };
        const Common::Vec3<u8> color_output = ColorCombine(tev_stage.color_op, color_result);

        u8 alpha_output;
        if (tev_stage.color_op == TexturingRegs::TevStageConfig::Operation::Dot3_RGBA) {
            // result of Dot3_RGBA operation is also placed to the alpha component
            alpha_output = color_output.x;
        } else {
            // alpha combiner
            const std::array<u8, 3> alpha_result = {{
                GetAlphaModifier(tev_stage.alpha_modifier1, get_source(tev_stage.alpha_source1)),
                GetAlphaModifier(tev_stage.alpha_modifier2, get_source(tev_stage.alpha_source2)),
                GetAlphaModifier(tev_stage.alpha_modifier3, get_source(tev_stage.alpha_source3)),
            }};
            alpha_output = AlphaCombine(tev_stage.alpha_op, alpha_result);
        }

        combiner_output[0] = std::min(255U, color_output.r() * tev_stage.GetColorMultiplier());
        combiner_output[1] = std::min(255U, color_output.g() * tev_stage.GetColorMultiplier());
        combiner_output[2] = std::min(255U, color_output.b() * tev_stage.GetColorMultiplier());
        combiner_output[3] = std::min(255U, alpha_output * tev_stage.GetAlphaMultiplier());

        combiner_buffer = next_combiner_buffer;

        if (regs.texturing.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferColor(
                tev_stage_index)) {
            next_combiner_buffer.r() = combiner_output.r();
            next_combiner_buffer.g() = combiner_output.g();
            next_combiner_buffer.b() = combiner_output.b();
        }

        if (regs.texturing.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferAlpha(
                tev_stage_index)) {
            next_combiner_buffer.a() = combiner_output.a();
        }
    }

    return combiner_output;
}

void RasterizerSoftware::WriteFog(float depth, Common::Vec4<u8>& combiner_output) const {
    /**
     * Apply fog combiner. Not fully accurate. We'd have to know what data type is used to
     * store the depth etc. Using float for now until we know more about Pica datatypes.
     **/
    if (regs.texturing.fog_mode == TexturingRegs::FogMode::Fog) {
        const Common::Vec3<u8> fog_color =
            Common::MakeVec(regs.texturing.fog_color.r.Value(), regs.texturing.fog_color.g.Value(),
                            regs.texturing.fog_color.b.Value())
                .Cast<u8>();

        float fog_index;
        if (regs.texturing.fog_flip) {
            fog_index = (1.0f - depth) * 128.0f;
        } else {
            fog_index = depth * 128.0f;
        }

        // Generate clamped fog factor from LUT for given fog index
        const f32 fog_i = std::clamp(floorf(fog_index), 0.0f, 127.0f);
        const f32 fog_f = fog_index - fog_i;
        const auto& fog_lut_entry = pica.fog.lut[static_cast<u32>(fog_i)];
        f32 fog_factor = fog_lut_entry.ToFloat() + fog_lut_entry.DiffToFloat() * fog_f;
        fog_factor = std::clamp(fog_factor, 0.0f, 1.0f);
        for (u32 i = 0; i < 3; i++) {
            combiner_output[i] = static_cast<u8>(fog_factor * combiner_output[i] +
                                                 (1.0f - fog_factor) * fog_color[i]);
        }
    }
}

bool RasterizerSoftware::DoAlphaTest(u8 alpha) const {
    const auto& output_merger = regs.framebuffer.output_merger;
    if (!output_merger.alpha_test.enable) {
        return true;
    }
    switch (output_merger.alpha_test.func) {
    case FramebufferRegs::CompareFunc::Never:
        return false;
    case FramebufferRegs::CompareFunc::Always:
        return true;
    case FramebufferRegs::CompareFunc::Equal:
        return alpha == output_merger.alpha_test.ref;
    case FramebufferRegs::CompareFunc::NotEqual:
        return alpha != output_merger.alpha_test.ref;
    case FramebufferRegs::CompareFunc::LessThan:
        return alpha < output_merger.alpha_test.ref;
    case FramebufferRegs::CompareFunc::LessThanOrEqual:
        return alpha <= output_merger.alpha_test.ref;
    case FramebufferRegs::CompareFunc::GreaterThan:
        return alpha > output_merger.alpha_test.ref;
    case FramebufferRegs::CompareFunc::GreaterThanOrEqual:
        return alpha >= output_merger.alpha_test.ref;
    default:
        LOG_CRITICAL(Render_Software, "Unknown alpha test condition {}",
                     output_merger.alpha_test.func.Value());
        return false;
    }
}

bool RasterizerSoftware::DoDepthStencilTest(u16 x, u16 y, float depth) const {
    const auto& framebuffer = regs.framebuffer.framebuffer;
    const auto stencil_test = regs.framebuffer.output_merger.stencil_test;
    // CodexAstraLocal: Ignore absent/undefined depth storage when no operation
    // consumes it, matching the attachment admission used for CPU scanlines.
    if (!regs.framebuffer.output_merger.depth_test_enable &&
        !(framebuffer.allow_depth_stencil_write && regs.framebuffer.output_merger.depth_write_enable) &&
        !(stencil_test.enable && framebuffer.depth_format == FramebufferRegs::DepthFormat::D24S8))
        return true;
    u8 old_stencil = 0;

    const auto update_stencil = [&](Pica::FramebufferRegs::StencilAction action) {
        const u8 new_stencil =
            PerformStencilAction(action, old_stencil, stencil_test.reference_value);
        if (framebuffer.allow_depth_stencil_write != 0) {
            const u8 stencil =
                (new_stencil & stencil_test.write_mask) | (old_stencil & ~stencil_test.write_mask);
            fb.SetStencil(x >> 4, y >> 4, stencil);
        }
    };

    const bool stencil_action_enable =
        regs.framebuffer.output_merger.stencil_test.enable &&
        regs.framebuffer.framebuffer.depth_format == FramebufferRegs::DepthFormat::D24S8;

    if (stencil_action_enable) {
        old_stencil = fb.GetStencil(x >> 4, y >> 4);
        const u8 dest = old_stencil & stencil_test.input_mask;
        const u8 ref = stencil_test.reference_value & stencil_test.input_mask;
        bool pass = false;
        switch (stencil_test.func) {
        case FramebufferRegs::CompareFunc::Never:
            pass = false;
            break;
        case FramebufferRegs::CompareFunc::Always:
            pass = true;
            break;
        case FramebufferRegs::CompareFunc::Equal:
            pass = (ref == dest);
            break;
        case FramebufferRegs::CompareFunc::NotEqual:
            pass = (ref != dest);
            break;
        case FramebufferRegs::CompareFunc::LessThan:
            pass = (ref < dest);
            break;
        case FramebufferRegs::CompareFunc::LessThanOrEqual:
            pass = (ref <= dest);
            break;
        case FramebufferRegs::CompareFunc::GreaterThan:
            pass = (ref > dest);
            break;
        case FramebufferRegs::CompareFunc::GreaterThanOrEqual:
            pass = (ref >= dest);
            break;
        }
        if (!pass) {
            update_stencil(stencil_test.action_stencil_fail);
            return false;
        }
    }

    const u32 num_bits = FramebufferRegs::DepthBitsPerPixel(framebuffer.depth_format);
    const u32 z = static_cast<u32>(depth * ((1 << num_bits) - 1));

    const auto& output_merger = regs.framebuffer.output_merger;
    if (output_merger.depth_test_enable) {
        const u32 ref_z = fb.GetDepth(x >> 4, y >> 4);
        bool pass = false;
        switch (output_merger.depth_test_func) {
        case FramebufferRegs::CompareFunc::Never:
            pass = false;
            break;
        case FramebufferRegs::CompareFunc::Always:
            pass = true;
            break;
        case FramebufferRegs::CompareFunc::Equal:
            pass = z == ref_z;
            break;
        case FramebufferRegs::CompareFunc::NotEqual:
            pass = z != ref_z;
            break;
        case FramebufferRegs::CompareFunc::LessThan:
            pass = z < ref_z;
            break;
        case FramebufferRegs::CompareFunc::LessThanOrEqual:
            pass = z <= ref_z;
            break;
        case FramebufferRegs::CompareFunc::GreaterThan:
            pass = z > ref_z;
            break;
        case FramebufferRegs::CompareFunc::GreaterThanOrEqual:
            pass = z >= ref_z;
            break;
        }
        if (!pass) {
            if (stencil_action_enable) {
                update_stencil(stencil_test.action_depth_fail);
            }
            return false;
        }
    }
    if (framebuffer.allow_depth_stencil_write != 0 && output_merger.depth_write_enable) {
        fb.SetDepth(x >> 4, y >> 4, z);
    }
    // The stencil depth_pass action is executed even if depth testing is disabled
    if (stencil_action_enable) {
        update_stencil(stencil_test.action_depth_pass);
    }

    return true;
}

} // namespace SwRenderer

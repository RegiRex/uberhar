// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/container/static_vector.hpp>
#include <bit> // CodexAstraLocal: Exceptional diagnostics preserve float operand bits.
#include <limits>
#include <chrono> // CodexAstraLocal TEST-ONLY LOG: Sparse preparation/raster cost samples.
#include <optional> // CodexAstraLocal: Feedback/untextured rows construct no texel memo.
#include "common/logging/log.h"
#include "common/scope_exit.h" // CodexAstraLocal: Sample all certificate exits without changing refusal paths.
#include "common/microprofile.h"
#include "common/quaternion.h"
#include "common/uberhar_software_controls.h"
#include "common/uberhar_activity.h" // CodexAstraLocal: Bind sparse reports to the existing session.
#include "common/settings.h" // CodexAstraLocal: Restart-only software participant budget.
#include "common/thread.h" // CodexAstraLocal: Identify sleeping raster workers in ordinary logs.
#include "common/vector_math.h"
#include "core/core.h" // CodexAstraLocal: Memo admission excludes concurrent guest writers.
#include "core/memory.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_software/sw_framebuffer.h"
#include "video_core/renderer_software/sw_lighting.h"
#include "video_core/renderer_software/sw_proctex.h"
#include "video_core/renderer_software/sw_rasterizer.h"
#include "video_core/renderer_software/sw_raster_work.h" // CodexAstraLocal: Exact empty-pixel pruning and bounded row claims.
#include "video_core/renderer_software/sw_texturing.h"
#include "video_core/renderer_software/sw_texture_demand.h"
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
    Vertex() = default; // CodexAstraLocal: Bounded owned fan preparation, assigned before use.
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

// CodexAstraLocal: Both ordinary pixels and certified constant groups use the
// same inherited scissor, fixed-point rounding, framebuffer bounds and biases.
std::optional<TriangleCoverage> PrepareCoverage(
    const std::array<Common::Vec3<Fix12P4>, 3>& vtxpos, const Pica::RegsInternal& regs) {
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
    if (min_x >= max_x || min_y >= max_y) return std::nullopt;

    const int bias0 =
        IsRightSideOrFlatBottomEdge(vtxpos[0].xy(), vtxpos[1].xy(), vtxpos[2].xy()) ? -1 : 0;
    const int bias1 =
        IsRightSideOrFlatBottomEdge(vtxpos[1].xy(), vtxpos[2].xy(), vtxpos[0].xy()) ? -1 : 0;
    const int bias2 =
        IsRightSideOrFlatBottomEdge(vtxpos[2].xy(), vtxpos[0].xy(), vtxpos[1].xy()) ? -1 : 0;

    return TriangleCoverage{min_x,min_y,max_x,max_y,scissor_x1,scissor_y1,scissor_x2,scissor_y2,
                            {bias0,bias1,bias2}};
}

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
      fb{memory, regs.framebuffer},
      feature_mask{Settings::software_renderer_features.GetValue()},
      diagnostics{Settings::software_renderer_diagnostics.GetValue()} {
    // CodexAstraLocal: Permanent effective configuration, emitted once per renderer.
    LOG_INFO(Render_Software, "CPU Software rendering participants={} (owner included), wait={}",
             sw_workers.Available(), sw_workers.WaitTransport());
    // CodexAstraLocal: Permanent once-per-renderer effective comparison policy.
    LOG_INFO(Render_Software, "Software feature policy schema=1 features={} diagnostics={} "
        "sw.tev_reuse={} sw.coverage_spans={} sw.row_dispatch={} sw.ordered_group={} "
        "live_guards=mandatory sample_stride=64 scope=renderer_lifetime",
        feature_mask, diagnostics, bool(feature_mask & 1), bool(feature_mask & 2),
        bool(feature_mask & 4), bool(feature_mask & 8));
}

// CodexAstraLocal TEST-ONLY LOG: Software's joined pool has no outstanding draw
// at teardown. Capture its owner-only counters once, including after a contained
// game failure; remove after adaptive Software participation is qualified.
RasterizerSoftware::~RasterizerSoftware() {
    EndCpuRasterBatch(true); // CodexAstraLocal: Defensive owner drain before pool destruction.
    const auto scheduling = sw_workers.Scheduling();
    LOG_INFO(Render_Software,
        "Uberhar software scheduling totals: schema=1 helpers={} samples={} rejected={} "
        "trials={} accepted={} reverted={} scope=sampled_pool_work_not_frames",
        scheduling.desired, scheduling.samples, scheduling.rejected,
        scheduling.trials, scheduling.accepted, scheduling.reverted);
    ReportWork(true);
}

// CodexAstraLocal TEST-ONLY LOG: Every 4096 submitted primitives at most, and
// no more often than five seconds. Counters are cumulative within this renderer;
// draw bounds and existing session identity permit matching ordinary frame logs.
// Remove after Software feature qualification; permanent errors remain separate.
void RasterizerSoftware::ReportWork(bool force) {
    if (!diagnostics || (!force && (input_draws & 4095))) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - last_report < std::chrono::seconds{5}) return;
    last_report = now;
    // CodexAstraLocal: Periodic feature progress must not stall rasterization on
    // a saturated log queue. Forced error/teardown summaries retain reliable delivery.
    const auto delivery = force ? Common::Log::Delivery::Reliable : Common::Log::Delivery::Diagnostic;
    const auto session = report_session;
    const auto scheduling = sw_workers.Scheduling();
    const auto emit = [&](const char* feature, bool enabled, u64 calls, u64 effective,
                          u64 refused, u64 work_in, u64 work_out, u64 ns) {
        LOG_INFO_WITH_DELIVERY(Render_Software, delivery, "Software feature cost schema=1 feature={} enabled={} "
            "diagnostics=true session={} mode=Software draw_first={} draw_last={} "
            "scope=cumulative_renderer samples={} calls={} effective={} refused={} "
            "work_in={} work_out={} sampled_ns={} errors={} errors_scope=renderer "
            "cost_scope=owner_feature_prep_not_savings",
            feature, enabled, session, report_draw, input_draws, work_samples, calls,
            effective, refused, work_in, work_out, ns, errors);
    };
    emit("sw.tev_reuse", feature_mask & 1, work_triangles, tev_cache.Hits(),
         tev_cache.Generation(), (feature_mask & 1) ? 33 * work_triangles : 0,
         (feature_mask & 1) ? 33 * tev_cache.Generation() : 0, tev_ns);
    emit("sw.coverage_spans", feature_mask & 2, work_triangles,
         retained_candidates, unsafe_rows, bounding_candidates, retained_candidates, span_ns);
    emit("sw.row_dispatch", feature_mask & 4, work_triangles, parallel_triangles,
         small_serial + ordered_serial, owner_rows + helper_rows, helper_rows, row_policy_ns);
    // CodexAstraLocal TEST-ONLY LOG: These exact work populations are not
    // measured savings. Raster wall includes their execution; no pixel clocks.
    // Remove after matched qualification of the new .51 raster changes.
    emit("sw.edge_steps", feature_mask & 2, work_triangles, stepped_rows,
         unsafe_rows, retained_candidates, stepped_candidates, span_ns);
    emit("sw.constant_tev", feature_mask & 1, work_triangles, constant_tev_triangles,
         work_triangles - constant_tev_triangles, retained_candidates, constant_tev_candidates, tev_ns);
    LOG_INFO_WITH_DELIVERY(Render_Software, delivery, "Software clipping cost schema=1 feature=sw.clip_copy_reuse "
        "enabled={} session={} planes={} copy_bypasses={} samples={} sampled_ns={} "
        "constant_state_refusals={} errors={} scope=cumulative_renderer_not_savings",
        bool(feature_mask & 2), session, clip_planes, clip_copy_bypasses, clip_samples,
        clip_ns, constant_state_refusals, errors);
    LOG_INFO_WITH_DELIVERY(Render_Software, delivery, "Software feature cost schema=1 feature=sw.constant_state_gate "
        "enabled={} session={} calls={} refused={} samples={} sampled_ns={} errors={} "
        "scope=cumulative_owner_gate_not_savings",
        bool(feature_mask & 8), session, constant_gate_calls, constant_state_refusals,
        constant_gate_samples, constant_gate_ns, errors);
    LOG_INFO_WITH_DELIVERY(Render_Software, delivery, "Software row detail schema=1 session={} draw_last={} "
        "parallel_admitted={} actual_helper_runs={} owner_small={} owner_ordered={} "
        "owner_rows={} helper_rows={} unsafe_span_rows={} history_hits={} rejected_cost={} "
        "samples={} preparation_ns={} raster_wall_ns={} parallel_owner_ns={} join_ns={} "
        "scope=sampled_complete_triangle_not_frame",
        session, input_draws, parallel_triangles, helper_runs, small_serial, ordered_serial,
        owner_rows, helper_rows, unsafe_rows, scheduling.history_hits, scheduling.rejected_cost,
        work_samples, preparation_ns, raster_ns, owner_ns, join_ns);
    // CodexAstraLocal TEST-ONLY LOG: Quantify this narrow grouping's real usage;
    // candidates are pre-Exclude coverage, not successful-pixel or savings claims.
    LOG_INFO_WITH_DELIVERY(Render_Software, delivery, "Software feature cost schema=1 feature=sw.ordered_group "
        "enabled={} diagnostics=true session={} mode=Software draw_last={} attempts={} "
        "fan_members={} groups={} grouped_triangles={} candidates={} owner_rows={} helper_rows={} "
        "refused_disabled={} refused_size={} refused_writers={} refused_fp={} refused_pixel_state={} "
        "refused_consumer={} refused_numeric={} refused_mapping={} refused_work={} "
        "samples={} preparation_ns={} raster_wall_ns={} owner_ns={} join_ns={} "
        "scope=certified_constant_owned_primitives",
        bool(feature_mask&8),session,input_draws,fan_attempts,fan_members,fan_groups,
        fan_grouped_triangles,fan_candidates,fan_owner_rows,fan_helper_rows,
        fan_refusals[0],fan_refusals[1],fan_refusals[2],fan_refusals[3],fan_refusals[4],
        fan_refusals[5],fan_refusals[6],fan_refusals[7],fan_refusals[8],fan_samples,
        fan_prepare_ns,fan_raster_ns,fan_owner_ns,fan_join_ns);
    LOG_INFO_WITH_DELIVERY(Render_Software, delivery, "Software constant scope schema=1 feature=sw.ordered_group "
        "session={} draw_last={} scopes={} refused={} input_alias_refused={} queued={} "
        "drains={} owner_drains={} pending={} admission_calls={} admission_samples={} admission_ns={} "
        "capture_calls={} capture_samples={} capture_ns={} errors={} errors_scope=renderer "
        "scope=supported_no_gs_loadvertices_only",
        session,input_draws,constant_scopes,constant_scope_refused,fan_refusals[9],
        constant_queued,constant_drains,constant_unwinds,pending_constants_count,
        constant_admission_calls,constant_admission_samples,constant_admission_ns,
        constant_capture_calls,constant_capture_samples,constant_capture_ns,errors);
    report_draw = input_draws + 1;
}

void RasterizerSoftware::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                                     const Pica::OutputVertex& v2) {
    // CodexAstraLocal TEST-ONLY LOG: No optional clocks or counters when disabled.
    if (diagnostics) {
        if (!input_draws) report_session = Common::UberharActivity::session.load(std::memory_order_relaxed);
        if(input_draws) ReportWork();
        ++input_draws;
    }
    /**
     * Clipping a planar n-gon against a plane will remove at least 1 vertex and introduces 2 at
     * the new edge (or less in degenerate cases). As such, we can say that each clipping plane
     * introduces at most 1 new vertex to the polygon. Since we start with a triangle and have a
     * seven fixed planes plus the optional custom plane require at most 3 + 8 = 11 vertices.
     **/
    // CodexAstraLocal: Include the W plane and optional custom plane in storage.
    static constexpr std::size_t MAX_VERTICES = 11;

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

    // CodexAstraLocal: Bound the clipped polygon plus closing vertex before
    // gathering owned fan work; no dynamically growing per-triangle storage.
    static_assert(MAX_VERTICES >= 3 + clipping_edges.size() + 1);

    // Simple implementation of the Sutherland-Hodgman clipping algorithm.
    // TODO: Make this less inefficient (currently lots of useless buffering overhead happens here)
    // CodexAstraLocal TEST-ONLY LOG: One in64 source primitives prices the
    // complete clipping scope; remove after copy-bypass qualification.
    {
        const bool measure_clip = diagnostics && (input_draws % 64 == 1);
        const auto clip_start = measure_clip ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
        SCOPE_EXIT({
            if (measure_clip) {
                ++clip_samples;
                clip_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - clip_start).count();
            }
        });
        const auto clip = [&](const ClippingEdge& edge) {
            // CodexAstraLocal: An all-inside plane copies the polygon unchanged.
            // Keep the original inside predicate (outside is its complement),
            // including NaN classification; skip writes only with no crossing.
            if (diagnostics) ++clip_planes;
            if ((feature_mask & 2) && std::all_of(output_list->begin(), output_list->end(),
                    [&](const Vertex& vertex) {
                        return edge.IsInside(vertex);
                    })) {
                if (diagnostics) ++clip_copy_bypasses;
                return;
            }
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
    }

    // CodexAstraLocal: Only already-owned fan primitives may share this route.
    // A certified PICA scope may retain owned coverage until its boundary;
    // otherwise the fan joins here. A refused certificate executes the old path.
    // CodexAstraLocal: Retained .50 fans all failed constant-group admission.
    // Reject the immutable pixel state before constructing ownership pointers
    // or querying concurrent writers/FP policy; eligible groups keep all guards.
    const bool constant_fan = (feature_mask & 8) && output_list->size() > 3;
    // CodexAstraLocal TEST-ONLY LOG: Price cheap early refusals independently
    // of the later full certificate; remove after grouping-cost qualification.
    const bool measure_gate = diagnostics && constant_fan && (constant_gate_calls++ % 64 == 0);
    const auto gate_start = measure_gate ? std::chrono::steady_clock::now()
                                        : std::chrono::steady_clock::time_point{};
    const bool constant_state = constant_fan && ConstantState();
    if (measure_gate) {
        ++constant_gate_samples;
        constant_gate_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - gate_start).count();
    }
    if (diagnostics && constant_fan && !constant_state) ++constant_state_refusals;
    if ((constant_batch || constant_state) && output_list->size() > 3) {
        std::array<const Pica::OutputVertex*, MAX_VERTICES> owned{};
        for (u32 i=0;i<output_list->size();++i) owned[i]=&(*output_list)[i];
        const auto& system = Core::System::GetInstance();
        if (constant_batch && QueueConstantFan({owned.data(),output_list->size()})) return;
        if (TryConstantFan({owned.data(),output_list->size()},
                           system.HasConcurrentGuestMemoryWriters(),
                           system.IsHostFpStatusIsolated())) return;
    }

    if (constant_batch && output_list->size()==3) {
        const std::array<const Pica::OutputVertex*,3> owned{
            &(*output_list)[0],&(*output_list)[1],&(*output_list)[2]};
        if (QueueConstantFan(owned)) return;
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

        try {
            ProcessTriangle(vtx0, vtx1, vtx2);
        } catch (const VideoCore::ShaderRecoveryError& error) {
            if (diagnostics) { ++errors; ReportWork(true); }
            // CodexAstraLocal TEST-ONLY LOG: ParallelWork has joined before
            // this owner catch. Remove after the first-fault numerical cases
            // are reconstructed and covered; never log from every worker/pixel.
            if (!sampling_failure_logged) {
                sampling_failure_logged = true;
                LOG_ERROR(Render_Software,
                    "Software first fault: {} triangle={} screen=({},{},{})/({},{},{})/({},{},{})",
                    error.what(), i, vtx0.screenpos.x.ToFloat32(), vtx0.screenpos.y.ToFloat32(),
                    vtx0.screenpos.z.ToFloat32(), vtx1.screenpos.x.ToFloat32(),
                    vtx1.screenpos.y.ToFloat32(), vtx1.screenpos.z.ToFloat32(),
                    vtx2.screenpos.x.ToFloat32(), vtx2.screenpos.y.ToFloat32(),
                    vtx2.screenpos.z.ToFloat32());
                // CodexAstraLocal TEST-ONLY LOG: The FEA fault retained only
                // final NaN UVs. Capture the original and prepared operands
                // once, after every worker joined, to distinguish shader input
                // from clipping/perspective arithmetic. Remove with a retained
                // reproduction of that remaining first-fault case.
                const std::array<const Pica::OutputVertex*, 3> source{&v0, &v1, &v2};
                const std::array<const Pica::OutputVertex*, 3> prepared{&vtx0, &vtx1, &vtx2};
                // CodexAstraLocal TEST-ONLY LOG: Match original source values
                // only after the terminal join; diagnostic allocation failure
                // must not replace the numerical error. Remove after the
                // demanded source/writer case has a retained reproduction.
                try {
                    LOG_ERROR(Render_Software, "Software terminal source: primitive={} fan={} {}",
                              input_draws, i, pica.DescribeSoftwareTerminalSources(source));
                } catch (...) {}
                const auto bits = [](f24 value) { return std::bit_cast<u32>(value.ToFloat32()); };
                for (u32 vertex = 0; vertex < source.size(); ++vertex) {
                    const auto& a = *source[vertex];
                    const auto& b = *prepared[vertex];
                    LOG_ERROR(Render_Software,
                        "Software first fault operands vertex={} source_pos={:08x},{:08x},{:08x},{:08x} "
                        "source_uv0={:08x},{:08x},{:08x} source_uv1={:08x},{:08x} "
                        "source_uv2={:08x},{:08x} prepared_invw={:08x} "
                        "prepared_uv0_over_w={:08x},{:08x},{:08x} "
                        "prepared_uv1_over_w={:08x},{:08x} prepared_uv2_over_w={:08x},{:08x}",
                        vertex, bits(a.pos.x), bits(a.pos.y), bits(a.pos.z), bits(a.pos.w),
                        bits(a.tc0.x), bits(a.tc0.y), bits(a.tc0_w),
                        bits(a.tc1.x), bits(a.tc1.y), bits(a.tc2.x), bits(a.tc2.y),
                        bits(b.pos.w), bits(b.tc0.x), bits(b.tc0.y), bits(b.tc0_w),
                        bits(b.tc1.x), bits(b.tc1.y), bits(b.tc2.x), bits(b.tc2.y));
                }
            }
            throw;
        }
    }
}

// CodexAstraLocal: This certificate admits only an exact constant overwrite.
// Live-input/error consumers cannot be reordered or removed by the grouped path.
bool RasterizerSoftware::ConstantState() const {
    const auto& target=regs.framebuffer.framebuffer;
    const auto& m=regs.framebuffer.output_merger;
    if (m.fragment_operation_mode!=FramebufferRegs::FragmentOperationMode::Default ||
        m.alpha_test.enable || m.depth_test_enable || m.stencil_test.enable ||
        m.depth_write_enable || m.alphablend_enable || m.logic_op!=FramebufferRegs::LogicOp::Copy ||
        !m.red_enable || !m.green_enable || !m.blue_enable || !m.alpha_enable ||
        !target.allow_color_write || regs.texturing.fog_mode!=TexturingRegs::FogMode::None) return false;
    // CodexAstraLocal: Even discarded depth arithmetic must not hide an
    // exceptional original conversion; admit bounded finite Z-buffer state.
    if(regs.rasterizer.depthmap_enable==RasterizerRegs::DepthBuffering::WBuffering) return false;
    for(const auto raw:{regs.rasterizer.viewport_depth_range.Value(),regs.rasterizer.viewport_depth_near_plane.Value()}) {
        const float value=f24::FromRaw(raw).ToFloat32();
        if(!std::isfinite(value) || std::abs(value)>1000000.0f) return false;
    }
    using Stage=TexturingRegs::TevStageConfig;
    const auto& final=regs.texturing.tev_stage5;
    return final.color_op==Stage::Operation::Replace && final.alpha_op==Stage::Operation::Replace &&
        final.color_source1==Stage::Source::Constant && final.alpha_source1==Stage::Source::Constant &&
        final.color_modifier1==Stage::ColorModifier::SourceColor &&
        final.alpha_modifier1==Stage::AlphaModifier::SourceAlpha;
}

// CodexAstraLocal: Mapping errors retain their original per-primitive typed
// failure path. No writes have occurred when this fresh target proof refuses.
bool RasterizerSoftware::ConstantTarget() const {
    std::array<PreparedTexture,3> textures;
    try { return PrepareTextures(regs.texturing.GetTextures(),textures,0); }
    catch (const VideoCore::ShaderRecoveryError&) { return false; }
}

// CodexAstraLocal: A whole fan is certified before queuing any of its commands.
// Bounded finite projection excludes old overflow/conversion ambiguities. Cull,
// integer rounding and scissor use the same helper as ordinary rasterization.
int RasterizerSoftware::PrepareConstantPrimitive(
    std::span<const Pica::OutputVertex* const,3> source, ConstantPrimitive& primitive) {
    // CodexAstraLocal TEST-ONLY LOG: Price immutable command preparation,
    // including refused numeric inputs; remove after grouped-path qualification.
    const bool measured=diagnostics && (constant_capture_calls++%64)==0;
    const auto started=measured ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    SCOPE_EXIT({ if(measured) { ++constant_capture_samples;constant_capture_ns+=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count(); } });
    for(u32 i=0;i<3;++i) {
        if(!source[i]) return -1;
        Vertex v{*source[i]};MakeScreenCoords(v);
        for(u32 axis=0;axis<2;++axis) {
            const float value=v.screenpos[axis].ToFloat32();
            if(!std::isfinite(value) || value<0 || value>1023) return -1;
        }
        const float inverse=v.pos.w.ToFloat32(),z=v.screenpos.z.ToFloat32();
        if(!std::isfinite(inverse) || inverse<0.000001f || inverse>1000000.0f ||
           !std::isfinite(z) || std::abs(z)>1000000.0f) return -1;
        primitive.points[i]={Fix12P4::FromFloat24(v.screenpos.x),
                             Fix12P4::FromFloat24(v.screenpos.y),Fix12P4{0}};
    }
    auto& p=primitive.points;
    auto area=SignedArea(p[0].xy(),p[1].xy(),p[2].xy());
    if(!area) return 0;
    const auto cull=regs.rasterizer.cull_mode.Value();
    if(((cull==RasterizerRegs::CullMode::KeepAll || cull==RasterizerRegs::CullMode::KeepAll2) && area<0) ||
       cull==RasterizerRegs::CullMode::KeepClockWise) { std::swap(p[1],p[2]);area=-area; }
    if(area<0) return 0;
    const auto bounds=PrepareCoverage(p,regs);
    if(!bounds) return 0;
    primitive.bounds=*bounds;
    return 1;
}

// CodexAstraLocal: Immutable constant primitives are the only queued work. All
// commands touching a destination row execute FIFO; distinct rows have proven
// disjoint Morton addresses. No allocation, live input or failing pixel consumer
// remains after certification, so unwind can finish earlier work on the owner.
bool RasterizerSoftware::RenderConstantGroup(std::span<const ConstantPrimitive> primitives,
    Common::Vec4<u8> color, bool owner_only, bool require_work) {
    if(primitives.empty()) return true;
    using Clock=std::chrono::steady_clock;
    const bool measure=diagnostics && fan_groups%64==0;
    const auto start=measure ? Clock::now() : Clock::time_point{};
    const u32 count=static_cast<u32>(primitives.size());
    u32 first_row=1024,last_row{},candidates{};
    for(u32 n=0;n<count;++n) {
        const auto& t=primitives[n];const auto& b=t.bounds;
        const RasterPoints points{{{t.points[0].x,t.points[0].y},
            {t.points[1].x,t.points[1].y},{t.points[2].x,t.points[2].y}}};
        const u32 rows=(b.max_y-b.min_y)/16,columns=(b.max_x-b.min_x)/16;
        for(u32 row=0;row<rows;++row) {
            auto& span=group_rows[n][row];
            span=CoveredRow(points,b.bias,b.min_x+8,b.min_y+8+row*16,columns);
            candidates+=span.end-span.first;
        }
        first_row=std::min(first_row,u32(b.min_y/16));last_row=std::max(last_row,u32(b.max_y/16));
    }
    const auto policy=PlanRasterRows(last_row-first_row,candidates);
    if(require_work && (count<2 || !policy.parallel)) return false;
    fb.Bind();
    // CodexAstraLocal: Every row owns disjoint Morton destinations; all owned
    // primitives touching that row execute FIFO. Exclude scissor and original
    // biased edge checks stay authoritative. No allocation or live input occurs.
    const auto process=[&](u32 first,u32 end) {
        for(u32 row=first_row+first;row<first_row+end;++row) {
            const auto y=static_cast<u16>(row*16+8);
            for(u32 n=0;n<count;++n) {
                const auto& t=primitives[n];const auto& b=t.bounds;
                if(y<b.min_y || y>=b.max_y) continue;
                const auto span=group_rows[n][row-b.min_y/16];
                for(u32 x=b.min_x+8+span.first*16;x<b.min_x+8+span.end*16;x+=16) {
                    if(regs.rasterizer.scissor_test.mode==RasterizerRegs::ScissorMode::Exclude &&
                       x>=b.scissor_x1 && x<b.scissor_x2 && y>=b.scissor_y1 && y<b.scissor_y2) continue;
                    const Common::Vec2<Fix12P4> p{Fix12P4{static_cast<u16>(x)},Fix12P4{y}};
                    if(SignedArea(t.points[1].xy(),t.points[2].xy(),p)+b.bias[0]<0 ||
                       SignedArea(t.points[2].xy(),t.points[0].xy(),p)+b.bias[1]<0 ||
                       SignedArea(t.points[0].xy(),t.points[1].xy(),p)+b.bias[2]<0) continue;
                    fb.DrawPixel(x/16,y/16,color);
                }
            }
        }
    };
    const auto prepared=measure ? Clock::now() : Clock::time_point{};
    Common::Uberhar::ParallelWork::Result work{};
    if(!owner_only && policy.parallel) {
        sw_workers.SetWorkIdentity(0x5357434f4e5354ULL);
        work=sw_workers.Run(last_row-first_row,policy.grain,process,measure);
    } else {
        process(0,last_row-first_row);work.owner_items=last_row-first_row;
    }
    if(diagnostics) {
        ++fan_groups;fan_grouped_triangles+=count;fan_candidates+=candidates;
        fan_helper_rows+=work.worker_items;fan_owner_rows+=work.owner_items;
        fan_owner_ns+=work.owner_process_ns;fan_join_ns+=work.join_ns;
        if(measure) {
            ++fan_samples;fan_prepare_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(prepared-start).count();
            fan_raster_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-prepared).count();
        }
    }
    return true;
}

// CodexAstraLocal: Fan-only grouping needs no future-input alias proof because
// it joins before AddTriangle returns. The same guarded kernel serves scopes.
bool RasterizerSoftware::TryConstantFan(std::span<const Pica::OutputVertex* const> fan,
                                        bool concurrent_writers,bool isolated_fp) {
    if(diagnostics) { ++fan_attempts;fan_members+=fan.size()>=2 ? fan.size()-2 : 0; }
    const auto refuse=[&](u32 reason) { if(diagnostics) ++fan_refusals[reason];return false; };
    if(!(feature_mask&8)) return refuse(0);
    if(fan.size()<4 || fan.size()>11) return refuse(1);
    if(!ConstantState()) return refuse(4);
    if(concurrent_writers) return refuse(2);
    if(!isolated_fp || !Common::Uberhar::ParallelFloatEnvironment::AllowsParallel()) return refuse(3);
    const PreparedTevConfig tev{regs.texturing,regs.lighting};
    if(tev.demand.textures || tev.demand.primary_color || tev.demand.lighting) return refuse(5);
    if(!ConstantTarget()) return refuse(7);
    std::array<ConstantPrimitive,9> primitives;u32 count{};
    for(u32 i=0;i+2<fan.size();++i) {
        const std::array<const Pica::OutputVertex*,3> source{fan[0],fan[i+1],fan[i+2]};
        const int result=PrepareConstantPrimitive(source,primitives[count]);
        if(result<0) return refuse(6);
        count+=result;
    }
    const std::array<Common::Vec4<u8>,4> none{};
    if(!RenderConstantGroup({primitives.data(),count},WriteTevConfig(none,tev,{},{},{}),false,true))
        return refuse(8);
    return true;
}

// CodexAstraLocal: Eligibility is cheap and side-effect free. PICA only prepares
// pinned future-input range certificates for the constant-copy state that can use them.
bool RasterizerSoftware::WantsCpuRasterBatch() const { return (feature_mask&8) && ConstantState(); }

bool RasterizerSoftware::BeginCpuRasterBatch(std::span<const CpuRasterReadRange> reads) {
    const auto& system=Core::System::GetInstance();
    return BeginConstantBatch(reads,system.HasConcurrentGuestMemoryWriters(),system.IsHostFpStatusIsolated());
}
// CodexAstraLocal: Explicit owner-time facts make the actual admission proof
// testable without constructing unrelated DSP/guest engines in host fixtures.
bool RasterizerSoftware::BeginConstantBatch(std::span<const CpuRasterReadRange> reads,
                                           bool concurrent_writers,bool isolated_fp) {
    // CodexAstraLocal TEST-ONLY LOG: Price the complete owner-side range/TEV
    // certificate separately from command capture and raster execution. Remove
    // after qualified use; disabled diagnostics perform no clocks/counters.
    const bool measured=diagnostics && (constant_admission_calls++%64)==0;
    const auto started=measured ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    SCOPE_EXIT({ if(measured) { ++constant_admission_samples;constant_admission_ns+=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count(); } });
    const auto refuse=[&](u32 reason) {
        if(diagnostics) { ++constant_scope_refused;++fan_refusals[reason]; }return false;
    };
    if(constant_batch) return refuse(1);
    if(!(feature_mask&8)) return refuse(0);
    if(!ConstantState()) return refuse(4);
    if(concurrent_writers) return refuse(2);
    if(!isolated_fp || !Common::Uberhar::ParallelFloatEnvironment::AllowsParallel()) return refuse(3);
    if(!ConstantTarget()) return refuse(7);
    const auto& target=regs.framebuffer.framebuffer;
    const u64 pixels=((target.GetHeight()-1)/8)*8*target.GetWidth()+((target.GetWidth()+7)/8)*64;
    const u64 bytes=pixels*FramebufferRegs::BytesPerColorPixel(target.color_format);
    const PAddr address=target.GetColorBufferPhysicalAddress();
    const auto ref=memory.GetPhysicalRef(address);
    // CodexAstraLocal: Both address identity and mapped-host aliasing matter.
    // Inputs remain pinned by PICA through End; no extra guest-memory read occurs.
    const auto overlap=[](u64 a,u64 n,u64 b,u64 m) { return n && m && (a<=b ? b-a<n : a-b<m); };
    for(const auto& input:reads) {
        if(!input.bytes.data() || overlap(address,bytes,input.address,input.bytes.size()) ||
           overlap(reinterpret_cast<std::uintptr_t>(ref.GetPtr()),bytes,
                   reinterpret_cast<std::uintptr_t>(input.bytes.data()),input.bytes.size())) {
            return refuse(9);
        }
    }
    const PreparedTevConfig tev{regs.texturing,regs.lighting};
    if(tev.demand.textures || tev.demand.primary_color || tev.demand.lighting) {
        return refuse(5);
    }
    const std::array<Common::Vec4<u8>,4> none{};
    constant_color=WriteTevConfig(none,tev,{},{},{});
    constant_batch=true;if(diagnostics) ++constant_scopes;
    return true;
}

// CodexAstraLocal: Detach the count before execution. A terminal publication
// error must never replay partial writes; the unwind path sees no old commands.
void RasterizerSoftware::DrainConstantGroup(bool owner_only) {
    const u32 count=std::exchange(pending_constants_count,0U);
    if(!count) return;
    if(diagnostics) { ++constant_drains;constant_unwinds+=owner_only; }
    (void)RenderConstantGroup({pending_constants.data(),count},constant_color,owner_only,false);
}
void RasterizerSoftware::EndCpuRasterBatch(bool unwinding) {
    constant_batch=false;
    DrainConstantGroup(unwinding);
}

// CodexAstraLocal: No timed fill or worker waits: only already-owned primitives
// are grouped, at capacity or the enclosing PICA scope end. Any certificate
// refusal first drains earlier output, then restores the full ordinary triangle.
bool RasterizerSoftware::QueueConstantFan(std::span<const Pica::OutputVertex* const> fan) {
    std::array<ConstantPrimitive,9> next;u32 count{};
    if(fan.size()<3 || fan.size()>11) { EndCpuRasterBatch(false);return false; }
    for(u32 i=0;i+2<fan.size();++i) {
        const std::array<const Pica::OutputVertex*,3> source{fan[0],fan[i+1],fan[i+2]};
        const int result=PrepareConstantPrimitive(source,next[count]);
        if(result<0) {
            if(diagnostics) ++fan_refusals[6];EndCpuRasterBatch(false);return false;
        }
        count+=result;
    }
    for(u32 i=0;i<count;++i) {
        pending_constants[pending_constants_count++]=next[i];
        if(diagnostics) ++constant_queued;
        if(pending_constants_count==pending_constants.size()) DrainConstantGroup(false);
    }
    return true;
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

    // CodexAstraLocal: A zero-area primitive covers no filled pixels. KeepAll
    // must not reverse it into a pass whose interpolation divides by zero.
    const s32 triangle_area = SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), vtxpos[2].xy());
    if (triangle_area == 0) {
        return;
    }

    if (regs.rasterizer.cull_mode == RasterizerRegs::CullMode::KeepAll ||
        regs.rasterizer.cull_mode == RasterizerRegs::CullMode::KeepAll2) {
        // Make sure we always end up with a triangle wound counter-clockwise
        if (!reversed && triangle_area < 0) {
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
        if (triangle_area < 0) {
            return;
        }
    }

    const auto bounds = PrepareCoverage(vtxpos, regs);
    if (!bounds) return;
    const auto [min_x,min_y,max_x,max_y,scissor_x1,scissor_y1,scissor_x2,scissor_y2,bias] = *bounds;
    const auto [bias0,bias1,bias2] = bias;

    // CodexAstraLocal: Coverage bounds share the same integer positions and
    // biases; they never replace interpolation or read/write guest memory.
    const RasterPoints coverage_points{{
        {vtxpos[0].x, vtxpos[0].y}, {vtxpos[1].x, vtxpos[1].y},
        {vtxpos[2].x, vtxpos[2].y}}};
    const std::array<s32, 3> coverage_bias{bias0, bias1, bias2};
    const auto w_inverse = Common::MakeVec(v0.pos.w, v1.pos.w, v2.pos.w);

    // CodexAstraLocal TEST-ONLY LOG: One in 64 surviving triangles prices
    // preparation separately from complete joined raster work. Remove these
    // clocks/counters after matched hardware qualification of this policy.
    using WorkClock = std::chrono::steady_clock;
    const bool measure_work = diagnostics && work_triangles % 64 == 0;
    const auto preparation_start = measure_work ? WorkClock::now() : WorkClock::time_point{};
    const auto textures = regs.texturing.GetTextures();
    // CodexAstraLocal: Reuse exact immutable state; every texture/attachment
    // mapping and live-input check below still belongs to this triangle.
    const auto tev_start = measure_work ? WorkClock::now() : WorkClock::time_point{};
    const auto& tev = tev_cache.Get(regs.texturing, regs.lighting, feature_mask & 1, diagnostics);
    // CodexAstraLocal: A consumer-free TEV plan is an integer draw constant.
    // Evaluate its existing six-stage implementation once; ordinary pixel
    // alpha/fog/depth/blend and all live-input guards still execute below.
    const bool constant_tev = (feature_mask & 1) && tev.constant_result;
    std::optional<Common::Vec4<u8>> constant_tev_color;
    if (constant_tev) {
        const std::array<Common::Vec4<u8>, 4> none{};
        constant_tev_color = WriteTevConfig(none, tev, {}, {}, {});
    }
    if (measure_work) tev_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        WorkClock::now() - tev_start).count();
    // CodexAstraLocal: Use the same immutable demand for range mapping, covered
    // interpolation, helper interpolation and sampling. A fixed LOD cannot
    // consume quad derivatives, while another unit may still need its own.
    const u32 demand_mask = tev.demand.textures;
    u32 coordinate_mask = 0, footprint_mask = 0, helper_coordinate_mask = 0;
    bool needs_w = false, helper_needs_w = false;
    for (u32 i = 0; i < 3; ++i) {
        const auto& t = textures[i];
        const auto type = i == 0 ? t.config.type.Value() : TexturingRegs::TextureConfig::Texture2D;
        if (!(demand_mask & (1U << i)) || !t.enabled || t.config.address == 0 ||
            type == TexturingRegs::TextureConfig::Disabled) continue;
        const u32 coordinate = i == 2 && regs.texturing.main_config.texture2_use_coord1 ? 1 : i;
        coordinate_mask |= 1U << coordinate;
        const bool shadow = type == TexturingRegs::TextureConfig::Shadow2D ||
                            type == TexturingRegs::TextureConfig::ShadowCube;
        const bool use_w = type != TexturingRegs::TextureConfig::Texture2D;
        needs_w |= use_w;
        if (!shadow && t.config.lod.min_level != t.config.lod.max_level) {
            footprint_mask |= 1U << i;
            helper_coordinate_mask |= 1U << coordinate;
            helper_needs_w |= use_w;
        }
    }
    if ((demand_mask & 8U) && regs.texturing.main_config.texture3_enable) {
        const u32 coord = regs.texturing.main_config.texture3_coordinates;
        if (coord >= 3) {
            throw VideoCore::ShaderRecoveryError{"CPU Software procedural coordinate selector is invalid"};
        }
        coordinate_mask |= 1U << coord;
    }

    std::array<PreparedTexture, 3> prepared_textures;
    const bool parallel_safe = PrepareTextures(textures, prepared_textures, demand_mask);
    // CodexAstraLocal: Do not initialize memo entries for untextured work or
    // cheap nearest uncompressed sampling; only repeated taps/ETC can use them.
    const auto sampling = Settings::values.texture_sampling.GetValue();
    bool reusable_texels = false;
    for (u32 i = 0; i < textures.size(); ++i) {
        if (prepared_textures[i].layout.bytes == 0) continue;
        const auto& texture = textures[i];
        const auto type = i == 0 ? texture.config.type.Value() : TexturingRegs::TextureConfig::Texture2D;
        reusable_texels |= type == TexturingRegs::TextureConfig::Shadow2D ||
            type == TexturingRegs::TextureConfig::ShadowCube ||
            texture.format == TexturingRegs::TextureFormat::ETC1 ||
            texture.format == TexturingRegs::TextureFormat::ETC1A4 ||
            sampling == Settings::TextureSampling::Linear ||
            (sampling == Settings::TextureSampling::GameControlled &&
             (texture.config.min_filter == TexturingRegs::TextureConfig::Linear ||
              texture.config.mag_filter == TexturingRegs::TextureConfig::Linear));
    }
    // CodexAstraLocal: Nonaliasing alone does not exclude DSP/RPC/async guest
    // writes. The owner admits reuse only while that existing execution guard
    // is clear; no new guest job starts before the complete triangle join.
    const bool memo_safe = reusable_texels && CanMemoizeTexels(parallel_safe,
        Core::System::GetInstance().HasConcurrentGuestMemoryWriters());

    // CodexAstraLocal: Quad helpers use the same unbiased interpolation plane
    // as covered pixels. Top-left coverage offsets must never alter attributes
    // or the perspective denominator, including outside-triangle helper lanes.
    const auto texture_coordinates = [&](u16 x, u16 y) {
        const auto barycentric = Common::MakeVec(
            f24::FromFloat32(static_cast<float>(SignedArea(vtxpos[1].xy(), vtxpos[2].xy(), {x, y}))),
            f24::FromFloat32(static_cast<float>(SignedArea(vtxpos[2].xy(), vtxpos[0].xy(), {x, y}))),
            f24::FromFloat32(static_cast<float>(SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), {x, y}))));
        const f24 inverse = f24::One() / Common::Dot(w_inverse, barycentric);
        const auto attr = [&](f24 a, f24 b, f24 c) {
            return Common::Dot(Common::MakeVec(a, b, c), barycentric) * inverse;
        };
        TextureCoordinates result{};
        if (helper_coordinate_mask & 1U)
            result.uv[0] = {attr(v0.tc0.u(), v1.tc0.u(), v2.tc0.u()), attr(v0.tc0.v(), v1.tc0.v(), v2.tc0.v())};
        if (helper_coordinate_mask & 2U)
            result.uv[1] = {attr(v0.tc1.u(), v1.tc1.u(), v2.tc1.u()), attr(v0.tc1.v(), v1.tc1.v(), v2.tc1.v())};
        if (helper_coordinate_mask & 4U)
            result.uv[2] = {attr(v0.tc2.u(), v1.tc2.u(), v2.tc2.u()), attr(v0.tc2.v(), v1.tc2.v(), v2.tc2.v())};
        if (helper_needs_w) result.w = attr(v0.tc0_w, v1.tc0_w, v2.tc0_w);
        return result;
    };

    fb.Bind();

    // CodexAstraLocal: Registers cannot advance before the joined triangle
    // returns. Decode depth constants once; keep each pixel's arithmetic order.
    const float depth_scale = f24::FromRaw(regs.rasterizer.viewport_depth_range).ToFloat32();
    const float depth_offset = f24::FromRaw(regs.rasterizer.viewport_depth_near_plane).ToFloat32();
    const bool w_buffer = regs.rasterizer.depthmap_enable == RasterizerRegs::DepthBuffering::WBuffering;
    // CodexAstraLocal: A complete Copy consumes no destination color. Decide
    // once, then preserve the same alpha/fog/depth/stencil and final encoding.
    const auto& merger = regs.framebuffer.output_merger;
    const bool overwrite_color = !merger.alphablend_enable &&
        merger.logic_op == FramebufferRegs::LogicOp::Copy && merger.red_enable &&
        merger.green_enable && merger.blue_enable && merger.alpha_enable;

    // CodexAstraLocal: The owner and sleeping workers steal disjoint small row
    // groups. Preserve the inherited pixel-center traversal inside each group;
    // no per-scanline heap task or extra owner wait thread is needed.
    const u32 rows = max_y > min_y ? (max_y - min_y) / 0x10 : 0;
    const u32 columns = (max_x - min_x) / 0x10;
    // CodexAstraLocal: Prepare each span once on the owner, then reuse its
    // exact candidate count for dispatch and the joined callbacks. No guest read
    // or write is moved across a primitive boundary by this bounded preparation.
    const auto span_start = measure_work ? WorkClock::now() : WorkClock::time_point{};
    u32 candidates{};
    for (u32 row = 0; row < rows; ++row) {
        coverage_rows[row] = (feature_mask & 2) ? CoveredRow(coverage_points, coverage_bias,
            min_x + 8, min_y + 8 + row * 16, columns, diagnostics ? &unsafe_rows : nullptr)
            : RasterSpan{0, columns};
        candidates += coverage_rows[row].end - coverage_rows[row].first;
        if (diagnostics && coverage_rows[row].exact_edges &&
            coverage_rows[row].end - coverage_rows[row].first >= 4) {
            ++stepped_rows;
            stepped_candidates += coverage_rows[row].end - coverage_rows[row].first;
        }
    }
    if (diagnostics) {
        bounding_candidates += u64(rows) * columns;
        retained_candidates += candidates;
        constant_tev_triangles += constant_tev;
        if (constant_tev) constant_tev_candidates += candidates;
    }
    if (measure_work) span_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        WorkClock::now() - span_start).count();
    const auto policy_start = measure_work ? WorkClock::now() : WorkClock::time_point{};
    const auto row_work = (feature_mask & 4) ? PlanRasterRows(rows, candidates)
                                           : RasterRowWork{4, true};
    if (measure_work) row_policy_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        WorkClock::now() - policy_start).count();
    const auto process_rows = [&](u32 first, u32 end) {
        // CodexAstraLocal: This callback owns requested decoded texels only for
        // its row group. Feedback and concurrent-writer routes pass no memo.
        std::optional<TexelMemo> memo;
        if (memo_safe) memo.emplace();
        auto* const texel_memo = memo ? &*memo : nullptr;
        for (u32 row = first; row < end; ++row) {
            const u16 y = static_cast<u16>(min_y + 8 + row * 0x10);
            // CodexAstraLocal: Adjacent covered pixels share immutable helper
            // coordinates; only texture byte reads remain ordered/live for feedback.
            std::array<TextureCoordinates, 4> quad{};
            u32 cached_quad = ~u32{0};
            // CodexAstraLocal: Skip only proven uncovered centers. The retained
            // original edge check below remains authoritative for every visit.
            const auto span = coverage_rows[row];
            if (span.first == span.end) continue;
            const u32 end_x = min_x + 8 + span.end * 16;
            const u32 first_x = min_x + 8 + span.first * 16;
            // CodexAstraLocal: Very short rows cannot amortize preparation.
            // Disabled/unsafe rows do no additional edge setup or increments.
            const bool step_edges = span.exact_edges && span.end - span.first >= 4;
            RasterEdgeSteps edge_steps;
            if (step_edges) edge_steps = RasterEdgeSteps{coverage_points, first_x, y};
            const auto advance_edges = [&] { if (step_edges) edge_steps.Advance(); };
            for (u32 wide_x = first_x; wide_x < end_x; wide_x += 16, advance_edges()) {
                const auto x = static_cast<u16>(wide_x);
                // Do not process the pixel if it's inside the scissor box and the scissor mode is
                // set to Exclude.
                if (regs.rasterizer.scissor_test.mode == RasterizerRegs::ScissorMode::Exclude) {
                    if (x >= scissor_x1 && x < scissor_x2 && y >= scissor_y1 && y < scissor_y2) {
                        continue;
                    }
                }

                // CodexAstraLocal: Coverage alone uses top-left edge biases.
                // Interpolation and Z/W depth use the true weights and area;
                // biasing an area-1 triangle can otherwise erase its sum.
                // CodexAstraLocal: Safe integer spans share exact edge
                // increments; interpolation keeps its original f24 operations.
                // Overflow-refused and disabled spans retain original products.
                const s32 w0 = step_edges ? static_cast<s32>(edge_steps.value[0])
                    : SignedArea(vtxpos[1].xy(), vtxpos[2].xy(), {x, y});
                const s32 w1 = step_edges ? static_cast<s32>(edge_steps.value[1])
                    : SignedArea(vtxpos[2].xy(), vtxpos[0].xy(), {x, y});
                const s32 w2 = step_edges ? static_cast<s32>(edge_steps.value[2])
                    : SignedArea(vtxpos[0].xy(), vtxpos[1].xy(), {x, y});
                const s32 wsum = w0 + w1 + w2;

                // If current pixel is not covered by the current primitive
                if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) {
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
                float depth = interpolated_z_over_w * depth_scale + depth_offset;

                // Potentially switch to W-Buffer
                if (w_buffer) {
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

                // CodexAstraLocal: A value absent from every executed TEV
                // operand needs no interpolation/conversion. Demanded values
                // retain the exact dot, reciprocal and rounding operations.
                Common::Vec4<u8> primary_color{};
                if (tev.demand.primary_color) {
                    primary_color = {
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
                }

                std::array<Common::Vec2<f24>, 3> uv{};
                if (coordinate_mask & 1U) {
                    uv[0].u() = get_interpolated_attribute(v0.tc0.u(), v1.tc0.u(), v2.tc0.u());
                    uv[0].v() = get_interpolated_attribute(v0.tc0.v(), v1.tc0.v(), v2.tc0.v());
                }
                if (coordinate_mask & 2U) {
                    uv[1].u() = get_interpolated_attribute(v0.tc1.u(), v1.tc1.u(), v2.tc1.u());
                    uv[1].v() = get_interpolated_attribute(v0.tc1.v(), v1.tc1.v(), v2.tc1.v());
                }
                if (coordinate_mask & 4U) {
                    uv[2].u() = get_interpolated_attribute(v0.tc2.u(), v1.tc2.u(), v2.tc2.u());
                    uv[2].v() = get_interpolated_attribute(v0.tc2.v(), v1.tc2.v(), v2.tc2.v());
                }

                // Sample bound texture units.
                const f24 tc0_w = needs_w ? get_interpolated_attribute(v0.tc0_w, v1.tc0_w, v2.tc0_w) : f24::Zero();
                const u16 qx = ((x >> 4) & ~1U) * 16 + 8;
                const u16 qy = ((y >> 4) & ~1U) * 16 + 8;
                // CodexAstraLocal: Untextured/shadow-only triangles need no LOD work.
                if (footprint_mask && cached_quad != qx) {
                    quad = {texture_coordinates(qx, qy), texture_coordinates(qx + 16, qy),
                            texture_coordinates(qx, qy + 16), texture_coordinates(qx + 16, qy + 16)};
                    cached_quad = qx;
                }
                const u32 lane = ((x >> 4) & 1U) | (((y >> 4) & 1U) << 1);
                // CodexAstraLocal: No demanded texture means no sampler call;
                // the same zero inputs still feed any live nontexture TEV.
                const auto texture_color = demand_mask
                    ? TextureColor(uv, textures, tc0_w, prepared_textures,
                        quad, lane, demand_mask, footprint_mask, x >> 4, y >> 4, texel_memo)
                    : std::array<Common::Vec4<u8>, 4>{};

                Common::Vec4<u8> primary_fragment_color = {0, 0, 0, 0};
                Common::Vec4<u8> secondary_fragment_color = {0, 0, 0, 0};

                if (tev.demand.lighting) {
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
                auto combiner_output = constant_tev_color ? *constant_tev_color :
                    WriteTevConfig(texture_color, tev, primary_color, primary_fragment_color,
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
                    const auto result = overwrite_color ? combiner_output
                                                        : PixelColor(x, y, combiner_output);
                    fb.DrawPixel(x >> 4, y >> 4, result);
                }
            }
        }
    };
    // CodexAstraLocal: Framebuffer feedback and overlapping attachments require
    // exact ascending y/x serial evaluation. A safe triangle still joins every
    // participant before later guest draws, memory writes or presentation.
    // CodexAstraLocal: Reuse .49's histories with a stable explicit work class:
    // demanded consumers, footprint, width, lighting count and texture formats.
    // Constants/data still pass its comparable-cost guard; no per-triangle tag.
    // Tiny or narrow work avoids pool publication entirely. Feedback is serial.
    const auto raster_start = measure_work ? WorkClock::now() : WorkClock::time_point{};
    if (measure_work) {
        ++work_samples;
        preparation_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            raster_start - preparation_start).count();
    }
    if (diagnostics) ++work_triangles;
    if (parallel_safe && row_work.parallel && sw_workers.Available() > 1) {
        if (diagnostics) ++parallel_triangles;
        const u64 work_identity = 1ULL | (u64(demand_mask) << 1) |
            (u64(tev.demand.lighting) << 5) | (u64(tev.demand.primary_color) << 6) |
            (u64(footprint_mask) << 7) | (u64(memo_safe) << 10) | (u64(columns) << 11) |
            (u64(regs.lighting.max_light_index.Value()) << 23) |
            (u64(textures[0].format) << 26) | (u64(textures[1].format) << 30) |
            (u64(textures[2].format) << 34);
        sw_workers.SetWorkIdentity(work_identity);
        const auto work = sw_workers.Run(rows, row_work.grain, process_rows, measure_work);
        if (diagnostics) {
            owner_ns += work.owner_process_ns; join_ns += work.join_ns;
            owner_rows += work.owner_items; helper_rows += work.worker_items;
            helper_runs += work.worker_items != 0;
        }
    } else {
        // CodexAstraLocal TEST-ONLY LOG: Separate dependency-forced serial work
        // from batches too small to dispatch; remove after row-policy qualification.
        if (diagnostics) {
            ordered_serial += !parallel_safe; small_serial += parallel_safe; owner_rows += rows;
        }
        process_rows(0, rows);
    }
    if (measure_work) raster_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        WorkClock::now() - raster_start).count();
}

// CodexAstraLocal: Prove disjoint live reads/writes before releasing scanlines.
// Compare mapped pointer ranges as well as checked sizes, covering physical
// aliases. Unknown memory is a typed failure; valid feedback keeps serial order.
bool RasterizerSoftware::PrepareTextures(
    std::span<const TexturingRegs::FullTextureConfig, 3> textures,
    std::array<PreparedTexture, 3>& prepared, u32 demand_mask) const {
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
        if (!(demand_mask & (1U << i)) || !texture.enabled || texture.config.address == 0 ||
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
            // CodexAstraLocal TEST-ONLY LOG payload: distinguish mapping from
            // numeric faults without successful-sample logging. Remove extra
            // context when these failure classes have concrete regressions.
            const auto ref = memory.GetPhysicalRef(out.addresses[face]);
            if (!ref || out.layout.bytes > ref.GetSize()) {
                const auto message = fmt::format(
                    "CPU Software texture mapping unit={} type={} face={} demand={:#x} address={:#x} "
                    "size={}x{} format={} lod={}:{} mapped={} required={}",
                    i, static_cast<u32>(type), face, demand_mask, out.addresses[face],
                    texture.config.width.Value(), texture.config.height.Value(),
                    static_cast<u32>(texture.format), texture.config.lod.min_level.Value(),
                    texture.config.lod.max_level.Value(), ref ? ref.GetSize() : 0, out.layout.bytes);
                throw VideoCore::ShaderRecoveryError{message.c_str()};
            }
            out.faces[face] = {ref.GetPtr(), out.layout.bytes};
            parallel_safe &= !overlaps(out.faces[face], color) && !overlaps(out.faces[face], depth);
        }
    }
    return parallel_safe;
}

std::array<Common::Vec4<u8>, 4> RasterizerSoftware::TextureColor(
    std::span<const Common::Vec2<f24>, 3> uv,
    std::span<const TexturingRegs::FullTextureConfig, 3> textures, f24 tc0_w,
    const std::array<PreparedTexture, 3>& prepared,
    const std::array<TextureCoordinates, 4>& quad, u32 lane, u32 demand_mask,
    u32 footprint_mask, u16 pixel_x, u16 pixel_y, TexelMemo* memo) const {
    std::array<Common::Vec4<u8>, 4> texture_color{};
    for (u32 i = 0; i < 3; ++i) {
        const auto& texture = textures[i];
        if (!(demand_mask & (1U << i)) || !texture.enabled) continue;
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
        float lod = std::numeric_limits<float>::quiet_NaN();
        try {
            if (cube) {
                // CodexAstraLocal: Distinct cube faces may alias the same guest
                // address, so face identity comes from direction, never the pointer.
                const float a = std::abs(u.ToFloat32()), b = std::abs(v.ToFloat32());
                const float c = std::abs(tc0_w.ToFloat32());
                // CodexAstraLocal: Reject consumed singular directions before
                // projection; finite large shadow UVs are handled by PCF borders.
                if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) ||
                    std::max({a, b, c}) == 0.f) {
                    throw VideoCore::ShaderRecoveryError{"CPU Software cube direction is singular/nonfinite"};
                }
                if (a > b && a > c) face = u > f24::Zero() ? 0 : 1;
                else if (b > c) face = v > f24::Zero() ? 2 : 3;
                else face = tc0_w > f24::Zero() ? 4 : 5;
                f24 z;
                PAddr address;
                std::tie(u, v, z, address) = ConvertCubeCoord(u, v, tc0_w, regs.texturing);
                shadow_z = z.ToFloat32();
            } else if (type == TexturingRegs::TextureConfig::Projection2D ||
                       (type == TexturingRegs::TextureConfig::Shadow2D && !regs.texturing.shadow.orthographic)) {
                if (!std::isfinite(tc0_w.ToFloat32()) || tc0_w == f24::Zero()) {
                    throw VideoCore::ShaderRecoveryError{"CPU Software projection denominator is zero/nonfinite"};
                }
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
                                                  u.ToFloat32(), v.ToFloat32(), query, cube, memo);
                continue;
            }
            // CodexAstraLocal: Project all helper lanes onto the selected cube face
            // before differentiation; choosing a different face per lane creates a
            // discontinuous LOD. Projection textures divide each helper lane too.
            if (footprint_mask & (1U << i)) {
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
                lod = TextureLod(mapped, lane, texture.config.width, texture.config.height);
            }
            if (cube) {
                texture_color[i] = SampleCubeTexture(prepared[i].faces, prepared[i].layout,
                    texture.config, face, u.ToFloat32(), v.ToFloat32(), lod,
                    Settings::values.texture_sampling.GetValue(), memo);
            } else {
                texture_color[i] = SampleTexture(prepared[i].faces[face], prepared[i].layout, texture.config,
                                                   u.ToFloat32(), v.ToFloat32(), lod,
                                                   Settings::values.texture_sampling.GetValue(), memo);
            }
        } catch (const VideoCore::ShaderRecoveryError& error) {
            // CodexAstraLocal TEST-ONLY LOG payload: this path only builds a
            // bounded error message; the joined owner emits it once. Remove
            // after mapped/numeric failures have retained operand regressions.
            const auto bits = [](f24 value) { return std::bit_cast<u32>(value.ToFloat32()); };
            std::string message = fmt::format(
                "{} unit={} type={} face={} demand={:#x} footprint={:#x} pixel={},{} lane={} "
                "covered_f32bits={:08x},{:08x},{:08x} projected_f32bits={:08x},{:08x} "
                "lod_f32bits={:08x} size={}x{} format={} minmax={}:{} bias={} filters={}/{}/{} "
                "wrap={}/{} shadow_ortho={} shadow_bias={} address={:#x} mapped={} required={}",
                error.what(), i, static_cast<u32>(type), face, demand_mask, footprint_mask,
                pixel_x, pixel_y, lane, bits(uv[coordinate].u()), bits(uv[coordinate].v()),
                bits(tc0_w), bits(u), bits(v), std::bit_cast<u32>(lod),
                texture.config.width.Value(), texture.config.height.Value(),
                static_cast<u32>(texture.format), texture.config.lod.min_level.Value(),
                texture.config.lod.max_level.Value(), texture.config.lod.bias.Value(),
                static_cast<u32>(texture.config.min_filter.Value()),
                static_cast<u32>(texture.config.mag_filter.Value()),
                static_cast<u32>(texture.config.mip_filter.Value()),
                static_cast<u32>(texture.config.wrap_s.Value()),
                static_cast<u32>(texture.config.wrap_t.Value()), regs.texturing.shadow.orthographic.Value(),
                regs.texturing.shadow.bias.Value(), prepared[i].addresses[face],
                prepared[i].faces[face].size(), prepared[i].layout.bytes);
            if (footprint_mask & (1U << i)) {
                for (u32 q = 0; q < 4; ++q) {
                    message += fmt::format(" helper{}={:08x},{:08x},{:08x}", q,
                        bits(quad[q].uv[coordinate].u()), bits(quad[q].uv[coordinate].v()), bits(quad[q].w));
                }
            }
            throw VideoCore::ShaderRecoveryError{message.c_str()};
        }
    }
    if ((demand_mask & 8U) && regs.texturing.main_config.texture3_enable) {
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
    const PreparedTevConfig& tev,
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
    Common::Vec4<u8> next_combiner_buffer = tev.initial_buffer;

    for (const auto& tev_stage : tev.stages) {
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
                return tev_stage.constant;
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
        // CodexAstraLocal: Resolve only operands with a path to the final
        // fragment. Both channels read the preceding stage before either is
        // written; Dot3_RGBA retains its unscaled alpha dependency. No arithmetic
        // or quantization changes for a live value, and buffer advancement stays
        // unconditional even when this stage produces no consumed value.
        Common::Vec3<u8> color_output{};
        if (tev_stage.live.NeedsColor(tev_stage.color_op)) {
            std::array<Common::Vec3<u8>, 3> color_result{};
            for (u32 input = 0; input < tev_stage.color_count; ++input) {
                color_result[input] = GetColorModifier(tev_stage.color_modifiers[input],
                                                      get_source(tev_stage.color_sources[input]));
            }
            color_output = ColorCombine(tev_stage.color_op, color_result);
        }

        u8 alpha_output{};
        if (tev_stage.live.alpha &&
            tev_stage.color_op == TexturingRegs::TevStageConfig::Operation::Dot3_RGBA) {
            // result of Dot3_RGBA operation is also placed to the alpha component
            alpha_output = color_output.x;
        } else if (tev_stage.live.alpha) {
            // alpha combiner
            std::array<u8, 3> alpha_result{};
            for (u32 input = 0; input < tev_stage.alpha_count; ++input) {
                alpha_result[input] = GetAlphaModifier(tev_stage.alpha_modifiers[input],
                                                      get_source(tev_stage.alpha_sources[input]));
            }
            alpha_output = AlphaCombine(tev_stage.alpha_op, alpha_result);
        }

        if (tev_stage.live.rgb) {
            combiner_output[0] = std::min(255U, color_output.r() * tev_stage.color_multiplier);
            combiner_output[1] = std::min(255U, color_output.g() * tev_stage.color_multiplier);
            combiner_output[2] = std::min(255U, color_output.b() * tev_stage.color_multiplier);
        }
        if (tev_stage.live.alpha) {
            combiner_output[3] = std::min(255U, alpha_output * tev_stage.alpha_multiplier);
        }

        combiner_buffer = next_combiner_buffer;

        if (tev_stage.update_color && tev_stage.live.rgb) {
            next_combiner_buffer.r() = combiner_output.r();
            next_combiner_buffer.g() = combiner_output.g();
            next_combiner_buffer.b() = combiner_output.b();
        }

        if (tev_stage.update_alpha && tev_stage.live.alpha) {
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

// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include <chrono> // CodexAstraLocal: Bounded owner-only diagnostic cadence.
#include "common/uberhar_parallel_work.h" // CodexAstraLocal: Bounded owner-participating CPU rows.
#include "video_core/pica/regs_texturing.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_software/sw_clipper.h"
#include "video_core/renderer_software/sw_framebuffer.h"
#include "video_core/renderer_software/sw_sampler.h"
#include "video_core/renderer_software/sw_raster_work.h" // CodexAstraLocal: Bounded owner-prepared row spans.
#include "video_core/renderer_software/sw_tev_preparation.h" // CodexAstraLocal: Exact immutable preparation reuse.

namespace Pica {
struct RegsInternal;
class PicaCore;
} // namespace Pica

namespace SwRenderer {

struct Vertex;
// CodexAstraLocal: Value-only raster commands carry exact integer coverage,
// never guest vertex pointers. The target/constant state is immutable in scope.
struct TriangleCoverage {
    u16 min_x, min_y, max_x, max_y;
    u16 scissor_x1, scissor_y1, scissor_x2, scissor_y2;
    std::array<s32,3> bias;
};
struct ConstantPrimitive {
    std::array<Common::Vec3<Fix12P4>,3> points;
    TriangleCoverage bounds;
};


class RasterizerSoftware : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerSoftware(Memory::MemorySystem& memory, Pica::PicaCore& pica);
    // CodexAstraLocal TEST-ONLY LOG: Final adaptive-worker evidence on ordinary
    // renderer teardown; remove the report after Software tuning is qualified.
    ~RasterizerSoftware() override;

    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;
    // CodexAstraLocal: Newly owned commands never cross visibility/invalidation
    // boundaries, including defensive reentry during a certified input scope.
    void DrawTriangles() override { EndCpuRasterBatch(false); }
    void FlushAll() override { EndCpuRasterBatch(true); }
    void FlushRegion(PAddr, u32) override { EndCpuRasterBatch(true); }
    void InvalidateRegion(PAddr, u32) override { EndCpuRasterBatch(true); }
    void FlushAndInvalidateRegion(PAddr, u32) override { EndCpuRasterBatch(true); }
    void ClearAll(bool) override { EndCpuRasterBatch(true); }
    bool WantsCpuRasterBatch() const override;
    bool BeginCpuRasterBatch(std::span<const CpuRasterReadRange> reads) override;
    void EndCpuRasterBatch(bool unwinding) override;

private:
    // CodexAstraLocal: Regression inspects actual admission/pool ownership
    // without adding renderer counters or a public runtime diagnostics API.
    friend struct SoftwareRendererTestAccess;
    // CodexAstraLocal: Texture descriptors retain checked guest ranges only
    // until the synchronous triangle join. No framebuffer snapshot changes
    // feedback semantics; aliases are traversed on the owner in original order.
    struct PreparedTexture {
        TextureLayout layout;
        std::array<std::span<const u8>, 6> faces{};
        std::array<PAddr, 6> addresses{};
        u32 face_count{};
    };
    struct TextureCoordinates {
        std::array<Common::Vec2<f24>, 3> uv;
        f24 w;
    };
    bool PrepareTextures(std::span<const Pica::TexturingRegs::FullTextureConfig, 3> textures,
                         std::array<PreparedTexture, 3>& prepared, u32 demand_mask) const;
    /// Computes the screen coordinates of the provided vertex.
    void MakeScreenCoords(Vertex& vtx);
    // CodexAstraLocal TEST-ONLY LOG: Sparse cumulative owner reports; no pixel logging.
    void ReportWork(bool force = false);
    // CodexAstraLocal: The caller supplies owner-time writer/FP admission facts;
    // the group borrows owned clipped vertices only until its complete join.
    bool TryConstantFan(std::span<const Pica::OutputVertex* const> fan,
                        bool concurrent_writers, bool isolated_fp);
    bool BeginConstantBatch(std::span<const CpuRasterReadRange> reads, bool concurrent_writers, bool isolated_fp);
    bool ConstantState() const;
    bool ConstantTarget() const;
    int PrepareConstantPrimitive(std::span<const Pica::OutputVertex* const,3> source,
                                 ConstantPrimitive& primitive);
    bool RenderConstantGroup(std::span<const ConstantPrimitive> primitives,
                             Common::Vec4<u8> color, bool owner_only, bool require_work);
    bool QueueConstantFan(std::span<const Pica::OutputVertex* const> fan);
    void DrainConstantGroup(bool owner_only);


    /// Processes the triangle defined by the provided vertices.
    void ProcessTriangle(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                         bool reversed = false);

    /// Returns the texture color of the currently processed pixel.
    std::array<Common::Vec4<u8>, 4> TextureColor(
        std::span<const Common::Vec2<f24>, 3> uv,
        std::span<const Pica::TexturingRegs::FullTextureConfig, 3> textures, f24 tc0_w,
        const std::array<PreparedTexture, 3>& prepared,
        const std::array<TextureCoordinates, 4>& quad, u32 lane, u32 demand_mask,
        u32 footprint_mask, u16 pixel_x, u16 pixel_y, TexelMemo* memo = nullptr) const;

    /// Returns the final pixel color with blending or logic ops applied.
    Common::Vec4<u8> PixelColor(u16 x, u16 y, Common::Vec4<u8> combiner_output) const;

    /// Emulates the TEV configuration and returns the combiner output.
    Common::Vec4<u8> WriteTevConfig(
        std::span<const Common::Vec4<u8>, 4> texture_color,
        const PreparedTevConfig& tev,
        Common::Vec4<u8> primary_color, Common::Vec4<u8> primary_fragment_color,
        Common::Vec4<u8> secondary_fragment_color);

    /// Blends fog to the combiner output if enabled.
    void WriteFog(float depth, Common::Vec4<u8>& combiner_output) const;

    /// Performs the alpha test. Returns false if the test failed.
    bool DoAlphaTest(u8 alpha) const;

    /// Performs the depth stencil test. Returns false if the test failed.
    bool DoDepthStencilTest(u16 x, u16 y, float depth) const;

private:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    Pica::RegsInternal& regs;
    // CodexAstraLocal: One/two/Auto counts the calling owner, not extra workers.
    // Persistent workers borrow only this triangle and join before it returns.
    Common::Uberhar::ParallelWork sw_workers;
    Framebuffer fb;
    // CodexAstraLocal: Only decoded register values survive a triangle join.
    PreparedTevCache tev_cache;
    // CodexAstraLocal: At most 4096 rows fit 12.4 coordinates. Reuse bounded
    // storage, borrowed only until this triangle's join, without per-draw allocation.
    std::array<RasterSpan, 4096> coverage_rows;
    // CodexAstraLocal TEST-ONLY LOG: Owner-only route counts price the bounded
    // work policy; remove after matched Software hardware qualification.
    u64 work_triangles{}, small_serial{}, ordered_serial{}, parallel_triangles{};
    u64 bounding_candidates{}, retained_candidates{};
    u64 work_samples{}, preparation_ns{}, raster_ns{}, owner_ns{}, join_ns{};
    // CodexAstraLocal: Feature policy is immutable for this renderer lifetime.
    const u32 feature_mask;
    const bool diagnostics;
    // CodexAstraLocal TEST-ONLY LOG: Per-feature attribution is owner-only and
    // removed after hardware qualification. Clock samples are not inferred savings.
    u64 input_draws{}, report_draw{}, report_session{}, errors{};
    u64 tev_ns{}, span_ns{}, row_policy_ns{}, unsafe_rows{}, owner_rows{}, helper_rows{}, helper_runs{};
    std::chrono::steady_clock::time_point last_report{};
    // CodexAstraLocal: 9 raster triangles follow at most 8 clipping planes;
    // grouped admission bounds coordinates to 1023, so storage stays <=72KiB.
    std::array<std::array<RasterSpan,1024>,9> group_rows;
    // CodexAstraLocal TEST-ONLY LOG: Owner-only group usage/cost/refusal evidence.
    u64 fan_attempts{},fan_members{},fan_groups{},fan_grouped_triangles{},fan_candidates{};
    u64 fan_owner_rows{},fan_helper_rows{},fan_samples{},fan_prepare_ns{},fan_raster_ns{};
    u64 fan_owner_ns{},fan_join_ns{};
    std::array<u64,10> fan_refusals{};
    // CodexAstraLocal: At most eight already-certified commands own a scope;
    // every capacity/error/state exit drains before later guest memory access.
    std::array<ConstantPrimitive,8> pending_constants;
    Common::Vec4<u8> constant_color{};
    u32 pending_constants_count{};
    bool constant_batch{};
    // CodexAstraLocal TEST-ONLY LOG: Distinguish fan-only and cross-call scopes.
    u64 constant_scopes{},constant_scope_refused{},constant_queued{},constant_drains{},constant_unwinds{};
    // CodexAstraLocal TEST-ONLY LOG: Disjoint sampled owner preparation scopes;
    // remove with grouped-path qualification, never present as inferred savings.
    u64 constant_admission_calls{},constant_admission_samples{},constant_admission_ns{};
    u64 constant_capture_calls{},constant_capture_samples{},constant_capture_ns{};

    // CodexAstraLocal TEST-ONLY LOG: One owner-side first-fault payload per
    // renderer, after join. Remove when the sampled numeric failures have
    // captured reproductions and regressions; no successful-pixel logging.
    bool sampling_failure_logged = false;
};

} // namespace SwRenderer

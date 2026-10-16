// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#include <algorithm>
#include <cstring>
#include <limits> // CodexAstraLocal: Defined singular/unused-derivative regression inputs.
#ifdef UBERHAR_SOFTWARE_HELPER_PROBE
// CodexAstraLocal: The small CI gate reuses these exact helper expectations
// without linking the guest system; native CTest also runs the real triangles.
#include "catch_amalgamated.hpp"
#else
#include <catch2/catch_test_macros.hpp>
#endif
#include "common/color.h"
#include "common/settings.h"
#include "common/uberhar_software_controls.h" // CodexAstraLocal: Exercise independent feature/observation policy.
#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
#include <sstream> // CodexAstraLocal: Round-trip the actual first-candidate archive, not a substitute.
#include <json.hpp> // CodexAstraLocal: Verify invocation/entry ownership in the actual capsule metadata.
#include <nihstro/inline_assembly.h>
#include "common/archives.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/uberhar_vertex_input.h" // CodexAstraLocal: Actual read-time source provenance.
#include "video_core/renderer_software/sw_lighting.h" // CodexAstraLocal: Real multi-light LUT reuse.
#include "video_core/renderer_software/sw_rasterizer.h"
#include "video_core/shader/shader.h"
#endif
#include "video_core/renderer_software/sw_frame.h"
#include "video_core/renderer_software/sw_sampler.h"
#include "video_core/renderer_software/sw_texture_demand.h"
#include "video_core/shader_recovery_error.h"
#include "video_core/utils.h"

#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
namespace SwRenderer {
// CodexAstraLocal: Inspect real private admission and created participants;
// no alternative raster loop or copied filtering implementation is the oracle.
struct SoftwareRendererTestAccess {
    static bool Safe(RasterizerSoftware& raster, const Pica::RegsInternal& regs) {
        std::array<RasterizerSoftware::PreparedTexture, 3> prepared;
        return raster.PrepareTextures(regs.texturing.GetTextures(), prepared,
            TextureDemandMask(regs.texturing, regs.lighting));
    }
    // CodexAstraLocal: Observe only owner-written preparation/dispatch evidence;
    // framebuffer comparisons remain the numerical oracle.
    static auto Work(const RasterizerSoftware& raster) {
        return std::array<u64,6>{raster.tev_cache.Generation(),raster.tev_cache.Hits(),
            raster.bounding_candidates,raster.retained_candidates,raster.small_serial,
            raster.parallel_triangles};
    }
    // CodexAstraLocal: Inspect populated measured fields, not log-format strings.
    static auto Diagnostic(const RasterizerSoftware& raster) {
        return std::array<u64,8>{raster.work_samples,raster.tev_ns,raster.span_ns,
            raster.row_policy_ns,raster.owner_rows,raster.helper_rows,raster.errors,raster.feature_mask};
    }
    // CodexAstraLocal: Explicit fixture facts exercise the real owner proof;
    // these tests own all guest memory and have no concurrent DSP/IPC writer.
    static bool Begin(RasterizerSoftware& raster,std::span<const VideoCore::RasterizerInterface::CpuRasterReadRange> reads,
                      bool writers=false,bool fp=true) { return raster.BeginConstantBatch(reads,writers,fp); }
    static bool Queue(RasterizerSoftware& raster,std::span<const Pica::OutputVertex* const> fan) {
        return raster.QueueConstantFan(fan);
    }
    static bool Fan(RasterizerSoftware& raster,std::span<const Pica::OutputVertex* const> fan,
                    bool writers=false,bool fp=true) { return raster.TryConstantFan(fan,writers,fp); }
    static auto GroupCost(const RasterizerSoftware& raster) {
        return std::array<u64,6>{raster.constant_admission_calls,raster.constant_admission_samples,
            raster.constant_admission_ns,raster.constant_capture_calls,raster.constant_capture_samples,
            raster.constant_capture_ns};
    }
    static auto Group(const RasterizerSoftware& raster) {
        return std::array<u64,7>{raster.fan_groups,raster.fan_grouped_triangles,raster.fan_candidates,
            raster.constant_queued,raster.pending_constants_count,raster.constant_drains,raster.constant_unwinds};
    }
    static unsigned Workers(const RasterizerSoftware& raster) { return raster.sw_workers.CreatedWorkers(); }
    static unsigned Available(const RasterizerSoftware& raster) { return raster.sw_workers.Available(); }
    // CodexAstraLocal: Use the real private mapper and sample consumer with
    // deliberately undefined unused helpers; no duplicated filtering oracle.
    static auto Sample(RasterizerSoftware& raster, const Pica::RegsInternal& regs, float u,
                       float v, float w, bool footprint) {
        std::array<RasterizerSoftware::PreparedTexture, 3> prepared;
        const auto demand = TextureDemandMask(regs.texturing, regs.lighting);
        raster.PrepareTextures(regs.texturing.GetTextures(), prepared, demand);
        std::array<Common::Vec2<Pica::f24>, 3> uv{};
        uv[0] = {Pica::f24::FromFloat32(u), Pica::f24::FromFloat32(v)};
        std::array<RasterizerSoftware::TextureCoordinates, 4> quad{};
        for (auto& q : quad) {
            for (auto& coord : q.uv) coord = {Pica::f24::FromFloat32(std::numeric_limits<float>::quiet_NaN()),
                Pica::f24::FromFloat32(std::numeric_limits<float>::quiet_NaN())};
            q.w = Pica::f24::Zero();
        }
        return raster.TextureColor(uv, regs.texturing.GetTextures(), Pica::f24::FromFloat32(w),
            prepared, quad, 0, demand, footprint ? 1U : 0U, 3, 7);
    }
};
}
namespace Pica {
// CodexAstraLocal: Exercise the real demand/serializer/proof seam without writing
// diagnostics into a user's dump directory or replacing shader execution.
struct SoftwareProducerTestAccess {
    using GeometryEntry = PicaCore::SoftwareGeometryEntry;
    using SnapshotType = PicaCore::SoftwareProducerSnapshot;
    // CodexAstraLocal: Invoke the actual scoped loader, with modeled raster
    // publication endpoints only; shader, live memory and FIFO stay production.
    static void Batch(PicaCore& pica,bool indexed=false) {
        pica.LoadVertices(indexed,std::chrono::steady_clock::now());
    }
    // CodexAstraLocal: Observe populated production fields rather than log strings.
    static auto TraceCost(const PicaCore& pica) {
        return std::array<u64,6>{pica.software_trace_draws,pica.software_trace_loads,
            pica.software_trace_bytes,pica.software_trace_samples,pica.software_trace_ns,
            pica.software_trace_errors};
    }
    static u32 Mask(const PicaCore& pica) { return pica.SoftwareProducerMask(); }
    static u32 Invalid(u32 mask, const OutputVertex& output) {
        return PicaCore::SoftwareInvalidSemantics(mask, output);
    }
    static OutputVertex Run(PicaCore& pica, ShaderSetup& setup, ShaderUnit& unit) {
        pica.shader_engine->SetupBatch(setup, 1);
        pica.shader_engine->Run(setup, unit);
        AttributeBuffer buffer{};
        unit.WriteOutput(pica.regs.internal.vs, buffer);
        return OutputVertex{pica.regs.internal.rasterizer, buffer};
    }
    static auto Snapshot(const PicaCore& pica, ShaderSetup& setup, const ShaderUnit& unit,
                         const OutputVertex& output, bool fresh, bool post = true,
                         const NativeInputReadTrace* trace=nullptr) {
        const auto mask = Mask(pica);
        return pica.MakeSoftwareProducerSnapshot(mask, Invalid(mask, output), output,
            setup, unit, fresh ? 0 : 1, 17, "fixture_cpu", fresh, post, 0x12345678ULL,nullptr,nullptr,trace);
    }
    // CodexAstraLocal: Drive the actual GS boundary/emitter from the fixture;
    // only file retention is replaced by an owned in-memory snapshot consumer.
    static void Begin(PicaCore& pica, GeometryEntry& entry, bool begin) {
        pica.BeginSoftwareGeometry(entry, begin);
    }
    static auto GeometryCandidate(PicaCore& pica, GeometryEntry& entry,
                                  const AttributeBuffer& buffer, u32 slot, bool publishing) {
        return pica.SoftwareGeometryCandidate(entry, buffer, slot, publishing);
    }
    static void GeometryObserve(PicaCore& pica, GeometryEntry& entry,
                                const AttributeBuffer& buffer, u32 slot, bool publishing) {
        pica.ObserveSoftwareGeometry(entry, buffer, slot, publishing);
    }
    static void SetupGeometry(PicaCore& pica) {
        pica.geometry_pipeline.Reconfigure();
        pica.geometry_pipeline.Setup(pica.shader_engine.get());
    }
    static void SubmitGeometry(PicaCore& pica, const AttributeBuffer& buffer) {
        pica.geometry_pipeline.SubmitVertex(buffer);
    }
    static void RunGeometry(PicaCore& pica, ShaderSetup& setup, GeometryShaderUnit& unit) {
        pica.shader_engine->SetupBatch(setup, setup.entry_point);
        pica.shader_engine->Run(setup, unit);
    }
};
}
#endif
namespace {
using Config = Pica::TexturingRegs::TextureConfig;
using Format = Pica::TexturingRegs::TextureFormat;
using Color = Common::Vec4<u8>;
constexpr auto Game = Settings::TextureSampling::GameControlled;

void Put(std::span<u8> bytes, const SwRenderer::TextureLayout& layout, u32 level,
         u32 x, u32 y, Color color) {
    const auto& info = layout.levels[level];
    const u32 inverted_y = info.height - 1 - y;
    const std::size_t offset = layout.offsets[level] + (inverted_y / 8) * info.stride +
        VideoCore::GetMortonOffset(x, inverted_y, 4);
    Common::Color::EncodeRGBA8(color, bytes.data() + offset);
}
Config Sampler(u32 size = 8, u32 max = 0) {
    Config config{};
    config.width.Assign(size); config.height.Assign(size);
    config.wrap_s.Assign(Config::ClampToEdge); config.wrap_t.Assign(Config::ClampToEdge);
    config.lod.max_level.Assign(max);
    return config;
}
}

TEST_CASE("Software LCD capture bounds rotation formats and eyes", "[software]") {
    // CodexAstraLocal: A padded 2x3 source must become [b,d,f,a,c,e], not
    // include padding or the byte immediately beyond its last visible row.
    Pica::FramebufferConfig fb{};
    fb.width.Assign(2); fb.height.Assign(3);
    Pica::ColorFill fill{};
    const std::array<Color, 6> colors{{{255,0,0,255},{0,255,0,255},{0,0,255,255},
                                      {255,255,0,255},{255,0,255,255},{0,255,255,255}}};
    for (u32 format = 0; format < 5; ++format) {
        fb.color_format.Assign(static_cast<Pica::PixelFormat>(format));
        const u32 bpp = Pica::BytesPerPixel(fb.color_format);
        fb.stride = 2 * bpp + 7;
        std::vector<u8> input(2 * fb.stride + 2 * bpp, 0x6d);
        for (u32 i = 0; i < colors.size(); ++i) {
            u8* out = input.data() + (i / 2) * fb.stride + (i % 2) * bpp;
            switch (fb.color_format) {
            case Pica::PixelFormat::RGBA8: Common::Color::EncodeRGBA8(colors[i], out); break;
            case Pica::PixelFormat::RGB8: Common::Color::EncodeRGB8(colors[i], out); break;
            case Pica::PixelFormat::RGB565: Common::Color::EncodeRGB565(colors[i], out); break;
            case Pica::PixelFormat::RGB5A1: Common::Color::EncodeRGB5A1(colors[i], out); break;
            case Pica::PixelFormat::RGBA4: Common::Color::EncodeRGBA4(colors[i], out); break;
            }
        }
        const auto captured = SwRenderer::CaptureScreen(fb, fill, input);
        CHECK(captured.width == 2); CHECK(captured.height == 3);
        const std::array<u32, 6> order{1,3,5,0,2,4};
        REQUIRE(captured.pixels.size() == 24);
        for (u32 i = 0; i < order.size(); ++i)
            CHECK(std::memcmp(captured.pixels.data() + i * 4, colors[order[i]].AsArray(), 4) == 0);
        CHECK_THROWS_AS(SwRenderer::CaptureScreen(fb, fill, std::span<const u8>{input}.first(input.size()-1)), VideoCore::ShaderRecoveryError);
        // CodexAstraLocal: Format/stride are independent guest register writes.
        // A wider width register must use the same bounded visible row as the
        // accelerated displays, preserving rotation without reading the next row.
        std::vector<u8> compact(6 * bpp);
        for (u32 row = 0; row < 3; ++row)
            std::memcpy(compact.data() + row * 2 * bpp, input.data() + row * fb.stride, 2 * bpp);
        fb.width.Assign(3); fb.stride = 2 * bpp;
        const auto narrow = SwRenderer::CaptureScreen(fb, fill, compact);
        CHECK(narrow.width == 2); CHECK(narrow.height == 3);
        CHECK(narrow.pixels == captured.pixels);
        CHECK_THROWS_AS(SwRenderer::CaptureScreen(fb, fill, std::span<const u8>{compact}.first(compact.size()-1)), VideoCore::ShaderRecoveryError);
        CHECK_THROWS_AS(SwRenderer::CaptureScreen(fb, fill, {}), VideoCore::ShaderRecoveryError);
        fb.width.Assign(2);
    }
    fb.address_left1 = 8; fb.address_left2 = 16; fb.address_right1 = 24; fb.address_right2 = 32;
    CHECK(SwRenderer::ScreenAddress(fb, false) == 8); CHECK(SwRenderer::ScreenAddress(fb, true) == 24);
    fb.active_fb = 1;
    CHECK(SwRenderer::ScreenAddress(fb, false) == 16); CHECK(SwRenderer::ScreenAddress(fb, true) == 32);
    fb.address_right1 = 0;
    CHECK(SwRenderer::ScreenAddress(fb, true) == 16);
    fill.is_enabled.Assign(1); fill.color_r.Assign(17); fill.color_g.Assign(34); fill.color_b.Assign(51);
    fb.format = 7; fb.stride = 0;
    CHECK(SwRenderer::CaptureScreen(fb, fill, {}).pixels == std::vector<u8>{17,34,51,255});
}

#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
TEST_CASE("Software LCD startup register transitions", "[software][software-startup]") {
    // CodexAstraLocal: Exercise actual PICA boot registers and physical VRAM,
    // followed by separate format/stride writes before the first guest draw.
    // This is a source-defined startup transition, not a claimed Thor register dump.
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory,{}};
    for (u32 buffer = 0; buffer < 2; ++buffer) {
        for (u32 screen = 0; screen < 3; ++screen) {
            auto& fb = pica.regs.framebuffer_config[screen == 2 ? 1 : 0];
            fb.active_fb = buffer;
            const auto ref = memory.GetPhysicalRef(SwRenderer::ScreenAddress(fb, screen == 1));
            REQUIRE(ref.GetPtr() != nullptr);
            const auto capture = [&](const Pica::ColorFill& fill = {}) {
                return SwRenderer::CaptureScreen(fb, fill, {ref.GetPtr(), ref.GetSize()});
            };
            fb.color_format.Assign(Pica::PixelFormat::RGB8); fb.stride = 720;
            auto image = capture();
            CHECK(image.width == 240); CHECK(image.height == (screen == 2 ? 320 : 400));
            CHECK(image.pixels.size() == static_cast<std::size_t>(image.width) * image.height * 4);
            // A format change can arrive before its stride update. Both existing
            // accelerated displays use min(width, stride / bytes_per_pixel).
            fb.color_format.Assign(Pica::PixelFormat::RGBA8);
            image = capture();
            CHECK(image.width == 180); CHECK(image.height == (screen == 2 ? 320 : 400));
            fb.stride = 960;
            image = capture();
            CHECK(image.width == 240);
            // Empty visible rectangles require no memory. Once nonempty again,
            // unmapped storage still fails with the actual dimensions and span.
            fb.stride = 0;
            CHECK(capture().pixels.empty());
            fb.stride = 960;
            CHECK_THROWS_AS(SwRenderer::CaptureScreen(fb, {}, {}), VideoCore::ShaderRecoveryError);
        }
    }
}
#endif

TEST_CASE("Software ordinary mip filters and signed border taps", "[software][software-sampling]") {
    auto config = Sampler(32, 2);
    config.mag_filter.Assign(Config::Nearest); config.min_filter.Assign(Config::Linear);
    auto layout = SwRenderer::MakeTextureLayout(config, Format::RGBA8, false);
    std::vector<u8> data(layout.bytes);
    for (u32 level = 0; level < 3; ++level)
        for (u32 y = 0; y < layout.levels[level].height; ++y)
            for (u32 x = 0; x < layout.levels[level].width; ++x)
                Put(data, layout, level, x, y, {static_cast<u8>(x & 1 ? 200 : 0),static_cast<u8>(level * 80),0,255});
    const auto sample = [&](float u, float v, float lod) {
        return SwRenderer::SampleTexture(data, layout, config, u, v, lod, Game);
    };
    CHECK(sample(1.f/32, .5f/32, -1).x == 200);
    // CodexAstraLocal: Register MIN_LOD applies before min/mag selection,
    // matching GL_TEXTURE_MIN_LOD and Vulkan minLod (Khronos Samplers mapping).
    config.lod.min_level.Assign(2);
    CHECK(sample(1.f/8, .5f/8, -4).x == 100);
    CHECK(sample(1.f/8, .5f/8, -4).y == 160);
    config.lod.min_level.Assign(0); config.lod.max_level.Assign(0);
    CHECK(sample(1.f/32, .5f/32, 8).x == 200); // maxLOD0 selects mag.
    config.lod.max_level.Assign(2); config.mip_filter.Assign(Config::Linear);
    CHECK(sample(.25f, .25f, .5f).y == 40);
    config.lod.bias.Assign(-256);
    CHECK(sample(.25f, .25f, 1.5f).y == 40);
    config.lod.bias.Assign(0); config.lod.max_level.Assign(0); config.mag_filter.Assign(Config::Linear);
    config.wrap_s.Assign(Config::ClampToBorder); config.border_color.r.Assign(60); config.border_color.a.Assign(255);
    CHECK(sample(0, .5f/32, 0).x == 30);
    config.wrap_s.Assign(Config::Repeat);
    CHECK(sample(0, .5f/32, 0).x == 100);
    config.wrap_s.Assign(Config::MirroredRepeat);
    CHECK(sample(0, .5f/32, 0).x == 0);
    CHECK(SwRenderer::SampleTexture(data, layout, config, 1.f/32, .5f/32, 0,
                                     Settings::TextureSampling::NearestNeighbor).x == 200);
    std::array<Common::Vec2f,4> quad{{{0,0},{.25f,0},{0,.125f},{.25f,.125f}}};
    CHECK(SwRenderer::TextureLod(quad, 0, 32, 32) == 3.f);
    CHECK_THROWS_AS(SwRenderer::SampleTexture(std::span<const u8>{data}.first(2), layout, config,
        0,0,0,Game), VideoCore::ShaderRecoveryError);
    // CodexAstraLocal: Fixed LOD needs no derivative; consumed UV/memory still
    // must be valid. Nonzero fixed LOD uses minification before image clamping.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    config.wrap_s.Assign(Config::ClampToEdge);
    config.mag_filter.Assign(Config::Nearest); config.min_filter.Assign(Config::Linear);
    CHECK(sample(1.f/32, .5f/32, nan).x == 200);
    config.lod.min_level.Assign(2); config.lod.max_level.Assign(2);
    CHECK(sample(1.f/8, .5f/8, nan).x == 100);
    CHECK(sample(1.f/8, .5f/8, nan).y == 160);
    CHECK_THROWS_AS(sample(nan, .5f, nan), VideoCore::ShaderRecoveryError);
    config.lod.min_level.Assign(0);
    CHECK_THROWS_AS(sample(.5f, .5f, nan), VideoCore::ShaderRecoveryError);
    quad[2].y = nan; // NaN in a later max operand must not be hidden.
    CHECK(std::isnan(SwRenderer::TextureLod(quad, 0, 32, 32)));
}

TEST_CASE("Software shadow PCF compares depth before filtering", "[software][software-sampling]") {
    const auto config = Sampler();
    const auto layout = SwRenderer::MakeTextureLayout(config, Format::RGBA8, true);
    std::vector<u8> data(layout.bytes);
    // CodexAstraLocal: Packed color has intensity in R and big-endian D24 in
    // guest bytes. Depth equality must fail, and two depths must compare first.
    for (u32 y = 0; y < 8; ++y) for (u32 x = 0; x < 8; ++x)
        Put(data, layout, 0, x, y, {200,static_cast<u8>(x & 1 ? 20 : 10),0,0});
    CHECK(SwRenderer::SampleShadow(data, layout, .5f/8,.5f/8,10,false).x == 0);
    CHECK(SwRenderer::SampleShadow(data, layout, 1.f/8,.5f/8,15,false).x == 100);
    CHECK(SwRenderer::SampleShadow(data, layout, 1.f/8,.5f/8,0,false).x == 200);
    CHECK(SwRenderer::SampleShadow(data, layout, 0,.5f/8,30,false).x == 128);
    CHECK(SwRenderer::SampleShadow(data, layout, 0,.5f/8,30,true).x == 0);
    // CodexAstraLocal: Far finite coordinates have defined PCF border/edge
    // values before conversion. Near-border mixed taps must stay fractional.
    const float large = std::numeric_limits<float>::max();
    for (float outside : {-large, large}) {
        CHECK(SwRenderer::SampleShadow(data, layout, outside,.5f,0,false).x == 255);
        CHECK(SwRenderer::SampleShadow(data, layout, .5f,outside,0,false).x == 255);
        CHECK(SwRenderer::SampleShadow(data, layout, outside,.5f,0,true).x == 200);
        CHECK(SwRenderer::SampleShadow(data, layout, .5f,outside,30,true).x == 0);
    }
    CHECK(SwRenderer::SampleShadow(data, layout, -.5f/8,.5f,0,false).x == 255);
    CHECK(SwRenderer::SampleShadow(data, layout, 1.f+.5f/8,.5f,0,false).x == 255);
    CHECK(SwRenderer::SampleShadow(data, layout, -.25f/8,.5f,0,false).x == 241);
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()}) {
        CHECK_THROWS_AS(SwRenderer::SampleShadow(data, layout, invalid,.5f,0,false), VideoCore::ShaderRecoveryError);
        CHECK_THROWS_AS(SwRenderer::SampleShadow(data, layout, .5f,invalid,0,true), VideoCore::ShaderRecoveryError);
    }
}

TEST_CASE("Software texture demand follows consumed TEV and lighting operands", "[software][software-sampling][software-liveness]") {
    // CodexAstraLocal: Reserved/unused operand fields cannot map guest texture
    // bytes. Keep Software's existing stage0 alpha Previous behavior explicit.
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    std::array<Stage, 6> stages{};
    // CodexAstraLocal: Earlier-stage expectations require an actual chain to
    // final output; a later Replace would deliberately make them dead.
    for (u32 i = 1; i < stages.size(); ++i) {
        stages[i].color_source1.Assign(Source::Previous);
        stages[i].alpha_source1.Assign(Source::Previous);
    }
    Pica::LightingRegs lighting{}; lighting.disable.Assign(1);
    using Policy = Pica::TevLiveness::Stage0Policy;
    const auto demand = [&] { return SwRenderer::GetFragmentDemand(
        Pica::TevLiveness::Analyze(stages, 0, 0, Policy::Software), lighting); };
    const auto mask = [&] { return demand().textures; };
    auto& s = stages[0];
    s.color_source2.Assign(Source::Texture1); s.color_source3.Assign(Source::Texture2);
    s.alpha_source2.Assign(Source::Texture3);
    CHECK(mask() == 0);
    s.color_source1.Assign(Source::Previous); CHECK(mask() == 4);
    s.color_source1.Assign(Source::Texture0);
    s.color_op.Assign(Stage::Operation::Modulate); CHECK(mask() == 3);
    s.color_op.Assign(Stage::Operation::Lerp); CHECK(mask() == 7);
    s.color_op.Assign(Stage::Operation::Dot3_RGBA);
    s.alpha_source1.Assign(Source::Texture3); CHECK(mask() == 3);
    s.color_op.Assign(Stage::Operation::Dot3_RGB); CHECK(mask() == 11);
    s.color_op.Assign(Stage::Operation::Replace); s.color_source1.Assign(Source::PrimaryColor);
    s.alpha_source1.Assign(Source::Previous); s.alpha_source3.Assign(Source::Texture3);
    CHECK(mask() == 0); // Actual Software alpha Previous reads initial combiner zero.
    s.alpha_op.Assign(Stage::Operation::Lerp); CHECK(mask() == 8);
    s = {};
    lighting.disable.Assign(0); lighting.config0.enable_shadow.Assign(1);
    lighting.config0.shadow_selector.Assign(3);
    // CodexAstraLocal: Enabled but unconsumed lighting cannot request its
    // shadow/bump inputs. A late executed operand must restore that demand.
    CHECK(mask() == 0);
    CHECK(demand().primary_color);
    CHECK_FALSE(demand().lighting);
    stages[5].alpha_source1.Assign(Source::SecondaryFragmentColor);
    CHECK(mask() == 8);
    CHECK(demand().lighting);
    lighting.config0.bump_mode.Assign(Pica::LightingRegs::LightingBumpMode::NormalMap);
    lighting.config0.bump_selector.Assign(1); CHECK(mask() == 10);
    lighting.disable.Assign(1); CHECK(mask() == 0);
    stages[5].color_source1.Assign(Source::Texture2); CHECK(mask() == 4);

    // CodexAstraLocal: Pure backward dependencies preserve independent channel
    // paths and the two-stage buffer delay. No value-based zero/NaN folding.
    const auto reset = [&] {
        for (auto& stage : stages) {
            stage = {};
            stage.color_source1.Assign(Source::Constant);
            stage.alpha_source1.Assign(Source::Constant);
        }
    };
    const auto analyze = [&](u32 rgb = 0, u32 alpha = 0, Policy policy = Policy::Software) {
        return Pica::TevLiveness::Analyze(stages, rgb, alpha, policy);
    };
    reset();
    stages[0].color_source1.Assign(Source::Texture0);
    stages[0].alpha_source1.Assign(Source::Texture1);
    CHECK(analyze().external_sources == 0); // Later constant replaces both.
    stages[5].color_source1.Assign(Source::PreviousBuffer);
    CHECK(analyze(1).Uses(Source::Texture0));
    CHECK_FALSE(analyze(1).Uses(Source::Texture1));
    stages[5].color_modifier1.Assign(Stage::ColorModifier::SourceAlpha);
    CHECK_FALSE(analyze(1, 1).Uses(Source::Texture0));
    CHECK(analyze(1, 1).Uses(Source::Texture1));
    stages[5].alpha_source1.Assign(Source::PreviousBuffer);
    stages[5].alpha_modifier1.Assign(Stage::AlphaModifier::SourceRed);
    CHECK(analyze(1, 1).Uses(Source::Texture0));
    stages[3].alpha_source1.Assign(Source::Texture2);
    CHECK(analyze(1, 9).Uses(Source::Texture2));
    CHECK_FALSE(analyze(1, 9).Uses(Source::Texture1)); // Latest alpha write wins.
    reset();
    stages[0].color_source1.Assign(Source::Texture0);
    stages[1].color_source1.Assign(Source::PreviousBuffer);
    for (u32 i = 2; i < stages.size(); ++i) stages[i].color_source1.Assign(Source::Previous);
    CHECK_FALSE(analyze(1).Uses(Source::Texture0)); // Stage1 sees initial buffer.
    stages[2].color_source1.Assign(Source::PreviousBuffer);
    CHECK(analyze(1).Uses(Source::Texture0)); // Stage2 sees stage0's write.
    reset();
    stages[4].color_op.Assign(Stage::Operation::Dot3_RGBA);
    stages[4].color_source1.Assign(Source::Texture0);
    stages[4].color_source2.Assign(Source::Texture1);
    stages[4].alpha_source1.Assign(Source::Texture2);
    stages[5].alpha_source1.Assign(Source::Previous);
    auto live = analyze();
    CHECK_FALSE(live.stages[4].rgb);
    CHECK(live.stages[4].alpha);
    CHECK(live.Uses(Source::Texture0)); CHECK(live.Uses(Source::Texture1));
    CHECK_FALSE(live.Uses(Source::Texture2));
    reset();
    stages[4].alpha_source1.Assign(Source::Texture3);
    stages[5].color_source1.Assign(Source::Previous);
    stages[5].color_modifier1.Assign(Stage::ColorModifier::SourceAlpha);
    CHECK(analyze().Uses(Source::Texture3));
    CHECK_FALSE(analyze().stages[4].rgb);
    stages[4].color_source1.Assign(Source::Texture2);
    stages[5].alpha_source1.Assign(Source::Previous);
    stages[5].alpha_modifier1.Assign(Stage::AlphaModifier::SourceRed);
    CHECK(analyze().Uses(Source::Texture2));
    reset();
    for (u32 i = 1; i < stages.size(); ++i) stages[i].alpha_source1.Assign(Source::Previous);
    stages[0].alpha_source1.Assign(Source::Previous);
    stages[0].alpha_source3.Assign(Source::Texture0);
    CHECK_FALSE(analyze().Uses(Source::Texture0));
    CHECK(analyze(0, 0, Policy::GeneratedGlsl).Uses(Source::Texture0));
    // CodexAstraLocal: A captured live texture chain remains demanded even if
    // another captured program overwrites it. No game-specific admission exists.
    reset();
    stages[0].sources_raw = 0x00030003;
    stages[0].ops_raw = 0x00010001;
    for (u32 i = 1; i < stages.size(); ++i) stages[i].sources_raw = 0x000f000f;
    CHECK(analyze(2, 2).Uses(Source::Texture0));
    CHECK(analyze(2, 2).Uses(Source::PrimaryColor));
}

TEST_CASE("Software cube filtering crosses edges with owned face identity", "[software]") {
    auto config = Sampler(); config.mag_filter.Assign(Config::Linear);
    const auto layout = SwRenderer::MakeTextureLayout(config, Format::RGBA8, false);
    std::array<std::vector<u8>,6> storage;
    std::array<std::span<const u8>,6> faces;
    for (u32 face=0; face<6; ++face) {
        storage[face].resize(layout.bytes);
        for (u32 y=0; y<8; ++y) for (u32 x=0; x<8; ++x)
            Put(storage[face],layout,0,x,y,{static_cast<u8>(face*40),0,0,255});
        faces[face]=storage[face];
    }
    CHECK(SwRenderer::SampleCubeTexture(faces,layout,config,0,0,.5f,0,Game).x == 80); // +X/+Z
    CHECK(SwRenderer::SampleCubeTexture(faces,layout,config,0,1,.5f,0,Game).x == 100); // +X/-Z
    CHECK(SwRenderer::SampleCubeTexture(faces,layout,config,0,.5f,.5f,0,Game).x == 0);
    faces[0]=faces[1]; // Aliased contents must not replace explicit face orientation.
    CHECK(SwRenderer::SampleCubeTexture(faces,layout,config,0,0,.5f,0,Game).x == 100);
}

TEST_CASE("Software texel memo preserves filtered formats mip faces and shadow", "[software][software-memo]") {
    // CodexAstraLocal: Compare the real sampler's new tap source with its live
    // decoder over finite synthetic bytes. No copied filtering/ETC oracle or
    // game shader is used. Per-format memos cover mip changes; shared-face and
    // same-pointer/different-format key checks are separate cases below.
    auto config = Sampler(32, 2);
    config.min_filter.Assign(Config::Linear);
    config.mag_filter.Assign(Config::Linear);
    config.mip_filter.Assign(Config::Linear);
    config.wrap_s.Assign(Config::Repeat);
    config.wrap_t.Assign(Config::MirroredRepeat);
    SwRenderer::TexelMemo memo;
    const std::array<Common::Vec2f, 7> positions{{{.01f,.01f}, {.24f,.26f}, {.49f,.51f},
        {.99f,.99f}, {-.02f,.1f}, {1.02f,.9f}, {2.25f,-.25f}}};
    for (u32 format = 0; format <= static_cast<u32>(Format::ETC1A4); ++format) {
        const auto layout = SwRenderer::MakeTextureLayout(config, static_cast<Format>(format), false);
        std::vector<u8> bytes(layout.bytes);
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<u8>(i * 37 + i / 11 + 19);
        // A fresh immutable lifetime starts for each allocation, even if its
        // allocator happens to reuse the exact prior address/format.
        SwRenderer::TexelMemo format_memo;
        for (float lod : {0.f, .5f, 1.f, 1.5f, 2.f}) {
            for (const auto uv : positions) {
                INFO("format=" << format << " lod=" << lod << " uv=" << uv.x << ',' << uv.y);
                const auto live = SwRenderer::SampleTexture(bytes, layout, config, uv.x, uv.y, lod, Game);
                CHECK(SwRenderer::SampleTexture(bytes, layout, config, uv.x, uv.y, lod, Game, &format_memo) == live);
                CHECK(SwRenderer::SampleTexture(bytes, layout, config, uv.x, uv.y, lod, Game, &format_memo) == live);
            }
        }
        if (format >= static_cast<u32>(Format::ETC1)) {
            // Expensive compressed texels also use the memo for nearest taps.
            auto nearest = config;
            nearest.mag_filter.Assign(Config::Nearest);
            nearest.min_filter.Assign(Config::Nearest);
            for (const auto uv : positions) {
                CHECK(SwRenderer::SampleTexture(bytes, layout, nearest, uv.x, uv.y, .5f, Game, &format_memo) ==
                      SwRenderer::SampleTexture(bytes, layout, nearest, uv.x, uv.y, .5f, Game));
            }
        }
    }

    const auto layout = SwRenderer::MakeTextureLayout(config, Format::RGBA8, false);
    std::array<std::vector<u8>, 6> storage;
    std::array<std::span<const u8>, 6> faces;
    for (u32 face = 0; face < faces.size(); ++face) {
        storage[face].resize(layout.bytes);
        for (std::size_t i = 0; i < layout.bytes; ++i) storage[face][i] = static_cast<u8>(i * 13 + face * 31);
        faces[face] = storage[face];
    }
    faces[3] = faces[0]; // Different cube identity may share one physical source.
    for (u32 face = 0; face < faces.size(); ++face) {
        for (const auto uv : positions) {
            CHECK(SwRenderer::SampleCubeTexture(faces, layout, config, face, uv.x, uv.y, .5f, Game, &memo) ==
                  SwRenderer::SampleCubeTexture(faces, layout, config, face, uv.x, uv.y, .5f, Game));
        }
    }
    auto shadow_config = Sampler(32);
    const auto shadow_layout = SwRenderer::MakeTextureLayout(shadow_config, Format::RGBA8, true);
    for (bool cube : {false, true}) for (u32 depth : {0U, 0x123456U, 0xffffffU}) {
        for (const auto uv : positions) {
            CHECK(SwRenderer::SampleShadow(faces[0], shadow_layout, uv.x, uv.y, depth, cube, &memo) ==
                  SwRenderer::SampleShadow(faces[0], shadow_layout, uv.x, uv.y, depth, cube));
        }
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(SwRenderer::SampleTexture(faces[0], layout, config, nan, .5f, 0, Game, &memo), VideoCore::ShaderRecoveryError);
    CHECK_THROWS_AS(SwRenderer::SampleTexture(faces[0], layout, config, .5f, .5f, nan, Game, &memo), VideoCore::ShaderRecoveryError);
    CHECK_THROWS_AS(SwRenderer::SampleTexture(faces[0].first(layout.bytes - 1), layout, config, .5f, .5f, 0, Game, &memo), VideoCore::ShaderRecoveryError);
    CHECK_THROWS_AS(SwRenderer::SampleShadow(faces[0], shadow_layout, nan, .5f, 0, false, &memo), VideoCore::ShaderRecoveryError);
}

TEST_CASE("Software texel memo admission and lifetime preserve live writes", "[software][software-memo]") {
    // CodexAstraLocal: No alias proof can authorize reuse during concurrent
    // writes. Intentionally changing bytes here exposes that distinction:
    // admitted immutable samples reuse, bypass and a fresh group see new bytes.
    CHECK(SwRenderer::CanMemoizeTexels(true, false));
    CHECK_FALSE(SwRenderer::CanMemoizeTexels(false, false));
    CHECK_FALSE(SwRenderer::CanMemoizeTexels(true, true));
    CHECK_FALSE(SwRenderer::CanMemoizeTexels(false, true));
    auto config = Sampler(8);
    config.mag_filter.Assign(Config::Linear);
    const auto layout = SwRenderer::MakeTextureLayout(config, Format::RGBA8, false);
    std::vector<u8> bytes(layout.bytes);
    const auto fill = [&](Color color) {
        for (u32 y = 0; y < 8; ++y) for (u32 x = 0; x < 8; ++x) Put(bytes, layout, 0, x, y, color);
    };
    fill({21,42,63,255});
    SwRenderer::TexelMemo group;
    CHECK(SwRenderer::SampleTexture(bytes, layout, config, .5f, .5f, 0, Game, &group) == Color{21,42,63,255});
    fill({32,64,96,255});
    CHECK(SwRenderer::SampleTexture(bytes, layout, config, .5f, .5f, 0, Game, &group) == Color{21,42,63,255});
    CHECK(SwRenderer::SampleTexture(bytes, layout, config, .5f, .5f, 0, Game) == Color{32,64,96,255});
    SwRenderer::TexelMemo next_group;
    CHECK(SwRenderer::SampleTexture(bytes, layout, config, .5f, .5f, 0, Game, &next_group) == Color{32,64,96,255});
    // The same pointer interpreted as a different format must not reuse bytes.
    const auto other = SwRenderer::MakeTextureLayout(config, Format::RGBA4, false);
    CHECK(SwRenderer::SampleTexture(bytes, other, config, .5f, .5f, 0, Game, &next_group) ==
          SwRenderer::SampleTexture(bytes, other, config, .5f, .5f, 0, Game));
    // Nearest uncompressed sampling has no memo setup/use benefit and remains live.
    config.mag_filter.Assign(Config::Nearest);
    fill({17,34,51,255});
    CHECK(SwRenderer::SampleTexture(bytes, layout, config, .5f, .5f, 0, Game, &next_group) == Color{17,34,51,255});
}

#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
TEST_CASE("Software producer capsule preserves demanded inputs and carry limits",
          "[software][software-producer]") {
    // CodexAstraLocal: The first candidate is evidence, not an injected renderer
    // error. Actual CPU shader output and the native save serializers must retain
    // NaN payloads, mapping and post-carry without inventing a replay pre-state.
    using Access = Pica::SoftwareProducerTestAccess;
    using Semantic = Pica::RasterizerRegs::VSOutputAttributes;
    using Stage = Pica::TexturingRegs::TevStageConfig;
    const auto old_mode = Settings::values.uberhar_test_mode.GetValue();
    const auto old_jit = Settings::values.use_shader_jit.GetValue();
    struct Restore {
        Settings::UberharTestMode mode; bool jit;
        ~Restore() { Settings::values.uberhar_test_mode = mode; Settings::values.use_shader_jit = jit; }
    } restore{old_mode, old_jit};
    Settings::values.uberhar_test_mode = Settings::UberharTestMode::Software;
    Settings::values.use_shader_jit = false;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory,{}};
    auto& regs = pica.regs.internal;
    regs = {}; regs.lighting.disable.Assign(1);
    for (auto* stage : {&regs.texturing.tev_stage0, &regs.texturing.tev_stage1,
                       &regs.texturing.tev_stage2, &regs.texturing.tev_stage3,
                       &regs.texturing.tev_stage4, &regs.texturing.tev_stage5}) {
        stage->color_source1.Assign(Stage::Source::Constant);
        stage->alpha_source1.Assign(Stage::Source::Constant);
    }
    Pica::OutputVertex semantic{};
    const auto nan = std::bit_cast<Pica::f24>(0x7fc12345U);
    semantic.tc0.x = nan;
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == 0);
    // CodexAstraLocal: Keep stage0 connected to final output so the producer
    // fixture observes genuinely consumed attributes, not overwritten values.
    for (auto* stage : {&regs.texturing.tev_stage1, &regs.texturing.tev_stage2,
                       &regs.texturing.tev_stage3, &regs.texturing.tev_stage4,
                       &regs.texturing.tev_stage5}) {
        stage->color_source1.Assign(Stage::Source::Previous);
        stage->alpha_source1.Assign(Stage::Source::Previous);
    }
    regs.texturing.tev_stage0.color_source1.Assign(Stage::Source::Texture0);
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0.address.Assign(Memory::VRAM_PADDR / 8);
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == (1U << Semantic::TEXCOORD0_U));
    regs.texturing.texture0.address.Assign(0); // The actual sampler returns constant black here.
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == 0);
    regs.texturing.texture0.address.Assign(Memory::VRAM_PADDR / 8);
    regs.texturing.texture0.type.Assign(Config::Disabled);
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == 0);
    regs.texturing.texture0.type.Assign(Config::Projection2D);
    semantic.tc0 = {}; semantic.tc0_w = nan;
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == (1U << Semantic::TEXCOORD0_W));
    regs.texturing.texture0.type.Assign(Config::Texture2D);
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == 0);
    regs.texturing.tev_stage0.color_source1.Assign(Stage::Source::Texture2);
    regs.texturing.main_config.texture2_enable.Assign(1);
    regs.texturing.main_config.texture2_use_coord1.Assign(1);
    regs.texturing.texture2.address.Assign(Memory::VRAM_PADDR / 8);
    semantic.tc1.y = nan; semantic.tc2.x = nan;
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == (1U << Semantic::TEXCOORD1_V));
    regs.texturing.tev_stage0.color_source1.Assign(Stage::Source::Constant);
    regs.texturing.tev_stage0.alpha_source1.Assign(Stage::Source::Previous);
    regs.texturing.tev_stage0.alpha_source3.Assign(Stage::Source::Texture1);
    regs.texturing.main_config.texture1_enable.Assign(1);
    regs.texturing.texture1.address.Assign(Memory::VRAM_PADDR / 8);
    CHECK(Access::Invalid(Access::Mask(pica), semantic) == 0); // Retained Software alpha semantics.

    const auto compile = [](std::initializer_list<nihstro::InlineAsm> code) {
        const auto binary = nihstro::InlineAsm::CompileToRawBinary(code);
        Pica::ShaderSetup setup;
        setup.uniforms = {};
        Pica::ProgramCode program{};
        Pica::SwizzleData swizzles{};
        std::transform(binary.program.begin(), binary.program.end(), program.begin(),
                       [](const auto& word) { return word.hex; });
        std::transform(binary.swizzle_table.begin(), binary.swizzle_table.end(), swizzles.begin(),
                       [](const auto& word) { return word.hex; });
        setup.UpdateProgramCode(program);
        setup.UpdateSwizzleData(swizzles);
        setup.entry_point = 1;
        return setup;
    };
    using Op = nihstro::OpCode::Id;
    using Dest = nihstro::DestRegister;
    using Source = nihstro::SourceRegister;
    auto setup = compile({{Op::NOP}, {Op::MOV, Dest::MakeOutput(0), Source::MakeInput(0)}, {Op::END}});
    setup.uniforms.f[2] = {nan, Pica::f24::One(), Pica::f24::Zero(), Pica::f24::One()};
    setup.uniforms.b[3] = true; setup.uniforms.i[1] = {1, 2, 3, 4};
    regs.vs.output_mask.Assign(1);
    regs.rasterizer.vs_output_total.Assign(1);
    auto& mapping = regs.rasterizer.vs_output_attributes[0];
    mapping.map_x.Assign(Semantic::TEXCOORD0_U); mapping.map_y.Assign(Semantic::TEXCOORD0_V);
    mapping.map_z.Assign(Semantic::INVALID); mapping.map_w.Assign(Semantic::TEXCOORD0_W);
    regs.texturing.tev_stage0.color_source1.Assign(Stage::Source::Texture0);
    Pica::ShaderUnit unit;
    unit.input[0] = {nan, Pica::f24::One(), Pica::f24::Zero(), Pica::f24::One()};
    unit.temporary[7].w = nan; unit.address_registers[2] = 19; unit.conditional_code[1] = true;
    unit.output[1][4].z = nan;
    const auto output = Access::Run(pica, setup, unit);
    REQUIRE(Access::Invalid(Access::Mask(pica), output) == (1U << Semantic::TEXCOORD0_U));
    // CodexAstraLocal: The snapshot metadata computes lazy shader hashes after
    // serializing their cache fields. Compare traced/untraced payloads from the
    // same stable hash state, rather than mistaking cache population for input edits.
    (void)setup.GetProgramCodeHash();(void)setup.GetSwizzleDataHash();
    const auto snapshot = Access::Snapshot(pica, setup, unit, output, false);
    // CodexAstraLocal: Exact source bytes are copied by actual typed loading;
    // later RAM mutation cannot change the retained metadata payload.
    std::array<u32,4> source_bits{0x7fc12345,0x3f800000,0,0x3f800000};
    const std::array<Pica::NativeInputAttribute,1> description{{{0,16,4,Pica::PipelineRegs::VertexAttributeFormat::FLOAT,false}}};
    Pica::ShaderRegs trace_regs{};Pica::NativeVertexInputPlan trace_plan;
    REQUIRE(trace_plan.Prepare(trace_regs,1,0x123400,0,[&](u32){return description[0];},
        [&](PAddr){return std::span<const u8>{reinterpret_cast<const u8*>(source_bits.data()),16};})==Pica::NativeVertexInputPlan::Result::Ready);
    Pica::ShaderUnit trace_unit;Pica::NativeInputReadTrace trace;
    trace_plan.LoadObserved(trace_unit,{},0,0x123400,description,trace);
    source_bits[0]=0;
    const auto traced=Access::Snapshot(pica,setup,unit,output,false,true,&trace);
    const auto metadata=nlohmann::json::parse(traced.metadata);
    CHECK(metadata["input_provenance"]["available"]==true);
    CHECK(metadata["input_provenance"]["guest_bytes"]==16);
    CHECK(metadata["input_provenance"]["attributes"][0]["address"]==0x123400);
    CHECK(metadata["input_provenance"]["attributes"][0]["bytes_hex"]=="4523c17f0000803f000000000000803f");
    CHECK(metadata["input_provenance"]["attributes"][0]["origin"]=="actual_scalar_load");
    const bool same_native_payload=traced.payload==snapshot.payload;
    CHECK(same_native_payload); // Keep failure XML bounded; native binary strings contain control bytes.

    // CodexAstraLocal: The same capsule remains evidence on other hosts, but
    // only the recorded exact FP-control contracts authorize value replay.
#if (defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))) || \
    defined(__i386__) || defined(__x86_64__)
    constexpr bool replay_host = true;
#else
    constexpr bool replay_host = false;
#endif
    REQUIRE(snapshot.replay_qualified == replay_host);
    REQUIRE(snapshot.payload.size() < 64 * 1024);
    REQUIRE(snapshot.metadata.size() < 4096);
    CHECK(snapshot.metadata.find("\"entry\":1") != std::string::npos);
    CHECK(snapshot.metadata.find("post_shader_not_pre_carry") != std::string::npos);
    Pica::RegsInternal restored_regs{};
    Pica::ShaderSetup restored_setup;
    Pica::ShaderUnit restored_unit;
    Pica::OutputVertex restored_output{};
    std::istringstream stream{snapshot.payload, std::ios::in | std::ios::binary};
    iarchive archive{stream};
    archive >> restored_regs.reg_array >> restored_setup >> restored_unit >> restored_output;
    CHECK(restored_regs.reg_array == regs.reg_array);
    CHECK(restored_setup.GetProgramCode() == setup.GetProgramCode());
    CHECK(restored_setup.GetSwizzleData() == setup.GetSwizzleData());
    CHECK(std::memcmp(restored_setup.uniforms.f.data(), setup.uniforms.f.data(), sizeof(setup.uniforms.f)) == 0);
    CHECK(restored_setup.uniforms.b == setup.uniforms.b);
    CHECK(restored_setup.uniforms.i == setup.uniforms.i);
    CHECK(std::memcmp(restored_unit.input.data(), unit.input.data(), sizeof(unit.input)) == 0);
    CHECK(std::memcmp(restored_unit.temporary.data(), unit.temporary.data(), sizeof(unit.temporary)) == 0);
    CHECK(std::memcmp(restored_unit.output.data(), unit.output.data(), sizeof(unit.output)) == 0);
    CHECK(restored_unit.output_bank == unit.output_bank);
    CHECK(restored_unit.address_registers[2] == 19);
    CHECK(restored_unit.conditional_code[1]);
    CHECK(std::bit_cast<u32>(restored_output.tc0.x) == 0x7fc12345U);
    CHECK(std::bit_cast<u32>(unit.input[0].x) == 0x7fc12345U);
    Pica::ShaderUnit replay;
    replay.input = restored_unit.input;
    const auto replay_output = Access::Run(pica, restored_setup, replay);
    CHECK(std::bit_cast<u32>(replay_output.tc0.x) == std::bit_cast<u32>(output.tc0.x));

    // CodexAstraLocal: A current input cannot reconstruct a prior invocation's
    // mutable temporary. Fresh carry is bounded; late effects still refuse it.
    auto carry = compile({{Op::NOP}, {Op::MOV, Dest::MakeOutput(0), Source::MakeTemporary(0)},
        {Op::MOV, Dest::MakeTemporary(0), Source::MakeInput(0)}, {Op::END}});
    Pica::ShaderUnit carried;
    carried.input[0] = unit.input[0];
    (void)Access::Run(pica, carry, carried);
    const auto carried_output = Access::Run(pica, carry, carried);
    CHECK_FALSE(Access::Snapshot(pica, carry, carried, carried_output, false).replay_qualified);
    Pica::ShaderUnit fresh;
    fresh.input[0] = unit.input[0];
    const auto fresh_output = Access::Run(pica, carry, fresh);
    CHECK(Access::Snapshot(pica, carry, fresh, fresh_output, true).replay_qualified == replay_host);
    CHECK(std::bit_cast<u32>(carried_output.tc0.x) == 0x7fc12345U);
    CHECK(std::bit_cast<u32>(fresh_output.tc0.x) == 0U);
    auto effect = compile({{Op::NOP}, {Op::MOV, Dest::MakeOutput(0), Source::MakeTemporary(0)},
        {Op::MOV, Dest::MakeTemporary(0), Source::MakeInput(0)}, {Op::EMIT}, {Op::END}});
    CHECK_FALSE(Access::Snapshot(pica, effect, fresh, fresh_output, true).replay_qualified);
    const auto mid_emit = Access::Snapshot(pica, setup, unit, output, false, false);
    CHECK_FALSE(mid_emit.replay_qualified);
    CHECK(mid_emit.metadata.find("buffered GS vertex may precede current emit") != std::string::npos);
}

TEST_CASE("Software geometry capsule owns the producing emit and entry carry",
          "[software][software-producer-geometry]") {
    // CodexAstraLocal: A later EMIT may publish an older slot. Exercise real
    // interpreter/GeometryPipeline/emitter code and keep the captured producer's
    // source/entry, never the later publisher's uniforms or post-state.
    using Access = Pica::SoftwareProducerTestAccess;
    using Semantic = Pica::RasterizerRegs::VSOutputAttributes;
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Op = nihstro::OpCode::Id;
    using Dest = nihstro::DestRegister;
    using Source = nihstro::SourceRegister;
    const auto old_mode = Settings::values.uberhar_test_mode.GetValue();
    const auto old_jit = Settings::values.use_shader_jit.GetValue();
    struct Restore {
        Settings::UberharTestMode mode; bool jit;
        ~Restore() { Settings::values.uberhar_test_mode = mode; Settings::values.use_shader_jit = jit; }
    } restore{old_mode, old_jit};
    Settings::values.uberhar_test_mode = Settings::UberharTestMode::Software;
    Settings::values.use_shader_jit = false;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory,{}};
    auto& regs = pica.regs.internal;
    regs = {}; regs.lighting.disable.Assign(1);
    for (auto* stage : {&regs.texturing.tev_stage0, &regs.texturing.tev_stage1,
                       &regs.texturing.tev_stage2, &regs.texturing.tev_stage3,
                       &regs.texturing.tev_stage4, &regs.texturing.tev_stage5}) {
        stage->color_source1.Assign(Stage::Source::Constant);
        stage->alpha_source1.Assign(Stage::Source::Constant);
    }
    // CodexAstraLocal: Keep stage0 connected to final output so the producer
    // fixture observes genuinely consumed attributes, not overwritten values.
    for (auto* stage : {&regs.texturing.tev_stage1, &regs.texturing.tev_stage2,
                       &regs.texturing.tev_stage3, &regs.texturing.tev_stage4,
                       &regs.texturing.tev_stage5}) {
        stage->color_source1.Assign(Stage::Source::Previous);
        stage->alpha_source1.Assign(Stage::Source::Previous);
    }
    regs.texturing.tev_stage0.color_source1.Assign(Stage::Source::Texture0);
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0.address.Assign(Memory::VRAM_PADDR / 8);
    regs.pipeline.use_gs.Assign(Pica::PipelineRegs::UseGS::Yes);
    regs.pipeline.gs_config.mode.Assign(Pica::PipelineRegs::GSMode::Point);
    regs.gs.shader_mode.Assign(Pica::ShaderRegs::ShaderMode::GS);
    regs.gs.main_offset.Assign(1);
    regs.gs.output_mask.Assign(1);
    regs.rasterizer.vs_output_total.Assign(1);
    auto& mapping = regs.rasterizer.vs_output_attributes[0];
    mapping.map_x.Assign(Semantic::TEXCOORD0_U); mapping.map_y.Assign(Semantic::TEXCOORD0_V);
    mapping.map_z.Assign(Semantic::INVALID); mapping.map_w.Assign(Semantic::TEXCOORD0_W);

    const auto make_program = [](std::initializer_list<nihstro::InlineAsm> code) {
        const auto binary = nihstro::InlineAsm::CompileToRawBinary(code);
        Pica::ShaderSetup setup;
        setup.uniforms = {};
        Pica::ProgramCode program{}; Pica::SwizzleData swizzles{};
        std::transform(binary.program.begin(), binary.program.end(), program.begin(),
                       [](const auto& word) { return word.hex; });
        std::transform(binary.swizzle_table.begin(), binary.swizzle_table.end(), swizzles.begin(),
                       [](const auto& word) { return word.hex; });
        setup.UpdateProgramCode(program); setup.UpdateSwizzleData(swizzles); setup.entry_point = 1;
        return setup;
    };
    const auto setemit = [](Pica::ShaderSetup& setup, u32 pc, u32 slot, bool publish) {
        nihstro::Instruction word{};
        word.opcode = nihstro::OpCode{Op::SETEMIT};
        word.setemit.vertex_id.Assign(slot); word.setemit.prim_emit.Assign(publish);
        setup.UpdateProgramCode(pc, word.hex);
    };
    auto first = make_program({{Op::NOP}, {Op::MOV, Dest::MakeOutput(0), Source::MakeTemporary(0)},
        {Op::NOP}, {Op::EMIT}, {Op::MOV, Dest::MakeOutput(0), Source::MakeInput(0)},
        {Op::NOP}, {Op::EMIT}, {Op::END}});
    setemit(first, 2, 0, false); setemit(first, 5, 1, false);
    const auto nan = std::bit_cast<Pica::f24>(0x7fc12345U);
    first.uniforms.f[3].x = nan; first.uniforms.b[2] = true;
    pica.gs_setup = first;
    pica.gs_unit.emitter.buffer = {};
    pica.gs_unit.emitter.buffer[0][5].w = nan; // Unwritten slot carry, outside the mapped prefix.
    pica.gs_unit.emitter.emit_state.raw = 0;
    pica.gs_unit.temporary[0] = {nan, Pica::f24::One(), Pica::f24::Zero(), Pica::f24::One()};
    pica.gs_unit.address_registers[2] = 19;
    pica.gs_unit.conditional_code[1] = true;
    pica.gs_unit.output[1][4].z = nan;
    auto& handlers = *pica.gs_unit.emitter.handlers;
    Access::GeometryEntry entry;
    std::optional<Access::SnapshotType> retained;
    std::vector<Pica::AttributeBuffer> published;
    u32 begins{}, ends{}, writes{};
    handlers.vertex_handler = [&](const Pica::AttributeBuffer& buffer) { published.push_back(buffer); };
    handlers.winding_setter = [] {};
    handlers.invocation_observer = [&](bool begin) {
        begin ? ++begins : ++ends;
        Access::Begin(pica, entry, begin);
    };
    handlers.emit_observer = [&](const Pica::AttributeBuffer& buffer, u32 slot, bool publishing) {
        writes += !publishing;
        if (auto candidate = Access::GeometryCandidate(pica, entry, buffer, slot, publishing))
            retained = std::move(candidate);
    };
    Pica::AttributeBuffer input{};
    input[0] = {Pica::f24::One(), Pica::f24::One(), Pica::f24::Zero(), Pica::f24::One()};
    Access::SetupGeometry(pica);
    Access::SubmitGeometry(pica, input);
    REQUIRE(entry.pending[0].has_value());
    CHECK_FALSE(retained.has_value());
    CHECK(begins == 1); CHECK(ends == 1); CHECK(writes == 2);
    CHECK(published.empty()); CHECK(entry.semantic_mask == 0);
    const auto metadata = nlohmann::json::parse(entry.pending[0]->metadata);
    CHECK(metadata.at("schema") == 2);
    CHECK(metadata.at("geometry").at("invocation") == 1);
    CHECK(metadata.at("geometry").at("emit_ordinal") == 1);
    CHECK(metadata.at("geometry").at("slot") == 0);
    CHECK_FALSE(entry.pending[0]->replay_qualified);
    CHECK(entry.pending[0]->metadata.find("upstream VS") != std::string::npos);
    CHECK(metadata.at("geometry").at("written_attribute_count") == 1);

    // CodexAstraLocal: Decode the actual schema-2 extension. It must retain the
    // pre-entry state, including carry/input and bool15 BEFORE the pipeline sets
    // it after Run; neither the second emit nor next shader may replace it.
    Pica::RegsInternal restored_regs{};
    Pica::ShaderSetup restored_setup;
    Pica::ShaderUnit mid_unit, entry_unit;
    Pica::OutputVertex captured_output{};
    Pica::AttributeBuffer raw_slot{};
    std::array<u8, sizeof(raw_slot)> raw_slot_bytes{};
    std::istringstream stream{entry.pending[0]->payload, std::ios::in | std::ios::binary};
    iarchive archive{stream};
    archive >> restored_regs.reg_array >> restored_setup >> mid_unit >> captured_output >> entry_unit >> raw_slot_bytes;
    std::memcpy(&raw_slot, raw_slot_bytes.data(), raw_slot_bytes.size());
    CHECK(restored_setup.GetProgramCode() == first.GetProgramCode());
    CHECK(restored_setup.GetSwizzleData() == first.GetSwizzleData());
    CHECK(std::bit_cast<u32>(restored_setup.uniforms.f[3].x) == 0x7fc12345U);
    CHECK_FALSE(restored_setup.uniforms.b[15]); CHECK(pica.gs_setup.uniforms.b[15]);
    CHECK(std::memcmp(entry_unit.input.data(), pica.gs_unit.input.data(), sizeof(entry_unit.input)) == 0);
    CHECK(std::bit_cast<u32>(entry_unit.temporary[0].x) == 0x7fc12345U);
    CHECK(std::bit_cast<u32>(entry_unit.output[1][4].z) == 0x7fc12345U);
    CHECK(entry_unit.address_registers[2] == 19); CHECK(entry_unit.conditional_code[1]);
    CHECK(std::bit_cast<u32>(entry_unit.output[0][0].x) == 0U);
    CHECK(std::bit_cast<u32>(mid_unit.output[0][0].x) == 0x7fc12345U);
    CHECK(std::bit_cast<u32>(captured_output.tc0.x) == 0x7fc12345U);
    CHECK(std::bit_cast<u32>(raw_slot[0].x) == 0x7fc12345U);
    CHECK(std::bit_cast<u32>(raw_slot[5].w) == 0x7fc12345U);
    CHECK(metadata.at("unit_correspondence").get<std::string>().find("carried/unqualified") != std::string::npos);

    auto publisher = make_program({{Op::NOP}, {Op::MOV, Dest::MakeOutput(0), Source::MakeInput(0)},
                                  {Op::NOP}, {Op::EMIT}, {Op::END}});
    setemit(publisher, 2, 2, true);
    publisher.uniforms.f[3].x = Pica::f24::FromFloat32(42);
    pica.gs_setup = publisher;
    regs.texturing.texture0.border_color.raw = 0x76543210; // Consumer register state can change too.
    Access::SetupGeometry(pica);
    Access::SubmitGeometry(pica, input);
    CHECK(begins == 2); CHECK(ends == 2); CHECK(writes == 3);
    REQUIRE(published.size() == 3);
    REQUIRE(retained.has_value());
    CHECK(std::bit_cast<u32>(published[0][0].x) == 0x7fc12345U);
    CHECK(published[1][0].x == Pica::f24::One()); CHECK(published[2][0].x == Pica::f24::One());
    CHECK(restored_setup.GetProgramCode() != pica.gs_setup.GetProgramCode());
    const auto published_metadata = nlohmann::json::parse(retained->metadata);
    CHECK(published_metadata.at("geometry") == metadata.at("geometry"));
    CHECK(published_metadata.at("publication").at("invocation") == 2);
    CHECK(published_metadata.at("publication").at("slot") == 0);
    CHECK(published_metadata.at("program_hash") == metadata.at("program_hash"));
    CHECK_FALSE(entry.pending[0].has_value());
    Pica::RegsInternal publication_regs{};
    Pica::OutputVertex publication_output{};
    std::istringstream publication_stream{retained->payload.substr(
        published_metadata.at("publication_archive_offset").get<std::size_t>()),
        std::ios::in | std::ios::binary};
    iarchive publication_archive{publication_stream};
    publication_archive >> publication_regs.reg_array >> publication_output;
    CHECK(publication_regs.reg_array == regs.reg_array);
    CHECK(publication_regs.reg_array != restored_regs.reg_array);
    CHECK(std::bit_cast<u32>(publication_output.tc0.x) == 0x7fc12345U);
    CHECK(retained->payload.size() < 64 * 1024);

    // CodexAstraLocal: This finite synthetic GS can reproduce NEW slot values
    // from its actual entry. That check does not qualify arbitrary captured GS
    // termination, previous buffered primitives, full frames or GPU equivalence.
    Pica::GeometryShaderUnit replay;
    static_cast<Pica::ShaderUnit&>(replay) = entry_unit;
    replay.emitter_ptr = &replay.emitter;
    replay.emitter.output_mask = 1; replay.emitter.emit_state.raw = 0; replay.emitter.buffer = {};
    Pica::Handlers replay_handlers;
    replay.emitter.handlers = &replay_handlers;
    std::vector<Pica::AttributeBuffer> replay_writes;
    replay_handlers.emit_observer = [&](const Pica::AttributeBuffer& value, u32, bool publishing) {
        if (!publishing) replay_writes.push_back(value);
    };
    restored_setup.entry_point = metadata.at("entry").get<u32>();
    Access::RunGeometry(pica, restored_setup, replay);
    REQUIRE(replay_writes.size() == 2);
    CHECK(std::bit_cast<u32>(replay_writes[0][0].x) == 0x7fc12345U);
    CHECK(replay_writes[1][0].x == Pica::f24::One());

    // CodexAstraLocal: A non-publishing invalid slot may be overwritten before
    // any triangle uses it. The pending capsule must disappear rather than
    // consume the once-only retained incident or blame a later finite vertex.
    retained.reset();
    pica.gs_setup = first;
    Access::SetupGeometry(pica); Access::SubmitGeometry(pica, input);
    REQUIRE(entry.pending[0].has_value());
    auto overwrite = publisher;
    setemit(overwrite, 2, 0, false);
    pica.gs_setup = overwrite;
    Access::SetupGeometry(pica); Access::SubmitGeometry(pica, input);
    CHECK_FALSE(entry.pending[0].has_value());
    pica.gs_setup = publisher;
    Access::SetupGeometry(pica); Access::SubmitGeometry(pica, input);
    CHECK_FALSE(retained.has_value());

    // CodexAstraLocal: Observe an unused signaling color lane through the real
    // nonfatal wrapper. Even conversion's FP effects are restored; finite UVs
    // do not request file retention or a new renderer error.
    mapping.map_z.Assign(Semantic::COLOR_R);
    auto observed = input;
    observed[0].z = std::bit_cast<Pica::f24>(0x7f800001U);
    Access::Begin(pica, entry, true);
    const auto before_flags = Common::Uberhar::ParallelFloatEnvironment::Flags();
    Access::GeometryObserve(pica, entry, observed, 0, false);
    CHECK(Common::Uberhar::ParallelFloatEnvironment::Flags() == before_flags);
    CHECK_FALSE(entry.pending[0].has_value());
    Access::Begin(pica, entry, false);

    // CodexAstraLocal: Throwing optional observers cannot cancel original
    // publication or leave entry active. No renderer/sampler error is injected.
    handlers.invocation_observer = [](bool) { throw std::runtime_error{"observer only"}; };
    handlers.emit_observer = [](const Pica::AttributeBuffer&, u32, bool) { throw std::runtime_error{"observer only"}; };
    const auto prior_publications = published.size();
    Access::SetupGeometry(pica);
    CHECK_NOTHROW(Access::SubmitGeometry(pica, input));
    CHECK(published.size() == prior_publications + 3);
    Pica::GeometryEmitter standalone{};
    standalone.output_mask = 1; standalone.emit_state.raw = 0;
    CHECK_NOTHROW(standalone.Emit(input));
}

TEST_CASE("Software actual demanded sampling and fixed helper ownership", "[software][software-sampling]") {
    // CodexAstraLocal: Real MemorySystem/raster admission must not touch an
    // enabled, malformed unused texture. Changing the actual consumer restores
    // its refusal; fixed projection LOD ignores only unused helper values.
    const auto old_workers = Settings::values.software_renderer_workers.GetValue();
    struct Restore { u32 value; ~Restore() { Settings::values.software_renderer_workers = value; } } restore{old_workers};
    Settings::values.software_renderer_workers = 1;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory,{}};
    auto& regs = pica.regs.internal;
    regs = {}; regs.lighting.disable.Assign(1);
    SwRenderer::RasterizerSoftware raster{memory, pica};
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0 = Sampler(16, 0);
    regs.texturing.texture0.type.Assign(Config::Projection2D);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR + 0x10000) / 8);
    regs.texturing.texture0_format.Assign(Format::RGBA8);
    auto layout = SwRenderer::MakeTextureLayout(regs.texturing.texture0, Format::RGBA8, false);
    std::span<u8> bytes{memory.GetPhysicalPointer(regs.texturing.texture0.GetPhysicalAddress()), layout.bytes};
    for (u32 y = 0; y < 16; ++y) for (u32 x = 0; x < 16; ++x)
        Put(bytes, layout, 0, x, y, {41,82,123,255});
    using Source = Pica::TexturingRegs::TevStageConfig::Source;
    // CodexAstraLocal: A chain to the final stage keeps this sampler control
    // live after backward TEV preparation; unused malformed units stay dead.
    for (auto* stage : {&regs.texturing.tev_stage1, &regs.texturing.tev_stage2,
                       &regs.texturing.tev_stage3, &regs.texturing.tev_stage4,
                       &regs.texturing.tev_stage5}) {
        stage->color_source1.Assign(Source::Previous);
        stage->alpha_source1.Assign(Source::Previous);
    }
    regs.texturing.tev_stage0.color_source1.Assign(Source::Texture0);
    regs.texturing.main_config.texture1_enable.Assign(1);
    regs.texturing.texture1.address.Assign((Memory::VRAM_PADDR + 0x20000) / 8); // zero invalid dimensions
    CHECK(SwRenderer::SoftwareRendererTestAccess::Safe(raster, regs));
    CHECK(SwRenderer::SoftwareRendererTestAccess::Sample(raster, regs, .5f,.5f,1.f,false)[0] == Color{41,82,123,255});
    regs.texturing.tev_stage1.alpha_source1.Assign(Source::Texture1);
    CHECK_THROWS_AS(SwRenderer::SoftwareRendererTestAccess::Safe(raster, regs), VideoCore::ShaderRecoveryError);
    regs.texturing.tev_stage1.alpha_source1.Assign(Source::PrimaryColor);
    CHECK_THROWS_AS(SwRenderer::SoftwareRendererTestAccess::Sample(raster, regs, .5f,.5f,0.f,false), VideoCore::ShaderRecoveryError);
    regs.texturing.texture0.lod.max_level.Assign(1);
    CHECK_THROWS_AS(SwRenderer::SoftwareRendererTestAccess::Sample(raster, regs, .5f,.5f,1.f,true), VideoCore::ShaderRecoveryError);
    // Demanding no texture must leave even singular covered values unused.
    regs.texturing.tev_stage0.color_source1.Assign(Source::PrimaryColor);
    CHECK_NOTHROW(SwRenderer::SoftwareRendererTestAccess::Sample(raster, regs,
        std::numeric_limits<float>::quiet_NaN(), .5f, 0.f, false));
}

TEST_CASE("Software actual triangle budgets and alias admission", "[software]") {
    // CodexAstraLocal: Assign changes only each register field, preserving its
    // neighboring fields. Restore global controls; use actual tiled guest memory,
    // actual rasterization/TEV/depth and synchronous worker joins for each mode.
    const auto old_workers=Settings::values.software_renderer_workers.GetValue();
    const auto old_mode=Settings::values.uberhar_test_mode.GetValue();
    struct Restore { u32 workers; Settings::UberharTestMode mode; ~Restore() {
        Settings::values.software_renderer_workers=workers; Settings::values.uberhar_test_mode=mode;
    }} restore{old_workers,old_mode};
    Settings::values.uberhar_test_mode=Settings::UberharTestMode::Software;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory,{}};
    auto& regs=pica.regs.internal;
    regs = {};
    regs.lighting.disable.Assign(1);
    regs.rasterizer.viewport_size_x.Assign(0x440000); regs.rasterizer.viewport_size_y.Assign(0x440000);
    regs.rasterizer.viewport_depth_near_plane.Assign(0x3f0000);
    auto& target=regs.framebuffer.framebuffer;
    target.width.Assign(64);target.height.Assign(63);target.color_format.Assign(Pica::FramebufferRegs::ColorFormat::RGBA8);
    target.color_buffer_address.Assign((Memory::VRAM_PADDR+0x40000)/8);
    target.depth_buffer_address.Assign((Memory::VRAM_PADDR+0x50000)/8);
    target.depth_format.Assign(Pica::FramebufferRegs::DepthFormat::D16);
    target.allow_color_write.Assign(15);
    regs.framebuffer.output_merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    regs.framebuffer.output_merger.red_enable.Assign(1);regs.framebuffer.output_merger.green_enable.Assign(1);
    regs.framebuffer.output_merger.blue_enable.Assign(1);regs.framebuffer.output_merger.alpha_enable.Assign(1);
    auto* color=memory.GetPhysicalPointer(target.GetColorBufferPhysicalAddress());
    const auto vertex=[](float x,float y) {
        Pica::OutputVertex v{};
        const auto f=[](float v){return Pica::f24::FromFloat32(v);};
        v.pos={f(x),f(y),f(-.5f),f(1)};v.color={f(1),f(.5f),f(.25f),f(1)};
        v.quat.w=f(1);v.tc0={f((x+1)*.5f),f((y+1)*.5f)};v.tc0_w=f(1);
        return v;
    };
    const auto a=vertex(-.9f,-.9f),b=vertex(.9f,-.9f),c=vertex(-.9f,.9f),d=vertex(.9f,.9f);
    // CodexAstraLocal: The compared triangles really consume filtered tiled
    // texture bytes through TEV, rather than only exercising a flat-color loop.
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0=Sampler(64);
    regs.texturing.texture0.mag_filter.Assign(Config::Linear);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR+0x10000)/8);
    regs.texturing.texture0_format.Assign(Format::RGBA8);
    const auto texture_layout=SwRenderer::MakeTextureLayout(regs.texturing.texture0,Format::RGBA8,false);
    std::span<u8> texture{memory.GetPhysicalPointer(regs.texturing.texture0.GetPhysicalAddress()),texture_layout.bytes};
    for(u32 y=0;y<64;++y)for(u32 x=0;x<64;++x)
        Put(texture,texture_layout,0,x,y,{static_cast<u8>(x*4),static_cast<u8>(y*4),128,255});
    for(auto* stage : {&regs.texturing.tev_stage0,&regs.texturing.tev_stage1,&regs.texturing.tev_stage2,
                      &regs.texturing.tev_stage3,&regs.texturing.tev_stage4,&regs.texturing.tev_stage5}) {
        stage->color_source1.Assign(Pica::TexturingRegs::TevStageConfig::Source::Texture0);
        stage->alpha_source1.Assign(Pica::TexturingRegs::TevStageConfig::Source::Texture0);
    }
    std::vector<u8> reference;
    for (u32 budget : {1U,2U,0U}) {
        // CodexAstraLocal: Recreate a renderer at the same storage location and
        // address: cached attachments must initialize even after an earlier one.
        INFO("Software participant budget " << budget);
        Settings::values.software_renderer_workers=budget;
        SwRenderer::RasterizerSoftware raster{memory,pica};
        REQUIRE(SwRenderer::SoftwareRendererTestAccess::Safe(raster,regs));
        const auto available=SwRenderer::SoftwareRendererTestAccess::Available(raster);
        CHECK(available== (budget ? std::min(budget,Common::Uberhar::AvailableProcessors()) : Common::Uberhar::AvailableProcessors()));
        std::memset(color,0,64*64*4);
        raster.AddTriangle(a,b,c);raster.AddTriangle(b,d,c);
        // CodexAstraLocal: The real two-triangle draw reuses its immutable plan
        // and bypasses empty bounding pixels without altering framebuffer bytes.
        const auto work=SwRenderer::SoftwareRendererTestAccess::Work(raster);
        CHECK(work[0]==1); CHECK(work[1]>=1); CHECK(work[2]>work[3]);
        if(available>1) CHECK(work[5]>0);
        std::vector<u8> output(color,color+64*64*4);
        REQUIRE(std::count(output.begin(),output.end(),255)>100);
        if (reference.empty()) reference=output; else CHECK(output==reference);
    }
    // CodexAstraLocal: Independent feature switches and diagnostic-off retain
    // exact actual framebuffer output. Measured enabled fields must populate;
    // disabled observation performs no optional timing or counter accumulation.
    const auto old_features=Settings::software_renderer_features.GetValue();
    const auto old_diagnostics=Settings::software_renderer_diagnostics.GetValue();
    struct RestoreFeatures { u32 mask; bool diagnostics; ~RestoreFeatures() {
        Settings::software_renderer_features=mask;Settings::software_renderer_diagnostics=diagnostics;
    }} restore_features{old_features,old_diagnostics};
    for(u32 mask:{15U,14U,13U,11U,7U,0U}) for(bool observe:{false,true}) {
        Settings::software_renderer_features=mask;Settings::software_renderer_diagnostics=observe;
        SwRenderer::RasterizerSoftware comparison{memory,pica};
        std::memset(color,0,64*64*4);
        comparison.AddTriangle(a,b,c);comparison.AddTriangle(b,d,c);
        CHECK(std::vector<u8>(color,color+64*64*4)==reference);
        const auto data=SwRenderer::SoftwareRendererTestAccess::Diagnostic(comparison);
        CHECK(data[7]==mask);
        if(observe) {
            CHECK(data[0]>0);CHECK(data[1]>0);CHECK(data[2]>0);CHECK(data[3]>0);
            CHECK(data[4]+data[5]>0);CHECK(data[6]==0);
            const auto work=SwRenderer::SoftwareRendererTestAccess::Work(comparison);
            if(mask&1) {CHECK(work[0]==1);CHECK(work[1]>=1);}
            else {CHECK(work[0]==2);CHECK(work[1]==0);}
            if(mask&2) CHECK(work[2]>work[3]); else CHECK(work[2]==work[3]);
        } else for(u32 i=0;i<7;++i) CHECK(data[i]==0);
    }
    Settings::software_renderer_features=old_features;
    Settings::software_renderer_diagnostics=old_diagnostics;
    Settings::values.software_renderer_workers=2;
    {
        // CodexAstraLocal: Real covered tiny work remains on the owner even
        // with a two-participant allowance; no sleeper is created just for it.
        SwRenderer::RasterizerSoftware tiny{memory,pica};
        tiny.AddTriangle(a,vertex(-.8f,-.9f),vertex(-.9f,-.8f));
        const auto work=SwRenderer::SoftwareRendererTestAccess::Work(tiny);
        CHECK(work[3]>0); CHECK(work[4]==1); CHECK(work[5]==0);
        CHECK(SwRenderer::SoftwareRendererTestAccess::Workers(tiny)==0);
    }
    SwRenderer::RasterizerSoftware raster{memory,pica};
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0=Sampler(64);
    regs.texturing.texture0.address.Assign(target.color_buffer_address);
    regs.texturing.texture0_format.Assign(Format::RGBA8);
    CHECK_FALSE(SwRenderer::SoftwareRendererTestAccess::Safe(raster,regs));
    raster.AddTriangle(a,b,c);
    CHECK(SwRenderer::SoftwareRendererTestAccess::Workers(raster)==0);
    // A tail mip, and then only the sixth cube face, aliases the output.
    regs.texturing.texture0=Sampler(16,1);
    regs.texturing.texture0.address.Assign((target.GetColorBufferPhysicalAddress()-1024)/8);
    CHECK_FALSE(SwRenderer::SoftwareRendererTestAccess::Safe(raster,regs));
    regs.texturing.texture0=Sampler(8);
    regs.texturing.texture0.type.Assign(Config::TextureCube);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR+0x10000)/8);
    for(u32 face=0;face<5;++face)regs.texturing.cube_address[face].Assign((Memory::VRAM_PADDR+0x10000)/8);
    regs.texturing.cube_address[4].Assign(target.color_buffer_address);
    CHECK_FALSE(SwRenderer::SoftwareRendererTestAccess::Safe(raster,regs));
    regs.texturing.main_config.texture0_enable.Assign(0);
    target.allow_color_write.Assign(0);target.color_buffer_address.Assign(0);
    target.allow_depth_stencil_write.Assign(1);regs.framebuffer.output_merger.depth_write_enable.Assign(1);
    CHECK(SwRenderer::SoftwareRendererTestAccess::Safe(raster,regs));
    CHECK_NOTHROW(raster.AddTriangle(a,b,c));
    target.allow_depth_stencil_write.Assign(0);target.depth_buffer_address.Assign(0);
    CHECK(SwRenderer::SoftwareRendererTestAccess::Safe(raster,regs));
}

// CodexAstraLocal: The actual Software kernel must preserve output across
// joined ordinary triangles, owned fan grouping, bounded cross-call queues and
// owner-only error drains. Host facts are explicit, not a fake powered guest.
TEST_CASE("Software constant groups preserve ownership aliases and drain boundaries", "[software]") {
    using Access=SwRenderer::SoftwareRendererTestAccess;
    const auto old_features=Settings::software_renderer_features.GetValue();
    const auto old_workers=Settings::values.software_renderer_workers.GetValue();
    const auto old_diagnostics=Settings::software_renderer_diagnostics.GetValue();
    struct Restore { u32 features,workers;bool diagnostics;~Restore(){Settings::software_renderer_features=features;
        Settings::values.software_renderer_workers=workers;Settings::software_renderer_diagnostics=diagnostics;}
    } restore{old_features,old_workers,old_diagnostics};
    Settings::values.software_renderer_workers=2;Settings::software_renderer_diagnostics=true;
    Core::System system;Memory::MemorySystem memory{system};Pica::PicaCore pica{memory,{}};
    auto& regs=pica.regs.internal;regs={};regs.lighting.disable.Assign(1);
    regs.rasterizer.depthmap_enable.Assign(Pica::RasterizerRegs::DepthBuffering::ZBuffering);
    regs.rasterizer.viewport_size_x.Assign(0x440000);regs.rasterizer.viewport_size_y.Assign(0x440000);
    auto& target=regs.framebuffer.framebuffer;target.width.Assign(64);target.height.Assign(63);
    target.color_format.Assign(Pica::FramebufferRegs::ColorFormat::RGBA8);
    target.color_buffer_address.Assign((Memory::VRAM_PADDR+0x40000)/8);target.allow_color_write.Assign(15);
    auto& m=regs.framebuffer.output_merger;m.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    m.red_enable.Assign(1);m.green_enable.Assign(1);m.blue_enable.Assign(1);m.alpha_enable.Assign(1);
    using Stage=Pica::TexturingRegs::TevStageConfig;
    regs.texturing.tev_stage5.color_source1.Assign(Stage::Source::Constant);
    regs.texturing.tev_stage5.alpha_source1.Assign(Stage::Source::Constant);
    regs.texturing.tev_stage5.const_color=0xff4080ff;
    auto* color=memory.GetPhysicalPointer(target.GetColorBufferPhysicalAddress());
    const auto vertex=[](float x,float y) { Pica::OutputVertex v{};const auto f=[](float z){return Pica::f24::FromFloat32(z);};
        v.pos={f(x),f(y),f(-.5f),f(1)};return v; };
    std::array<Pica::OutputVertex,4> quad{vertex(-.9f,-.9f),vertex(.9f,-.9f),vertex(.9f,.9f),vertex(-.9f,.9f)};
    const std::array<const Pica::OutputVertex*,4> fan{&quad[0],&quad[1],&quad[2],&quad[3]};
    std::vector<u8> reference;
    Settings::software_renderer_features=7;
    { SwRenderer::RasterizerSoftware raster{memory,pica};std::memset(color,0,64*64*4);
      raster.AddTriangle(quad[0],quad[1],quad[2]);raster.AddTriangle(quad[0],quad[2],quad[3]);
      reference.assign(color,color+64*64*4); }
    REQUIRE(std::count(reference.begin(),reference.end(),255)>100);
    Settings::software_renderer_features=15;
    SwRenderer::RasterizerSoftware raster{memory,pica};
    std::memset(color,0,64*64*4);
    REQUIRE(Access::Fan(raster,fan));CHECK(std::vector<u8>(color,color+64*64*4)==reference);
    CHECK(Access::Group(raster)[0]==1);CHECK(Access::Group(raster)[1]==2);CHECK(Access::Group(raster)[2]>1024);
    CHECK_FALSE(Access::Fan(raster,fan,true));CHECK_FALSE(Access::Fan(raster,fan,false,false));
    using Range=VideoCore::RasterizerInterface::CpuRasterReadRange;
    std::array<u8,16> separate{};
    const std::array<Range,1> physical_alias{{{target.GetColorBufferPhysicalAddress(),separate}}};
    const std::array<Range,1> host_alias{{{Memory::VRAM_PADDR+0x10000,{color,16}}}};
    const std::array<Range,1> independent{{{Memory::VRAM_PADDR+0x10000,separate}}};
    CHECK_FALSE(Access::Begin(raster,physical_alias));CHECK_FALSE(Access::Begin(raster,host_alias));
    CHECK_FALSE(Access::Begin(raster,independent,true));CHECK_FALSE(Access::Begin(raster,independent,false,false));
    for(bool unwind:{false,true}) {
        std::memset(color,0,64*64*4);REQUIRE(Access::Begin(raster,independent));
        raster.AddTriangle(quad[0],quad[1],quad[2]);raster.AddTriangle(quad[0],quad[2],quad[3]);
        CHECK(Access::Group(raster)[4]==2);CHECK(std::count(color,color+64*64*4,255)==0);
        raster.EndCpuRasterBatch(unwind);
        CHECK(Access::Group(raster)[4]==0);CHECK(std::vector<u8>(color,color+64*64*4)==reference);
        for(auto measured:Access::GroupCost(raster)) CHECK(measured>0);
    }
    // Capacity publishes only already-owned work and cannot leave a dormant tail.
    REQUIRE(Access::Begin(raster,independent));
    for(u32 i=0;i<8;++i) raster.AddTriangle(quad[0],quad[1],quad[2]);
    CHECK(Access::Group(raster)[4]==0);
    raster.AddTriangle(quad[0],quad[2],quad[3]);CHECK(Access::Group(raster)[4]==1);
    raster.InvalidateRegion(target.GetColorBufferPhysicalAddress(),16);CHECK(Access::Group(raster)[4]==0);
    // A failed new primitive drains earlier certified pixels before refusal.
    std::memset(color,0,64*64*4);REQUIRE(Access::Begin(raster,independent));
    raster.AddTriangle(quad[0],quad[1],quad[2]);
    auto bad=quad[0];bad.pos.w=Pica::f24::Zero();
    const std::array<const Pica::OutputVertex*,3> invalid{&bad,&quad[1],&quad[2]};
    CHECK_FALSE(Access::Queue(raster,invalid));CHECK(Access::Group(raster)[4]==0);
    CHECK(std::count(color,color+64*64*4,255)>100);
    // Live texture/alpha/depth work and an aliased pitch never enter this route.
    m.alpha_test.enable.Assign(1);CHECK_FALSE(Access::Begin(raster,independent));m.alpha_test.enable.Assign(0);
    regs.texturing.tev_stage5.color_source1.Assign(Stage::Source::Texture0);CHECK_FALSE(Access::Begin(raster,independent));
    regs.texturing.tev_stage5.color_source1.Assign(Stage::Source::Constant);
    target.width.Assign(63);CHECK_FALSE(Access::Begin(raster,independent));target.width.Assign(64);
    // Include and Exclude remain byte-identical under the shared integer bounds.
    for(auto mode:{Pica::RasterizerRegs::ScissorMode::Include,Pica::RasterizerRegs::ScissorMode::Exclude}) {
        regs.rasterizer.scissor_test.mode.Assign(mode);regs.rasterizer.scissor_test.x1.Assign(16);
        regs.rasterizer.scissor_test.y1.Assign(16);regs.rasterizer.scissor_test.x2.Assign(47);regs.rasterizer.scissor_test.y2.Assign(47);
        Settings::software_renderer_features=7;
        { SwRenderer::RasterizerSoftware ordinary{memory,pica};std::memset(color,0,64*64*4);
          ordinary.AddTriangle(quad[0],quad[1],quad[2]);ordinary.AddTriangle(quad[0],quad[2],quad[3]);reference.assign(color,color+64*64*4); }
        Settings::software_renderer_features=15;std::memset(color,0,64*64*4);
        REQUIRE(Access::Begin(raster,independent));raster.AddTriangle(quad[0],quad[1],quad[2]);raster.AddTriangle(quad[0],quad[2],quad[3]);
        raster.EndCpuRasterBatch(false);CHECK(std::vector<u8>(color,color+64*64*4)==reference);
    }
    // CodexAstraLocal: Diagnostic-off preserves the grouped route/output while
    // eliminating optional command/admission counters and clocks.
    Settings::software_renderer_diagnostics=false;
    SwRenderer::RasterizerSoftware unobserved{memory,pica};std::memset(color,0,64*64*4);
    REQUIRE(Access::Begin(unobserved,independent));unobserved.AddTriangle(quad[0],quad[1],quad[2]);
    unobserved.AddTriangle(quad[0],quad[2],quad[3]);unobserved.EndCpuRasterBatch(false);
    CHECK(std::vector<u8>(color,color+64*64*4)==reference);
    for(auto value:Access::GroupCost(unobserved)) CHECK(value==0);
    CHECK(Access::Group(unobserved)[0]==0);
}

TEST_CASE("Software PICA input scope closes before return and exception", "[software]") {
    // CodexAstraLocal: Real PICA loader/JIT/FIFO with a recording sink proves the
    // scope hook, full range certificate and normal/unwind lifetime boundaries.
    struct Sink final:VideoCore::RasterizerInterface {
        bool fail{},open{},unwind{};u32 began{},ended{},triangles{};std::vector<CpuRasterReadRange> reads;
        bool WantsCpuRasterBatch()const override{return true;}
        bool BeginCpuRasterBatch(std::span<const CpuRasterReadRange> input)override {
            ++began;open=true;reads.assign(input.begin(),input.end());return true; }
        void EndCpuRasterBatch(bool error)override{++ended;unwind=error;open=false;}
        void AddTriangle(const Pica::OutputVertex&,const Pica::OutputVertex&,const Pica::OutputVertex&)override {
            ++triangles;if(fail)throw std::runtime_error("fixture publication failure"); }
        void DrawTriangles()override{} void FlushAll()override{}void FlushRegion(PAddr,u32)override{}
        void InvalidateRegion(PAddr,u32)override{}void FlushAndInvalidateRegion(PAddr,u32)override{}void ClearAll(bool)override{}
    };
    const auto old_mode=Settings::values.uberhar_test_mode.GetValue();
    const auto old_diag=Settings::software_renderer_diagnostics.GetValue();
    struct Restore { Settings::UberharTestMode mode;bool diag;~Restore(){Settings::values.uberhar_test_mode=mode;
        Settings::software_renderer_diagnostics=diag;} }restore{old_mode,old_diag};
    Settings::values.uberhar_test_mode=Settings::UberharTestMode::Software;Settings::software_renderer_diagnostics=false;
    for(bool observe:{false,true})for(bool fail:{false,true})for(bool indexed:{false,true}) {
        Settings::software_renderer_diagnostics=observe;
        Core::System system;Memory::MemorySystem memory{system};Sink sink;sink.fail=fail;Pica::PicaCore pica{memory,{}};
        pica.BindRasterizer(&sink);auto& regs=pica.regs.internal;regs={};
        auto& a=regs.pipeline.vertex_attributes;a.base_address.Assign(Memory::VRAM_PADDR/16);
        a.format0.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);a.size0.Assign(3);a.max_attribute_index.Assign(0);
        a.attribute_loaders[0].component_count.Assign(1);a.attribute_loaders[0].byte_count.Assign(16);
        regs.pipeline.num_vertices=3;regs.pipeline.index_array.offset.Assign(0x1000);
        regs.vs.output_mask.Assign(1);regs.vs.main_offset.Assign(1);regs.rasterizer.vs_output_total.Assign(1);
        auto& map=regs.rasterizer.vs_output_attributes[0];
        using Semantic=Pica::RasterizerRegs::VSOutputAttributes;
        map.map_x.Assign(Semantic::POSITION_X);map.map_y.Assign(Semantic::POSITION_Y);
        map.map_z.Assign(Semantic::POSITION_Z);map.map_w.Assign(Semantic::POSITION_W);
        const std::array<float,12> vertices{-.5f,-.5f,-.5f,1,.5f,-.5f,-.5f,1,-.5f,.5f,-.5f,1};
        std::memcpy(memory.GetPhysicalPointer(Memory::VRAM_PADDR),vertices.data(),sizeof(vertices));
        const std::array<u8,3> indices{0,1,2};std::memcpy(memory.GetPhysicalPointer(Memory::VRAM_PADDR+0x1000),indices.data(),3);
        using Op=nihstro::OpCode::Id;using Dest=nihstro::DestRegister;using Source=nihstro::SourceRegister;
        const auto binary=nihstro::InlineAsm::CompileToRawBinary(std::initializer_list<nihstro::InlineAsm>{{Op::NOP},{Op::MOV,Dest::MakeOutput(0),Source::MakeInput(0)},{Op::END}});
        Pica::ProgramCode program{};Pica::SwizzleData swizzles{};
        std::transform(binary.program.begin(),binary.program.end(),program.begin(),[](auto word){return word.hex;});
        std::transform(binary.swizzle_table.begin(),binary.swizzle_table.end(),swizzles.begin(),[](auto word){return word.hex;});
        pica.vs_setup.UpdateProgramCode(program);pica.vs_setup.UpdateSwizzleData(swizzles);
        if(fail) CHECK_THROWS_AS(Pica::SoftwareProducerTestAccess::Batch(pica,indexed),std::runtime_error);
        else CHECK_NOTHROW(Pica::SoftwareProducerTestAccess::Batch(pica,indexed));
        CHECK(sink.began==1);CHECK(sink.ended==1);CHECK(sink.triangles==1);CHECK_FALSE(sink.open);CHECK(sink.unwind==fail);
        const auto trace=Pica::SoftwareProducerTestAccess::TraceCost(pica);
        if(observe) { CHECK(trace[0]==1);CHECK(trace[1]==3);CHECK(trace[2]==48);
            CHECK(trace[3]>0);CHECK(trace[4]>0);CHECK(trace[5]==0); }
        else for(auto value:trace) CHECK(value==0);
        REQUIRE(sink.reads.size()==(indexed?2:1));CHECK(sink.reads[0].address==Memory::VRAM_PADDR);
        CHECK(sink.reads[0].bytes.size()==(indexed?4096:48));
        if(indexed){CHECK(sink.reads[1].address==Memory::VRAM_PADDR+0x1000);CHECK(sink.reads[1].bytes.size()==3);}
    }
}

TEST_CASE("Software prepared fragment demand preserves actual triangle outputs",
          "[software][software-preparation]") {
    // CodexAstraLocal: Exercise the real joined triangle consumer, not a second
    // TEV evaluator. Literal byte expectations expose stage order, arity,
    // quantization, delayed-buffer timing and Software's stage0 alpha rule.
    const auto old_workers = Settings::values.software_renderer_workers.GetValue();
    struct Restore {
        u32 workers;
        ~Restore() { Settings::values.software_renderer_workers = workers; }
    } restore{old_workers};
    Settings::values.software_renderer_workers = 1;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory, {}};
    auto& regs = pica.regs.internal;
    regs = {};
    regs.rasterizer.viewport_size_x.Assign(0x420000); // 8
    regs.rasterizer.viewport_size_y.Assign(0x420000);
    regs.rasterizer.viewport_depth_near_plane.Assign(0x3f0000);
    auto& target = regs.framebuffer.framebuffer;
    target.width.Assign(16); target.height.Assign(15);
    target.color_format.Assign(Pica::FramebufferRegs::ColorFormat::RGBA8);
    target.color_buffer_address.Assign((Memory::VRAM_PADDR + 0x40000) / 8);
    target.allow_color_write.Assign(15);
    auto& merger = regs.framebuffer.output_merger;
    merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    merger.red_enable.Assign(1); merger.green_enable.Assign(1);
    merger.blue_enable.Assign(1); merger.alpha_enable.Assign(1);
    auto* color = memory.GetPhysicalPointer(target.GetColorBufferPhysicalAddress());
    SwRenderer::Framebuffer observed{memory, regs.framebuffer};
    observed.Bind();
    SwRenderer::RasterizerSoftware raster{memory, pica};
    const auto f = [](float value) { return Pica::f24::FromFloat32(value); };
    const auto nan = f(std::numeric_limits<float>::quiet_NaN());
    const auto vertex = [&](float x, float y) {
        Pica::OutputVertex result{};
        result.pos = {f(x), f(y), f(-.5f), f(1)};
        result.color = {nan, nan, nan, nan};
        result.quat = {nan, nan, nan, nan};
        result.view = {nan, nan, nan};
        result.tc0 = result.tc1 = result.tc2 = {nan, nan};
        return result;
    };
    const auto a = vertex(-1, -1), b = vertex(1, -1), c = vertex(-1, 1);
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    const std::array<Stage*, 6> stages{&regs.texturing.tev_stage0, &regs.texturing.tev_stage1,
        &regs.texturing.tev_stage2, &regs.texturing.tev_stage3, &regs.texturing.tev_stage4,
        &regs.texturing.tev_stage5};
    const auto constant = [](Stage& stage, Color value) {
        stage.const_r.Assign(value.r()); stage.const_g.Assign(value.g());
        stage.const_b.Assign(value.b()); stage.const_a.Assign(value.a());
    };
    const auto reset = [&] {
        regs.texturing.tev_combiner_buffer_input.update_mask_rgb.Assign(0);
        regs.texturing.tev_combiner_buffer_input.update_mask_a.Assign(0);
        for (auto* stage : stages) {
            *stage = {};
            stage->color_source1.Assign(Source::Previous);
            stage->alpha_source1.Assign(Source::Previous);
        }
        stages[0]->color_source1.Assign(Source::Constant);
        stages[0]->alpha_source1.Assign(Source::Constant);
        constant(*stages[0], {21, 73, 149, 205});
    };
    const auto draw = [&] {
        std::memset(color, 0x6d, 16 * 16 * 4);
        raster.AddTriangle(a, b, c);
        return observed.GetPixel(2, 2);
    };
    // Invalid enabled bump memory and NaN semantic attributes remain unused
    // only while no executed operand consumes them; there is no repair value.
    regs.lighting.config0.bump_mode.Assign(Pica::LightingRegs::LightingBumpMode::NormalMap);
    regs.lighting.config0.bump_selector.Assign(1);
    regs.texturing.main_config.texture1_enable.Assign(1);
    // CodexAstraLocal: Address zero is the renderer's valid constant-color
    // sampler. Use mapped storage with invalid zero dimensions for this refusal.
    regs.texturing.texture1.address.Assign((Memory::VRAM_PADDR + 0x10000) / 8);
    reset();
    REQUIRE(draw() == Color{21, 73, 149, 205});
    stages[5]->alpha_source1.Assign(Source::PrimaryFragmentColor);
    CHECK_THROWS_AS(draw(), VideoCore::ShaderRecoveryError);
    CHECK(std::all_of(color, color + 16 * 16 * 4, [](u8 value) { return value == 0x6d; }));
    reset();
    stages[0]->color_source1.Assign(Source::Previous);
    stages[0]->color_source3.Assign(Source::Constant);
    stages[0]->alpha_source1.Assign(Source::Previous);
    stages[0]->alpha_source3.Assign(Source::Texture1);
    CHECK(draw() == Color{21, 73, 149, 0});
    reset();
    stages[0]->color_modifier1.Assign(Stage::ColorModifier::OneMinusSourceColor);
    stages[0]->alpha_modifier1.Assign(Stage::AlphaModifier::OneMinusSourceAlpha);
    stages[0]->color_scale.Assign(1); stages[0]->alpha_scale.Assign(1);
    CHECK(draw() == Color{255, 255, 212, 100});
    reset();
    auto& buffer = regs.texturing.tev_combiner_buffer_color;
    buffer.r.Assign(3); buffer.g.Assign(5); buffer.b.Assign(7); buffer.a.Assign(11);
    regs.texturing.tev_combiner_buffer_input.update_mask_rgb.Assign(1);
    regs.texturing.tev_combiner_buffer_input.update_mask_a.Assign(1);
    stages[1]->color_source1.Assign(Source::PreviousBuffer);
    stages[1]->alpha_source1.Assign(Source::PreviousBuffer);
    CHECK(draw() == Color{3, 5, 7, 11});
    stages[2]->color_source1.Assign(Source::PreviousBuffer);
    stages[2]->alpha_source1.Assign(Source::PreviousBuffer);
    CHECK(draw() == Color{21, 73, 149, 205});
    reset();
    const std::array<Stage::Operation, 5> ops{Stage::Operation::Modulate, Stage::Operation::Add,
        Stage::Operation::Lerp, Stage::Operation::MultiplyThenAdd, Stage::Operation::AddThenMultiply};
    const std::array<Color, 5> constants{{{128,64,32,128}, {7,11,13,17}, {31,63,95,127},
                                      {3,5,7,11}, {64,96,128,160}}};
    for (u32 i = 1; i < 6; ++i) {
        stages[i]->color_op.Assign(ops[i - 1]); stages[i]->alpha_op.Assign(ops[i - 1]);
        stages[i]->color_source2.Assign(Source::Constant); stages[i]->color_source3.Assign(Source::Constant);
        stages[i]->alpha_source2.Assign(Source::Constant); stages[i]->alpha_source3.Assign(Source::Constant);
        constant(*stages[i], constants[i - 1]);
    }
    stages[3]->color_modifier3.Assign(Stage::ColorModifier::SourceAlpha);
    CHECK(draw() == Color{16, 38, 68, 110});
    // CodexAstraLocal: Alpha-only Dot3_RGBA needs its RGB inputs, but takes the
    // dot result before color scaling. Neither the dead alpha source nor dead
    // final RGB from that stage may add a texture/lighting dependency.
    reset();
    stages[4]->color_op.Assign(Stage::Operation::Dot3_RGBA);
    stages[4]->color_source1.Assign(Source::Constant);
    stages[4]->color_source2.Assign(Source::Constant);
    stages[4]->alpha_source1.Assign(Source::Texture1);
    stages[4]->color_scale.Assign(1);
    constant(*stages[4], {160, 160, 160, 0});
    stages[5]->color_source1.Assign(Source::Constant);
    constant(*stages[5], {21, 73, 149, 205});
    CHECK(draw() == Color{21, 73, 149, 51});
    // CodexAstraLocal: Complete Copy may omit destination reads; any channel
    // mask, non-Copy logic or blend must still consume the seeded destination.
    reset();
    merger.red_enable.Assign(0);
    CHECK(draw() == Color{109, 73, 149, 205});
    merger.red_enable.Assign(1);
    merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Xor);
    CHECK(draw() == Color{120, 36, 248, 160});
    merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    merger.alphablend_enable.Assign(1);
    merger.alpha_blending.factor_source_rgb.Assign(Pica::FramebufferRegs::BlendFactor::One);
    merger.alpha_blending.factor_source_a.Assign(Pica::FramebufferRegs::BlendFactor::One);
    merger.alpha_blending.factor_dest_rgb.Assign(Pica::FramebufferRegs::BlendFactor::One);
    merger.alpha_blending.factor_dest_a.Assign(Pica::FramebufferRegs::BlendFactor::One);
    CHECK(draw() == Color{130, 182, 255, 255});
    merger.alphablend_enable.Assign(0);
    const std::array<Color, 5> encoded{{{21,73,149,205}, {21,73,149,255}, {16,74,148,255},
                                      {16,73,148,255}, {17,68,153,204}}};
    // Native PICA framebuffer order is RGBA8, RGB8, RGB5A1, RGB565, RGBA4.
    for (u32 format = 0; format < encoded.size(); ++format) {
        target.color_format.Assign(static_cast<Pica::FramebufferRegs::ColorFormat>(format));
        CHECK(draw() == encoded[format]);
    }
}

TEST_CASE("Software backward TEV liveness removes only overwritten texture work",
          "[software][software-liveness]") {
    // CodexAstraLocal: The retained failing triangle has a finite position plane
    // and one NaN UV vertex. Its six captured TEV stages finish with Replace
    // PrimaryColor, so that texture cannot affect RGBA. This executes actual
    // AddTriangle with those position/UV bits and TEV registers, controlled
    // identical primary colors, and local attachments (no title/shader fixture).
    const auto old_workers = Settings::values.software_renderer_workers.GetValue();
    struct Restore { u32 value; ~Restore() { Settings::values.software_renderer_workers = value; } }
        restore{old_workers};
    Settings::values.software_renderer_workers = 1;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory, {}};
    auto& regs = pica.regs.internal;
    regs = {};
    regs.lighting.disable.Assign(1);
    regs.rasterizer.viewport_size_x.Assign(0x45e000);
    regs.rasterizer.viewport_size_y.Assign(0x469000);
    regs.rasterizer.viewport_depth_range.Assign(0xbf0000);
    regs.rasterizer.depthmap_enable.Assign(Pica::RasterizerRegs::DepthBuffering::ZBuffering);
    auto& target = regs.framebuffer.framebuffer;
    target.width.Assign(256); target.height.Assign(255);
    target.color_format.Assign(Pica::FramebufferRegs::ColorFormat::RGBA8);
    target.color_buffer_address.Assign((Memory::VRAM_PADDR + 0x40000) / 8);
    target.allow_color_write.Assign(15);
    auto& merger = regs.framebuffer.output_merger;
    merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    merger.red_enable.Assign(1); merger.green_enable.Assign(1);
    merger.blue_enable.Assign(1); merger.alpha_enable.Assign(1);
    auto* color = memory.GetPhysicalPointer(target.GetColorBufferPhysicalAddress());
    SwRenderer::Framebuffer observed{memory, regs.framebuffer};
    observed.Bind();
    SwRenderer::RasterizerSoftware raster{memory, pica};
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    const std::array<Stage*, 6> stages{&regs.texturing.tev_stage0, &regs.texturing.tev_stage1,
        &regs.texturing.tev_stage2, &regs.texturing.tev_stage3, &regs.texturing.tev_stage4,
        &regs.texturing.tev_stage5};
    const std::array<std::array<u32, 5>, 6> captured{{
        {0x0e300e30, 0, 0, 0x80000000, 0},
        {0x0eef0eef, 0, 0, 0xffffffff, 0},
        {0x0ee30ee3, 0, 0x00030001, 0x00eff4f6, 0x00010000},
        {0x0fff0fff, 0x20, 1, 0xffffffff, 0},
        {0x0e300f3f, 0x200, 0x00010004, 0xffffffff, 0x00010000},
        {0x0f300ff0, 0, 0, 0, 0}}};
    const auto reset = [&] {
        for (u32 i = 0; i < stages.size(); ++i) {
            stages[i]->sources_raw = captured[i][0];
            stages[i]->modifiers_raw = captured[i][1];
            stages[i]->ops_raw = captured[i][2];
            stages[i]->const_color = captured[i][3];
            stages[i]->scales_raw = captured[i][4];
        }
        regs.texturing.tev_combiner_buffer_input.update_mask_rgb.Assign(0);
        regs.texturing.tev_combiner_buffer_input.update_mask_a.Assign(0);
    };
    const auto bits = [](u32 value) { return std::bit_cast<Pica::f24>(value); };
    std::array<Pica::OutputVertex, 3> triangle{};
    const std::array<std::array<u32, 4>, 3> position{{
        {0x402b15b5, 0xc02ff908, 0xc12d0f2e, 0x41746806},
        {0x400a7f5b, 0xbeeaa180, 0xc12d0f34, 0x4174680b},
        {0x400a7f5b, 0xc02ff908, 0xc12d0f34, 0x4174680b}}};
    for (u32 i = 0; i < triangle.size(); ++i) {
        auto& v = triangle[i];
        for (u32 lane = 0; lane < 4; ++lane) v.pos[lane] = bits(position[i][lane]);
        v.color = {bits(0), bits(0), bits(0), bits(0x3f3fbfc0)};
        v.quat.w = Pica::f24::One();
        v.tc0 = v.tc1 = v.tc2 = i == 2
            ? Common::Vec2{bits(0x7fc00000), bits(0x7fc00000)}
            : Common::Vec2{Pica::f24::Zero(), Pica::f24::One()};
        v.tc0_w = Pica::f24::One();
    }
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0 = Sampler(128);
    regs.texturing.texture0.height.Assign(512);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR + 0x10000) / 8);
    regs.texturing.texture0_format.Assign(Format::RGBA4);
    const auto draw = [&] {
        std::memset(color, 0x6d, 256 * 256 * 4);
        raster.AddTriangle(triangle[0], triangle[1], triangle[2]);
    };
    reset();
    auto live = Pica::TevLiveness::Analyze(regs.texturing, Pica::TevLiveness::Stage0Policy::Software);
    CHECK(live.external_sources == (1U << static_cast<u32>(Source::PrimaryColor)));
    for (u32 i = 0; i < 5; ++i) CHECK_FALSE((live.stages[i].rgb || live.stages[i].alpha));
    REQUIRE_NOTHROW(draw());
    CHECK(observed.GetPixel(137, 176) == Color{0, 0, 0, 191});
    CHECK(observed.GetPixel(0, 0) == Color{109, 109, 109, 109});

    // CodexAstraLocal: A final alpha-only texture, a color operand reading
    // source alpha, and a live delayed buffer write must restore the real NaN
    // refusal. This proves the repair does not mask or replace bad coordinates.
    stages[5]->alpha_source1.Assign(Source::Texture0);
    CHECK_THROWS_AS(draw(), VideoCore::ShaderRecoveryError);
    reset();
    stages[5]->color_source1.Assign(Source::Texture0);
    stages[5]->color_modifier1.Assign(Stage::ColorModifier::SourceAlpha);
    CHECK_THROWS_AS(draw(), VideoCore::ShaderRecoveryError);
    reset();
    stages[0]->color_source1.Assign(Source::Texture0);
    stages[5]->color_source1.Assign(Source::PreviousBuffer);
    regs.texturing.tev_combiner_buffer_input.update_mask_rgb.Assign(1);
    CHECK_THROWS_AS(draw(), VideoCore::ShaderRecoveryError);
    regs.texturing.tev_combiner_buffer_input.update_mask_rgb.Assign(0);
    REQUIRE_NOTHROW(draw()); // Initial buffer constant is independent of UVs.
    CHECK(observed.GetPixel(137, 176) == Color{0, 0, 0, 191});
    // Live sampling with finite UVs still returns the actual texture bytes.
    reset();
    stages[5]->color_source1.Assign(Source::Texture0);
    for (auto& v : triangle) v.tc0 = {Pica::f24::Zero(), Pica::f24::Zero()};
    const auto layout = SwRenderer::MakeTextureLayout(regs.texturing.texture0, Format::RGBA4, false);
    std::span<u8> texels{memory.GetPhysicalPointer(regs.texturing.texture0.GetPhysicalAddress()), layout.bytes};
    std::fill(texels.begin(), texels.end(), 0xff);
    REQUIRE_NOTHROW(draw());
    CHECK(observed.GetPixel(137, 176) == Color{255, 255, 255, 191});
}

TEST_CASE("Software repeated lighting vectors preserve distinct light LUT results",
          "[software][software-preparation]") {
    // CodexAstraLocal: Two distinct half-vectors repeatedly feed NH/VH/CP.
    // Sparse LUT entries make stale cross-light reuse or a different LUT input
    // change literal output bytes without introducing another lighting engine.
    using L = Pica::LightingRegs;
    L lighting{};
    Pica::PicaCore::Lighting state{};
    lighting.config0.config.Assign(L::LightingConfig::Config7);
    lighting.config0.enable_primary_alpha.Assign(1);
    lighting.config0.enable_secondary_alpha.Assign(1);
    lighting.config1.disable_spot_atten.Assign(255);
    lighting.config1.disable_dist_atten.Assign(255);
    lighting.max_light_index.Assign(1);
    lighting.light_enable.slot_1.Assign(1);
    lighting.light[0].z.Assign(0x3c00); lighting.light[1].x.Assign(0x3c00);
    for (auto& light : lighting.light) {
        light.config.directional.Assign(1);
        light.diffuse.r.Assign(10); light.diffuse.g.Assign(20); light.diffuse.b.Assign(30);
        light.specular_0.r.Assign(32); light.specular_0.g.Assign(64); light.specular_0.b.Assign(96);
        light.specular_1.r.Assign(64); light.specular_1.g.Assign(32); light.specular_1.b.Assign(16);
    }
    lighting.lut_input.d0.Assign(L::LightingLutInput::NH);
    lighting.lut_input.d1.Assign(L::LightingLutInput::VH);
    lighting.lut_input.rr.Assign(L::LightingLutInput::CP);
    lighting.lut_input.rg.Assign(L::LightingLutInput::NH);
    lighting.lut_input.rb.Assign(L::LightingLutInput::VH);
    lighting.lut_input.fr.Assign(L::LightingLutInput::VH);
    const auto lut = [&](L::LightingSampler sampler, u32 end, u32 diagonal) {
        auto& entries = state.luts[static_cast<u32>(sampler)];
        entries[255].value.Assign(end); entries[181].value.Assign(diagonal);
    };
    lut(L::LightingSampler::Distribution0, 4095, 2048);
    lut(L::LightingSampler::Distribution1, 2048, 4095);
    lut(L::LightingSampler::ReflectRed, 4095, 4095);
    lut(L::LightingSampler::ReflectGreen, 1024, 3072);
    lut(L::LightingSampler::ReflectBlue, 4095, 2048);
    lut(L::LightingSampler::Fresnel, 0, 3072);
    const std::array<Color, 4> textures{};
    const auto result = SwRenderer::ComputeFragmentsColors(lighting, state,
        Common::Quaternion<float>{{0,0,0},1}, {0,0,2}, textures);
    CHECK(result.first == Color{10, 20, 30, 191});
    CHECK(result.second == Color{112, 124, 160, 191});
}

TEST_CASE("Software unbiased interpolation preserves coverage and singular failures",
          "[software][software-interpolation]") {
    // CodexAstraLocal: Use the retained Dark Moon 12.4 positions through the
    // real clip/raster/TEV/depth path. Its original attributes were not logged;
    // distinct finite fixture attributes expose the zero-sum error directly.
    const auto old_workers = Settings::values.software_renderer_workers.GetValue();
    const auto old_sampling = Settings::values.texture_sampling.GetValue();
    struct Restore {
        u32 workers;
        Settings::TextureSampling sampling;
        ~Restore() {
            Settings::values.software_renderer_workers = workers;
            Settings::values.texture_sampling = sampling;
        }
    } restore{old_workers, old_sampling};
    Settings::values.software_renderer_workers = 1;
    Settings::values.texture_sampling = Game;
    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PicaCore pica{memory, {}};
    auto& regs = pica.regs.internal;
    regs = {};
    regs.lighting.disable.Assign(1);
    regs.rasterizer.viewport_size_x.Assign(0x470000); // 256
    regs.rasterizer.viewport_size_y.Assign(0x470000);
    regs.rasterizer.viewport_depth_range.Assign(0x3f0000); // 1
    regs.rasterizer.viewport_depth_near_plane.Assign(0x3f0000);
    regs.rasterizer.depthmap_enable.Assign(Pica::RasterizerRegs::DepthBuffering::ZBuffering);
    auto& target = regs.framebuffer.framebuffer;
    target.width.Assign(512); target.height.Assign(255);
    target.color_format.Assign(Pica::FramebufferRegs::ColorFormat::RGBA8);
    target.color_buffer_address.Assign((Memory::VRAM_PADDR + 0x100000) / 8);
    target.depth_buffer_address.Assign((Memory::VRAM_PADDR + 0x200000) / 8);
    target.depth_format.Assign(Pica::FramebufferRegs::DepthFormat::D16);
    target.allow_color_write.Assign(15); target.allow_depth_stencil_write.Assign(1);
    auto& merger = regs.framebuffer.output_merger;
    merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    merger.red_enable.Assign(1); merger.green_enable.Assign(1);
    merger.blue_enable.Assign(1); merger.alpha_enable.Assign(1);
    merger.depth_write_enable.Assign(1);
    const auto f = [](float value) { return Pica::f24::FromFloat32(value); };
    const auto vertex = [&](u16 x, u16 y, float w, float z) {
        Pica::OutputVertex result{};
        result.pos = {f((x / 4096.f - 1.f) * w), f((y / 4096.f - 1.f) * w),
                      f(z * w), f(w)};
        result.color = {f(.25f), f(.5f), f(.75f), f(1)};
        result.quat.w = f(1);
        result.tc0 = {f(.25f), f(.125f)};
        result.tc0_w = f(.5f);
        return result;
    };
    auto a = vertex(3785, 2181, 1, -.25f);
    auto b = vertex(3784, 2184, 2, -.75f);
    auto c = vertex(3817, 2084, 4, -.5f);
    a.color = {f(1), f(0), f(0), f(1)};
    c.color = {f(0), f(0), f(1), f(1)};
    a.tc0 = {f(.75f), f(.5f)}; a.tc0_w = f(.75f);
    c.tc0 = {f(.125f), f(.25f)}; c.tc0_w = f(.25f);
    auto* color = memory.GetPhysicalPointer(target.GetColorBufferPhysicalAddress());
    auto* depth = memory.GetPhysicalPointer(target.GetDepthBufferPhysicalAddress());
    constexpr std::size_t color_size = 512 * 256 * 4, depth_size = 512 * 256 * 2;
    const auto clear = [&] {
        std::memset(color, 0, color_size);
        std::memset(depth, 0xa5, depth_size);
    };
    SwRenderer::Framebuffer observed{memory, regs.framebuffer};
    observed.Bind();
    SwRenderer::RasterizerSoftware raster{memory, pica};
    clear();
    REQUIRE_NOTHROW(raster.AddTriangle(a, b, c));
    CHECK(observed.GetPixel(236, 136) == Color{64, 128, 191, 255});
    CHECK(observed.GetDepth(236, 136) == 16383);
    CHECK(std::count(color, color + color_size, u8{255}) == 1);
    regs.rasterizer.depthmap_enable.Assign(Pica::RasterizerRegs::DepthBuffering::WBuffering);
    clear();
    REQUIRE_NOTHROW(raster.AddTriangle(a, b, c));
    CHECK(observed.GetDepth(236, 136) == 32767);
    regs.rasterizer.depthmap_enable.Assign(Pica::RasterizerRegs::DepthBuffering::ZBuffering);

    using Source = Pica::TexturingRegs::TevStageConfig::Source;
    const auto source = [&](Source selected) {
        for (auto* stage : {&regs.texturing.tev_stage0, &regs.texturing.tev_stage1,
                           &regs.texturing.tev_stage2, &regs.texturing.tev_stage3,
                           &regs.texturing.tev_stage4, &regs.texturing.tev_stage5}) {
            stage->color_source1.Assign(selected);
            stage->alpha_source1.Assign(selected);
        }
    };
    source(Source::Texture0);
    regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0 = Sampler(8);
    regs.texturing.texture0.type.Assign(Config::Shadow2D);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR + 0x10000) / 8);
    regs.texturing.texture0_format.Assign(Format::RGBA8);
    auto layout = SwRenderer::MakeTextureLayout(regs.texturing.texture0, Format::RGBA8, true);
    auto* texture = memory.GetPhysicalPointer(regs.texturing.texture0.GetPhysicalAddress());
    for (u32 y = 0; y < 8; ++y) for (u32 x = 0; x < 8; ++x)
        Put({texture, layout.bytes}, layout, 0, x, y, {static_cast<u8>(10 * y + x), 255, 255, 255});
    clear();
    REQUIRE_NOTHROW(raster.AddTriangle(a, b, c));
    CHECK(observed.GetPixel(236, 136) == Color{19, 19, 19, 19});

    // CodexAstraLocal: Dynamic LOD also needs unbiased uncovered helpers.
    // This affine UV plane has exactly two texels per pixel: select mip 1.
    regs.texturing.texture0 = Sampler(32, 2);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR + 0x10000) / 8);
    layout = SwRenderer::MakeTextureLayout(regs.texturing.texture0, Format::RGBA8, false);
    const std::array<Color, 3> mip_colors{{{255, 0, 0, 255}, {31, 62, 93, 255}, {0, 0, 255, 255}}};
    for (u32 level = 0; level < 3; ++level)
        for (u32 y = 0; y < layout.levels[level].height; ++y)
            for (u32 x = 0; x < layout.levels[level].width; ++x)
                Put({texture, layout.bytes}, layout, level, x, y, mip_colors[level]);
    auto affine = [&](u16 x, u16 y) {
        auto result = vertex(x, y, 1, -.5f);
        result.tc0 = {f((x / 16.f - 236.f) / 16.f), f((y / 16.f - 136.f) / 16.f)};
        return result;
    };
    clear();
    REQUIRE_NOTHROW(raster.AddTriangle(affine(3785, 2181), affine(3784, 2184), affine(3817, 2084)));
    CHECK(observed.GetPixel(236, 136) == mip_colors[1]);

    // CodexAstraLocal: A shared diagonal must have neither a hole nor two
    // consumers. Distinct colors make double coverage depend on draw order.
    regs.texturing.main_config.texture0_enable.Assign(0);
    source(Source::PrimaryColor);
    const auto p = vertex(512, 512, 1, -.5f), q = vertex(576, 512, 1, -.5f);
    const auto r = vertex(512, 576, 1, -.5f), s = vertex(576, 576, 1, -.5f);
    auto draw = [&](Pica::OutputVertex x, Pica::OutputVertex y, Pica::OutputVertex z, float red) {
        x.color = y.color = z.color = {f(red), f(0), f(0), f(1)};
        raster.AddTriangle(x, y, z);
    };
    clear(); draw(p, q, r, .25f); draw(q, s, r, .5f);
    const std::vector<u8> ordered(color, color + color_size);
    CHECK(std::count(ordered.begin(), ordered.end(), u8{255}) == 16);
    clear(); draw(q, s, r, .5f); draw(p, q, r, .25f);
    CHECK(std::equal(ordered.begin(), ordered.end(), color));
    // Degenerate diagonal center hits must preserve both attachment sentinels.
    std::memset(color, 0x5a, color_size); std::memset(depth, 0xa5, depth_size);
    REQUIRE_NOTHROW(raster.AddTriangle(p, vertex(544, 544, 1, -.5f), s));
    CHECK(std::all_of(color, color + color_size, [](u8 value) { return value == 0x5a; }));
    CHECK(std::all_of(depth, depth + depth_size, [](u8 value) { return value == 0xa5; }));

    // CodexAstraLocal: FEA retained a nondegenerate triangle but no source UVs.
    // Its rounded shape works with finite fixture attributes; an injected NaN
    // must still fail rather than being repaired to a plausible texture value.
    source(Source::Texture0); regs.texturing.main_config.texture0_enable.Assign(1);
    regs.texturing.texture0 = Sampler(8);
    regs.texturing.texture0.address.Assign((Memory::VRAM_PADDR + 0x10000) / 8);
    layout = SwRenderer::MakeTextureLayout(regs.texturing.texture0, Format::RGBA8, false);
    for (u32 y = 0; y < 8; ++y) for (u32 x = 0; x < 8; ++x)
        Put({texture, layout.bytes}, layout, 0, x, y, {42, 84, 126, 255});
    regs.rasterizer.scissor_test.mode.Assign(Pica::RasterizerRegs::ScissorMode::Include);
    regs.rasterizer.scissor_test.x1.Assign(137); regs.rasterizer.scissor_test.x2.Assign(137);
    regs.rasterizer.scissor_test.y1.Assign(184); regs.rasterizer.scissor_test.y2.Assign(184);
    auto fa = vertex(2256, 2624, 1, -.70808023f);
    auto fb = vertex(2192, 3104, 1, -.70808035f);
    auto fc = vertex(2192, 2624, 1, -.70808035f);
    clear();
    REQUIRE_NOTHROW(raster.AddTriangle(fa, fb, fc));
    CHECK(observed.GetPixel(137, 184) == Color{42, 84, 126, 255});
    fa.tc0.x = f(std::numeric_limits<float>::quiet_NaN());
    clear();
    CHECK_THROWS_AS(raster.AddTriangle(fa, fb, fc), VideoCore::ShaderRecoveryError);
    CHECK(std::all_of(color, color + color_size, [](u8 value) { return value == 0; }));
    CHECK(std::all_of(depth, depth + depth_size, [](u8 value) { return value == 0xa5; }));
}
#endif

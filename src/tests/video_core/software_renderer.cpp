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
#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
#include <sstream> // CodexAstraLocal: Round-trip the actual first-candidate archive, not a substitute.
#include <nihstro/inline_assembly.h>
#include "common/archives.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
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
            TextureDemandMask(regs.texturing.GetTevStages(), regs.lighting));
    }
    static unsigned Workers(const RasterizerSoftware& raster) { return raster.sw_workers.CreatedWorkers(); }
    static unsigned Available(const RasterizerSoftware& raster) { return raster.sw_workers.Available(); }
    // CodexAstraLocal: Use the real private mapper and sample consumer with
    // deliberately undefined unused helpers; no duplicated filtering oracle.
    static auto Sample(RasterizerSoftware& raster, const Pica::RegsInternal& regs, float u,
                       float v, float w, bool footprint) {
        std::array<RasterizerSoftware::PreparedTexture, 3> prepared;
        const auto demand = TextureDemandMask(regs.texturing.GetTevStages(), regs.lighting);
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
                         const OutputVertex& output, bool fresh, bool post = true) {
        const auto mask = Mask(pica);
        return pica.MakeSoftwareProducerSnapshot(mask, Invalid(mask, output), output,
            setup, unit, fresh ? 0 : 1, 17, "fixture_cpu", fresh, post, 0x12345678ULL);
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

TEST_CASE("Software texture demand follows consumed TEV and lighting operands", "[software][software-sampling]") {
    // CodexAstraLocal: Reserved/unused operand fields cannot map guest texture
    // bytes. Keep Software's existing stage0 alpha Previous behavior explicit.
    using Stage = Pica::TexturingRegs::TevStageConfig;
    using Source = Stage::Source;
    std::array<Stage, 6> stages{};
    Pica::LightingRegs lighting{}; lighting.disable.Assign(1);
    const auto mask = [&] { return SwRenderer::TextureDemandMask(stages, lighting); };
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
    CHECK(SwRenderer::GetFragmentDemand(stages, lighting).primary_color);
    CHECK_FALSE(SwRenderer::GetFragmentDemand(stages, lighting).lighting);
    stages[5].alpha_source1.Assign(Source::SecondaryFragmentColor);
    CHECK(mask() == 8);
    CHECK(SwRenderer::GetFragmentDemand(stages, lighting).lighting);
    lighting.config0.bump_mode.Assign(Pica::LightingRegs::LightingBumpMode::NormalMap);
    lighting.config0.bump_selector.Assign(1); CHECK(mask() == 10);
    lighting.disable.Assign(1); CHECK(mask() == 0);
    stages[5].color_source1.Assign(Source::Texture2); CHECK(mask() == 4);
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
    const auto snapshot = Access::Snapshot(pica, setup, unit, output, false);
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
        std::vector<u8> output(color,color+64*64*4);
        REQUIRE(std::count(output.begin(),output.end(),255)>100);
        if (reference.empty()) reference=output; else CHECK(output==reference);
    }
    Settings::values.software_renderer_workers=2;
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

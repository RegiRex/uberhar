// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#include <algorithm>
#include <cstring>
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
#include "core/core.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_software/sw_rasterizer.h"
#endif
#include "video_core/renderer_software/sw_frame.h"
#include "video_core/renderer_software/sw_sampler.h"
#include "video_core/shader_recovery_error.h"
#include "video_core/utils.h"

#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
namespace SwRenderer {
// CodexAstraLocal: Inspect real private admission and created participants;
// no alternative raster loop or copied filtering implementation is the oracle.
struct SoftwareRendererTestAccess {
    static bool Safe(RasterizerSoftware& raster, const Pica::RegsInternal& regs) {
        std::array<RasterizerSoftware::PreparedTexture, 3> prepared;
        return raster.PrepareTextures(regs.texturing.GetTextures(), prepared);
    }
    static unsigned Workers(const RasterizerSoftware& raster) { return raster.sw_workers.CreatedWorkers(); }
    static unsigned Available(const RasterizerSoftware& raster) { return raster.sw_workers.Available(); }
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
    }
    fb.address_left1 = 8; fb.address_left2 = 16; fb.address_right1 = 24; fb.address_right2 = 32;
    CHECK(SwRenderer::ScreenAddress(fb, false) == 8); CHECK(SwRenderer::ScreenAddress(fb, true) == 24);
    fb.active_fb = 1;
    CHECK(SwRenderer::ScreenAddress(fb, false) == 16); CHECK(SwRenderer::ScreenAddress(fb, true) == 32);
    fb.address_right1 = 0;
    CHECK(SwRenderer::ScreenAddress(fb, true) == 16);
    fill.is_enabled.Assign(1); fill.color_r.Assign(17); fill.color_g.Assign(34); fill.color_b.Assign(51);
    CHECK(SwRenderer::CaptureScreen(fb, fill, {}).pixels == std::vector<u8>{17,34,51,255});
}

TEST_CASE("Software ordinary mip filters and signed border taps", "[software]") {
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
}

TEST_CASE("Software shadow PCF compares depth before filtering", "[software]") {
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

#ifndef UBERHAR_SOFTWARE_HELPER_PROBE
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
#endif

// CodexAstraLocal: Synthetic original-vertex proof uses original CPU vertices,
// actual conversion/layout, production packet admission and generated shaders.
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <json.hpp>
#include "common/hash.h"
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view f, const fmt::format_args& a) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(f, a));
}
}
#define LAYOUT_HASH static_cast<u64>(sizeof(T)), static_cast<u64>(alignof(T))
#define FIELD_HASH(x) static_cast<u64>(offsetof(T, x)), static_cast<u64>(sizeof(x))
namespace VideoCore {
using Pica::f24;
constexpr u32 MAX_VERTEX_BINDINGS=13, MAX_VERTEX_ATTRIBUTES=16;
#include "layout-types.inc"
struct RasterizerAccelerated {
#include "hardware-fields.inc"
    void AddTriangle(const Pica::OutputVertex&,const Pica::OutputVertex&,const Pica::OutputVertex&);
    void MakeSoftwareVertexLayout();
    VertexLayout software_layout{};
    std::vector<HardwareVertex> vertex_batch;
};
#include "hardware-bodies.inc"
#include "layout-body.inc"
}
using J=nlohmann::json;
using HV=VideoCore::RasterizerAccelerated::HardwareVertex;
using Pica::f24;
void Check(bool v,const char* msg) { if(!v)throw std::runtime_error(msg); }
void Binary(const std::filesystem::path& p,const void* data,size_t n) {
    std::ofstream s(p,std::ios::binary);s.write(static_cast<const char*>(data),n);
    Check(s.good(),"write binary fixture");
}
template<class T> void Binary(const std::filesystem::path& p,const T& value) {
    Binary(p,&value,sizeof(value));
}
static_assert(sizeof(HV)==88 && offsetof(HV,position)==0 && offsetof(HV,color)==16);
static_assert(sizeof(Vulkan::ComputeRectPacket)==32);

// CodexAstraLocal: Power-of-two viewport extents make these baseline coordinates
// exact binary32 values; the graphics reference still consumes original bytes.
Pica::OutputVertex Vertex(int x,int y,const std::array<int,4>& viewport,bool flip,
                          const std::array<unsigned,4>& rgba) {
    Pica::OutputVertex v{};
    const float sx=float(x-viewport[0])/(float(viewport[2])*0.5f)-1.f;
    const float sy=float(y-viewport[1])/(float(viewport[3])*0.5f)-1.f;
    v.pos={f24::FromFloat32(sx),f24::FromFloat32(flip?-sy:sy),
           f24::FromFloat32(-.5f),f24::FromFloat32(1.f)};
    for(unsigned i=0;i<4;++i)v.color[i]=f24::FromFloat32(float(rgba[i])/255.f);
    v.quat.w=f24::FromFloat32(1.f);
    v.tc0_w=f24::FromFloat32(1.f);
    v.view.z=f24::FromFloat32(1.f);
    return v;
}

#include <limits>

// CodexAstraLocal: Exhaust the finite supported grid and important unsafe values
// before creating pixels; these checks exercise the production grid predicate.
unsigned CheckGrid() {
    unsigned checks = 0;
    for (int q = -65536; q <= 65536; ++q) {
        const auto got = Vulkan::ComputeRectSupport::Grid(float(q) / 65536.f);
        Check(got && *got == q, "exact signed grid decoding");
        ++checks;
    }
    for (float bad : {-0.f, std::numeric_limits<float>::denorm_min(),
                      std::numeric_limits<float>::min(), std::nextafter(1.f, 2.f),
                      std::nextafter(0.5f, 1.f), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
        Check(!Vulkan::ComputeRectSupport::Grid(bad), "unsupported grid value rejected");
        ++checks;
    }
    return checks;
}

int main(int argc, char** argv) {
    Check(argc == 2, "expected output path");
    const std::filesystem::path out{argv[1]};
    std::filesystem::create_directories(out);
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator;
    using T = Pica::TexturingRegs::TevStageConfig;
    using FB = Pica::FramebufferRegs;
    using F = FB::BlendFactor;
    Profile profile{};
    profile.is_vulkan = true;
    profile.has_separable_shaders = true;
    profile.has_logic_op = true;
    profile.has_custom_border_color = false;
    UserConfig user{};
    J report{{"author", "CodexAstraLocal"}, {"target", {67, 43}},
             {"grid_checks", CheckGrid()}, {"production_mutated", false}};
    VideoCore::RasterizerAccelerated layout;
    layout.MakeSoftwareVertexLayout();
    report["stride"] = layout.software_layout.bindings[0].byte_count.Value();
    for (unsigned i = 0; i < 8; ++i) {
        const auto& a = layout.software_layout.attributes[i];
        report["attributes"].push_back({{"location", a.location.Value()},
            {"size", a.size.Value()}, {"offset", a.offset.Value()},
            {"type", unsigned(a.type.Value())}});
    }
    for (bool clip : {false, true})
        std::ofstream(out / (clip ? "trivial-clip.vert" : "trivial.vert"))
            << "#version 450\n#define VULKAN 1\n" << GLSL::GenerateTrivialVertexShader(clip, true);

    // CodexAstraLocal: All fifteen nonzero masks cross six finite replacement
    // proofs. Unsupported controls retain the original state/vertices for review.
    unsigned old_accepts = 0, new_accepts = 0, rejected = 0;
    for (unsigned id = 0; id < 114; ++id) {
        const unsigned mode = id < 90 ? id / 15 : 2;
        unsigned channels = id < 90 ? id % 15 + 1 : 7;
        const bool flip = (id / 2) % 2, diagonal = id % 2, reverse = (id / 4) % 2;
        const unsigned geometry = (id / 8) % 3;
        Pica::RegsInternal regs{};
        auto& fb = regs.framebuffer.framebuffer;
        auto& om = regs.framebuffer.output_merger;
        fb.allow_color_write.Assign(1);
        fb.flip.Assign(!flip);
        om.depth_color_mask = channels << 8;
        om.logic_op.Assign(FB::LogicOp::Copy);
        om.alphablend_enable.Assign(mode != 0);
        auto& blend = om.alpha_blending;
        blend.factor_source_rgb.Assign(F::One);
        blend.factor_source_a.Assign(F::One);
        blend.factor_dest_rgb.Assign(F::Zero);
        blend.factor_dest_a.Assign(F::Zero);
        regs.lighting.disable.Assign(1);
        unsigned alpha = mode == 2 ? 255 : mode == 3 ? 0 : mode == 4 ? 43 : mode == 5 ? 209 : 73;
        if (mode >= 2) {
            const bool one = mode == 2 || mode == 4;
            blend.factor_source_rgb.Assign(one ? F::SourceAlpha : F::OneMinusSourceAlpha);
            blend.factor_source_a.Assign(one ? F::SourceAlpha : F::OneMinusSourceAlpha);
            blend.factor_dest_rgb.Assign(one ? F::OneMinusSourceAlpha : F::SourceAlpha);
            blend.factor_dest_a.Assign(one ? F::OneMinusSourceAlpha : F::SourceAlpha);
        }
        const std::array<unsigned, 4> rgba{17 + (id * 23) % 220, 37 + (id * 11) % 190,
                                         71 + (id * 7) % 170, alpha};
        auto stage = T{};
        stage.color_source1.Assign(T::Source::PrimaryColor);
        stage.alpha_source1.Assign(T::Source::PrimaryColor);
        stage.const_color = rgba[0] | rgba[1] << 8 | rgba[2] << 16 | alpha << 24;
        regs.texturing.tev_stage0 = stage;
        stage.color_source1.Assign(T::Source::Previous);
        stage.alpha_source1.Assign(T::Source::Previous);
        regs.texturing.tev_stage1 = regs.texturing.tev_stage2 = regs.texturing.tev_stage3 =
            regs.texturing.tev_stage4 = regs.texturing.tev_stage5 = stage;
        if (mode == 4 || mode == 5) {
            // CodexAstraLocal: A later supported constant replaces an unknown
            // earlier primary alpha; later Previous stages preserve that endpoint.
            auto& override_stage = mode == 4 ? regs.texturing.tev_stage2 : regs.texturing.tev_stage4;
            override_stage.alpha_source1.Assign(T::Source::Constant);
            override_stage.const_color = (override_stage.const_color & 0xffffff) |
                                         (mode == 4 ? 0xff000000 : 0);
        }
        std::array<int, 4> viewport{0, 0, 64, 32}, drawrect{0, 0, 64, 32};
        std::array<int, 4> rect{8, 4, 48, 24};
        if (geometry == 1) { viewport = {3, 5, 64, 32}; drawrect = {3, 5, 67, 37}; rect = {10, 9, 17, 13}; }
        if (geometry == 2) drawrect = {15, 9, 43, 23};
        const int x = rect[0], y = rect[1], r = x + rect[2], t = y + rect[3];
        const std::array<Pica::OutputVertex, 4> corners{
            Vertex(x, y, viewport, flip, rgba), Vertex(r, y, viewport, flip, rgba),
            Vertex(x, t, viewport, flip, rgba), Vertex(r, t, viewport, flip, rgba)};
        std::array<unsigned, 6> indices = diagonal ? std::array<unsigned, 6>{0, 1, 2, 1, 3, 2}
                                                  : std::array<unsigned, 6>{0, 1, 3, 0, 3, 2};
        if (reverse) { std::swap(indices[0], indices[1]); std::swap(indices[3], indices[4]); }
        VideoCore::RasterizerAccelerated cpu;
        for (unsigned i = 0; i < 6; i += 3)
            cpu.AddTriangle(corners[indices[i]], corners[indices[i + 1]], corners[indices[i + 2]]);
        const auto set_alpha = [&](float value) { for (auto& v : cpu.vertex_batch) v.color[3] = value; };
        std::string control;
        switch (id) {
        case 90: control = "zero_color_mask"; om.depth_color_mask = 0; break;
        case 91: control = "disabled_color_gate"; fb.allow_color_write.Assign(0); break;
        case 92: control = "fractional_source_alpha"; set_alpha(128.f / 255.f); break;
        case 93: control = "additive_destination_one"; blend.factor_dest_rgb.Assign(F::One); break;
        case 94: control = "nonreplacement_alpha_equation"; om.depth_color_mask = 0xf00; blend.factor_source_a.Assign(F::Zero); break;
        case 95: control = "destination_dependent_factor"; blend.factor_source_rgb.Assign(F::DestAlpha); break;
        case 96: control = "logic_noop"; om.alphablend_enable.Assign(0); om.logic_op.Assign(FB::LogicOp::NoOp); break;
        case 97: control = "raw_depth_write"; om.depth_write_enable.Assign(1); break;
        case 98: control = "guest_scissor"; regs.rasterizer.scissor_test.mode.Assign(Pica::RasterizerRegs::ScissorMode::Include); break;
        case 99: control = "guest_cull"; regs.rasterizer.cull_mode.Assign(Pica::RasterizerRegs::CullMode::KeepClockWise); break;
        case 100: control = "fog"; regs.texturing.fog_mode.Assign(Pica::TexturingRegs::FogMode::Fog); break;
        case 101: control = "alpha_test"; om.alpha_test.enable.Assign(1); break;
        case 102: control = "nonconstant_color"; cpu.vertex_batch[0].color[0] = 0.25f; break;
        case 103: control = "texture_tev"; regs.texturing.tev_stage0.color_source1.Assign(T::Source::Texture0); break;
        case 104: control = "conservative_near_one"; set_alpha(std::nextafter(1.f, 0.f)); break;
        case 105: control = "conservative_near_zero"; set_alpha(0.00001f); blend.factor_source_rgb.Assign(F::OneMinusSourceAlpha); blend.factor_source_a.Assign(F::OneMinusSourceAlpha); blend.factor_dest_rgb.Assign(F::SourceAlpha); blend.factor_dest_a.Assign(F::SourceAlpha); break;
        case 106: control = "nonintegral_projection"; for (auto& v : cpu.vertex_batch) v.position.x += 1.f / 65536.f; break;
        case 107: control = "old_tolerance_shared_endpoint"; cpu.vertex_batch[0].position.y += 1.f / 1048576.f; break;
        case 108: control = "nonunit_w"; for (auto& v : cpu.vertex_batch) { v.position.x *= 2; v.position.y *= 2; v.position.z *= 2; v.position.w = 2; } break;
        case 109: control = "different_shared_raw_z"; cpu.vertex_batch[0].position.z = -.25f; break;
        case 110: control = "unbounded_viewport"; viewport[2] = 65537; break;
        case 111: control = "empty_drawrect"; drawrect = {0, 0, 0, 0}; break;
        case 112: control = "endpoint_overwritten_unknown"; set_alpha(128.f / 255.f); regs.texturing.tev_stage0.alpha_source1.Assign(T::Source::Constant); regs.texturing.tev_stage0.const_color |= 0xff000000; regs.texturing.tev_stage5.alpha_source1.Assign(T::Source::PrimaryColor); break;
        case 113: control = "custom_clip"; regs.rasterizer.clip_enable.Assign(1); break;
        default: break;
        }
        const auto vertices = std::span<const HV>{cpu.vertex_batch};
        // CodexAstraLocal: Native keeps the inherited state gate, including
        // zero additional endpoint scans for unsupported raw states.
        const auto native_state = Vulkan::PrepareComputeRectState(regs, vertices, false);
        Check(bool(native_state) == (Vulkan::ComputeRectStateRejections(regs) == 0),
              "Native inherited state unchanged");
        const auto old = Vulkan::MakeComputeRectPrepared(regs, vertices, viewport, drawrect,
                                                        native_state);
        const auto packet = Vulkan::MakeComputeRect(regs, vertices, viewport, drawrect);
        if (old) {
            Check(packet && packet->rect == old->rect && packet->color == old->color &&
                  packet->byte_mask == 0xffffffff, "inherited accepts unchanged");
            ++old_accepts;
        } else if (packet) ++new_accepts;
        else ++rejected;
#if !defined(IGNORE_BLEND_PROOF) && !defined(IGNORE_STRICT_GEOMETRY)
        Check(bool(packet) == (id < 90), "exact expected admission");
#endif
        const auto prefix = out / std::to_string(id);
        Binary(prefix.string() + "-vertices.bin", cpu.vertex_batch.data(), cpu.vertex_batch.size() * sizeof(HV));
        if (packet) Binary(prefix.string() + "-packet.bin", *packet);
        FSConfig config{regs};
        Check(GLSL::SupportsDynamicTev(config, user), "actual generic FS support");
        const auto state = GLSL::MakeDynamicTevState(config, profile);
        const auto family = GLSL::MakeDynamicTevFamilyConfig(config, profile);
        std::ofstream(prefix.string() + "-generic.frag") << "#version 450\n"
            << GLSL::FragmentModule{family, user, profile, true}.Generate();
        Binary(prefix.string() + "-state.bin", state);
        FSUniformData fs{};
        fs.framebuffer_scale = 1; fs.depth_scale = -1.f;
        const auto stages = regs.texturing.GetTevStages();
        for (unsigned s = 0; s < 6; ++s)
            for (unsigned c = 0; c < 4; ++c)
                fs.const_color[s][c] = float((stages[s].const_color >> (c * 8)) & 255) / 255.f;
        Binary(prefix.string() + "-fs.bin", fs);
        VSUniformData vs{}; vs.flip_viewport = flip; Binary(prefix.string() + "-vs.bin", vs);
        J row{{"id", id}, {"mode", mode}, {"admitted", bool(packet)}, {"old_admitted", bool(old)},
              {"viewport", viewport}, {"drawrect", drawrect}, {"flip", flip}, {"diagonal", diagonal},
              {"reversed", reverse}, {"clip_interface", bool((id / 3) % 2)}, {"control", control},
              {"raw_state_rejections", Vulkan::ComputeRectStateRejections(regs)},
              {"color_mask", fb.allow_color_write != 0 ? ((om.depth_color_mask >> 8) & 15) : 0},
              {"blend", bool(om.alphablend_enable)},
              {"blend_equations", {unsigned(blend.blend_equation_rgb.Value()), unsigned(blend.blend_equation_a.Value())}},
              {"blend_factors", {unsigned(blend.factor_source_rgb.Value()), unsigned(blend.factor_dest_rgb.Value()),
                                  unsigned(blend.factor_source_a.Value()), unsigned(blend.factor_dest_a.Value())}}};
        if (packet) { row["packet_rect"] = packet->rect; row["packed_color"] = packet->color;
            row["byte_mask"] = packet->byte_mask; row["pixels"] = packet->PixelCount(); }
        report["cases"].push_back(row);
    }
    report["old_accepts"] = old_accepts; report["new_accepts"] = new_accepts;
    report["rejected"] = rejected;
    std::ofstream(out / "fixtures.json") << report.dump(2) << '\n';
    std::cout << "PASS grid checks=" << report["grid_checks"] << " old=" << old_accepts
              << " new=" << new_accepts << " rejected=" << rejected << '\n';
}

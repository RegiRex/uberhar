// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraPro: Exercise production admission, PICA assembly and Vulkan topology
// mapping. Simulated promotion tests order/state, NOT game GPU shader precision.
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h"
#include "video_core/renderer_vulkan/pica_to_vk.h"
#include "video_core/rasterizer_cache/framebuffer_base.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                      fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

struct FakeBackend {
    struct Framebuffer {
        VideoCore::SurfaceId color_id{2}, depth_id{3};
        u32 color_level{}, depth_level{};
        u32 Scale() const { return 2; }
    };
};
namespace VideoCore {
template <> class RasterizerCache<FakeBackend> {
public:
    struct Surface {
        SurfaceInterval GetSubRectInterval(Common::Rectangle<u32>, u32) const {
            return SurfaceInterval(4096, 4160);
        }
    };
    const Surface& GetSurface(SurfaceId) { ++reads; return surface; }
    void InvalidateRegion(PAddr address, u32 bytes, SurfaceId) {
        if (address != 4096 || bytes != 64) throw std::runtime_error("Bad invalidation");
        ++invalidations;
    }
    u64 reads{}, invalidations{};
    Surface surface;
};
} // namespace VideoCore

void Check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
using Topology = Pica::PipelineRegs::TriangleTopology;
using Triangle = std::array<u32, 3>;

// AstraPro: Execute the real persistent CPU assembler, including partial tails
// and GS winding. Keep the independent GPU triangle-list model separate below.
void Submit(Pica::PrimitiveAssembler& assembler, const std::vector<u32>& indices,
            std::vector<Triangle>& triangles) {
    for (u32 index : indices) {
        Pica::OutputVertex vertex{};
        vertex.pos.x = Pica::f24::FromFloat32(static_cast<float>(index));
        assembler.SubmitVertex(vertex, [&](const auto& a, const auto& b, const auto& c) {
            triangles.push_back({static_cast<u32>(a.pos.x.ToFloat32()),
                                 static_cast<u32>(b.pos.x.ToFloat32()),
                                 static_cast<u32>(c.pos.x.ToFloat32())});
        });
    }
}

int main() {
    using namespace Vulkan::ReadyVertexPolicy;
    u64 checks = 0;
    std::array<u64, static_cast<u32>(Admission::Count)> reasons{};
    for (u32 flags = 0; flags < 64; ++flags) {
        for (u32 topology : {0U, 1U, 2U, 3U, 4U, 0xffU}) {
            for (u32 count : {0U, 1U, 2U, 3U, 93U, 95U, 96U, 97U, 99U,
                              65532U, 65535U, 65536U, 0xffffffffU}) {
                const bool automatic = flags & 1, debug = flags & 2, empty = flags & 4;
                const bool geometry = flags & 8, winding = flags & 16, matches = flags & 32;
                const bool reference = automatic && !debug && empty && !geometry && matches &&
                    (topology == 0 || (topology == 3 && !winding)) &&
                    count >= 96 && count <= 65535 && count % 3 == 0;
                const auto result = Classify(automatic, debug, empty, geometry,
                    static_cast<Topology>(topology), count, winding, matches);
                Check(IsEligible(result) == reference, "Admission differs from independent contract");
                Check(static_cast<u32>(result) < reasons.size(), "Invalid diagnostic bucket");
                ++reasons[static_cast<u32>(result)];
                ++checks;
            }
        }
    }
    for (u64 count : reasons) Check(count != 0, "An admission outcome was not exercised");
    Check(Classify(true,false,true,false,Topology::Shader,96,true,true) == Admission::Winding,
          "Pending winding was not explained");
    Check(Classify(true,false,true,false,Topology::Shader,96,false,false) == Admission::Topology,
          "Register/assembler mismatch was not explained");
    Check(PicaToVK::PrimitiveTopology(Topology::Shader) == vk::PrimitiveTopology::eTriangleList &&
          PicaToVK::PrimitiveTopology(Topology::List) == vk::PrimitiveTopology::eTriangleList,
          "Production Vulkan Shader/List mapping changed");
    Check(CanQueue(0,false) && CanQueue(255,false) && !CanQueue(256,false) && !CanQueue(0,true),
          "Speculation budget failed");

    u64 gpu = 0, cpu = 0, serial = 0;
    for (u64 id = 0; id < 200000; ++id) {
        const bool done = id % 5 != 0, failed = id % 17 == 0;
        const u64 actual = id % 23 == 0 ? id + 1 : id;
        const bool select = CanSelect(done,failed,id,actual);
        Check(select == (done && !failed && id == actual), "Readiness/identity selection failed");
        if (select) ++gpu; else ++cpu;
        Check(serial++ == id, "Draw order changed");
    }
    Check(gpu + cpu == 200000 && gpu && cpu, "Dropped or duplicated model draw");

    // AstraPro: Differential mixed-route sequences against uninterrupted CPU
    // assembly. Promotion leaves the persistent assembler untouched. Include
    // incomplete draws, winding left by an earlier emitter, duplicate indices,
    // deferred/failed pipelines, and topology reconfiguration/reset boundaries.
    u64 assembly_draws = 0, promoted = 0, shader_promoted = 0;
    for (Topology first : {Topology::List, Topology::Shader}) {
        for (bool indexed : {false, true}) {
            Pica::PrimitiveAssembler reference{first}, mixed{first};
            u32 seed = 0x91521U;
            for (u32 draw = 0; draw < 2500; ++draw) {
                seed = seed * 1664525U + 1013904223U;
                if (draw % 61 == 0) {
                    const auto topology = draw % 122 == 0 ? Topology::Shader : Topology::List;
                    reference.Reconfigure(topology); mixed.Reconfigure(topology);
                }
                if (draw % 17 == 0) { reference.SetWinding(); mixed.SetWinding(); }
                // Deliberately leave a two-vertex tail, then finish it next draw.
                u32 count = 96 + 3 * (seed % 31);
                if (draw % 23 == 0) count = 2;
                if (draw % 23 == 1) count = 1;
                std::vector<u32> indices(count);
                for (u32 i = 0; i < count; ++i)
                    indices[i] = indexed ? ((i * 173U + draw) % (draw % 2 ? 65536U : 256U)) : i;
                std::vector<Triangle> expected, actual;
                Submit(reference, indices, expected);
                const auto admission = Classify(true, false, mixed.IsEmpty(), false,
                    mixed.GetTopology(), count, mixed.HasPendingWinding(), true);
                const bool ready = CanSelect(draw % 5 != 0, draw % 13 == 0, draw, draw);
                if (IsEligible(admission) && ready) {
                    for (u32 i = 0; i < count; i += 3)
                        actual.push_back({indices[i], indices[i+1], indices[i+2]});
                    ++promoted;
                    if (admission == Admission::ShaderList) ++shader_promoted;
                } else {
                    Submit(mixed, indices, actual);
                }
                Check(actual == expected, "Mixed routes changed primitive order/winding");
                Check(reference.IsEmpty() == mixed.IsEmpty(), "Partial assembly was lost");
                Check(reference.HasPendingWinding() == mixed.HasPendingWinding(),
                      "Pending winding was lost or consumed by promotion");
                Check(reference.GetTopology() == mixed.GetTopology(), "Guest topology changed");
                ++assembly_draws;
            }
        }
    }
    Check(shader_promoted && promoted < assembly_draws, "Missing Shader promotion or CPU recovery");

    // AstraPro: REAL production RAII helper, not a replacement ownership model.
    VideoCore::RasterizerCache<FakeBackend> cache;
    FakeBackend::Framebuffer fb;
    Pica::RasterizerRegs regs{};
    const Common::Rectangle<u32> area{0,480,800,0};
    {
        const VideoCore::FramebufferHelper<FakeBackend> h{&cache,&fb,false,regs,area};
        h.CancelInvalidation();
    }
    Check(cache.reads == 0 && cache.invalidations == 0, "Abandoned candidate dirtied framebuffer");
    { const VideoCore::FramebufferHelper<FakeBackend> h{&cache,&fb,false,regs,area}; }
    Check(cache.reads == 2 && cache.invalidations == 2, "CPU retry lost color/depth ownership");
    { const VideoCore::FramebufferHelper<FakeBackend> h{&cache,&fb,false,regs,area}; }
    Check(cache.invalidations == 4, "Accepted GPU draw lost ownership");
    std::printf("PASS: %llu production admission checks, 200000 readiness decisions (%llu GPU/%llu CPU), "
                "%llu production assembly draw differentials (%llu promoted, %llu Shader), "
                "Vulkan topology mapping and framebuffer cancellation/retry\n",
                (unsigned long long)checks, (unsigned long long)gpu, (unsigned long long)cpu,
                (unsigned long long)assembly_draws, (unsigned long long)promoted,
                (unsigned long long)shader_promoted);
}

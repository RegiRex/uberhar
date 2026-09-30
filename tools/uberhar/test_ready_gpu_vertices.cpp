// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraPro: Production admission and independent ordered fallback model.
// Routing contracts, not Vulkan pixels or game shader equivalence.
#include <array>
#include <cstdio>
#include <stdexcept>
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h"
#include "video_core/rasterizer_cache/framebuffer_base.h"
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
            return SurfaceInterval(4096,4160);
        }
    };
    const Surface& GetSurface(SurfaceId) { ++reads; return surface; }
    void InvalidateRegion(PAddr address, u32 bytes, SurfaceId) {
        if (address!=4096 || bytes!=64) throw std::runtime_error("Bad invalidation");
        ++invalidations;
    }
    u64 reads{}, invalidations{};
    Surface surface;
};
}
void Check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
int main() {
    using namespace Vulkan::ReadyVertexPolicy;
    using Topology=Pica::PipelineRegs::TriangleTopology;
    u64 checks=0;
    for (u32 flags=0;flags<16;++flags) for (u32 topology=0;topology<4;++topology)
        for (u32 count : {0U,3U,93U,95U,96U,97U,99U,65532U,65535U,65536U,0xffffffffU}) {
            bool automatic=flags&1, debug=flags&2, empty=flags&4, geometry=flags&8;
            bool reference=automatic && !debug && empty && !geometry && topology==0 &&
                           count>=96 && count<=65535 && count%3==0;
            Check(Eligible(automatic,debug,empty,geometry,static_cast<Topology>(topology),count)==reference,
                  "Admission differs from independent contract");
            ++checks;
        }
    Check(CanQueue(0,false) && CanQueue(255,false) && !CanQueue(256,false) &&
          !CanQueue(0,true),"Speculation budget failed");
    u64 gpu=0,cpu=0,serial=0;
    for(u64 id=0;id<200000;++id) {
        const bool done=id%5!=0, failed=id%17==0;
        const u64 actual=id%23==0 ? id+1 : id;
        const bool select=CanSelect(done,failed,id,actual);
        Check(select==(done && !failed && id==actual),"Readiness/identity selection failed");
        if(select) ++gpu; else ++cpu;
        Check(serial++==id,"Draw order changed");
    }
    Check(gpu+cpu==200000 && gpu && cpu,"Dropped or duplicated model draw");
    // AstraPro: REAL production RAII helper, not a replacement ownership model.
    VideoCore::RasterizerCache<FakeBackend> cache;
    FakeBackend::Framebuffer fb;
    Pica::RasterizerRegs regs{};
    const Common::Rectangle<u32> area{0,480,800,0};
    {
        const VideoCore::FramebufferHelper<FakeBackend> h{&cache,&fb,false,regs,area};
        h.CancelInvalidation();
    }
    Check(cache.reads==0 && cache.invalidations==0,"Abandoned candidate dirtied framebuffer");
    { const VideoCore::FramebufferHelper<FakeBackend> h{&cache,&fb,false,regs,area}; }
    Check(cache.reads==2 && cache.invalidations==2,"CPU retry lost color/depth ownership");
    { const VideoCore::FramebufferHelper<FakeBackend> h{&cache,&fb,false,regs,area}; }
    Check(cache.invalidations==4,"Accepted GPU draw lost ownership");
    std::printf("PASS: %llu production admission checks, 200000 ordered fallback decisions "
                "(%llu GPU/%llu CPU), real framebuffer cancellation/retry\n",
                (unsigned long long)checks,(unsigned long long)gpu,(unsigned long long)cpu);
}

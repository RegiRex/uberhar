// CodexAstraLocal: Compare retained no-target batches against the original
// AddTriangle/Draw return path. Vulkan targets and actual draws are recording
// endpoints; conversion, preflight and no-target branch bodies are source extracts.
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"
#include "video_core/shader_recovery_error.h"

// CodexAstraLocal: Fail unexpected assertions/logs without normal emulator log
// output. The actual arithmetic helpers and recovery-error type remain included.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view f, const fmt::format_args& a) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(f, a));
}
}
u64 checks{}, compared_bytes{}, scenarios{};
void Need(bool b, const char* why) { ++checks; if (!b) throw std::runtime_error(why); }

namespace VideoCore {
using Pica::f24;
class RasterizerAccelerated {
public:
#include "hardware-type.inc"
    std::vector<HardwareVertex> vertex_batch;
    void AddTriangle(const Pica::OutputVertex&, const Pica::OutputVertex&, const Pica::OutputVertex&);
    static void WriteDeferredTriangles(std::span<const Pica::OutputVertex>, std::span<u8>) noexcept;
};
#include "hardware-functions.inc"
}

// CodexAstraLocal: Only packet storage/publication is an adapter. The actual
// Wait/HardwareBytes methods execute so unpublished/failed work cannot become an
// empty successful retained batch. This does not execute the real CPU executor.
namespace Pica {
class CpuDrawPacket {
public:
    struct Impl {
        std::atomic_bool submitted{true}, ready{true};
        mutable std::mutex mutex;
        mutable std::condition_variable completion;
        std::exception_ptr error;
        std::vector<u8> hardware;
        u32 vertex_count{};
    };
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    void Wait() const;
    std::span<const u8> HardwareBytes() const;
    u32 VertexCount() const noexcept;
};
#include "packet-functions.inc"
}

namespace Vulkan {
// CodexAstraLocal: The actual preflight reads these modeled capability endpoints
// but the compute raw/effective state helpers and register types are production.
struct Framebuffer { bool present{}; bool Handle() const { return present; } };
struct FramebufferHelper { mutable u32 canceled{}; void CancelInvalidation() const { ++canceled; } };
class RasterizerVulkan : public VideoCore::RasterizerAccelerated {
public:
    using DeferredHardwareWriter = decltype(&WriteDeferredTriangles);
    bool strict_compute{}, vertex_capture{}, ready_vertex_attempt{};
    struct { bool ready{}; } cpu_bridge;
    struct { bool enabled{true}; bool HasWorkerThread() const { return enabled; } } scheduler;
    struct { bool enabled{true}; bool CanDeferUpload() const { return enabled; } } stream_buffer;
    bool compute_rect{true};
    Pica::RegsInternal regs{};
    struct { u32 no_target{}; } strict_compute_stats;
    std::vector<std::vector<u8>> draws;
    u32 shader_batches{}, packet_conversions{};
    DeferredHardwareWriter PrepareDeferredVertices(u32 count) const;

    RasterizerVulkan() { regs.framebuffer.output_merger.alpha_test.enable.Assign(1); }
    void Append(std::span<const Pica::OutputVertex> vertices) {
        Need(vertices.size() % 3 == 0, "complete synthetic triangle list");
        ++shader_batches;
        for (std::size_t i=0; i<vertices.size(); i+=3) AddTriangle(vertices[i],vertices[i+1],vertices[i+2]);
    }
    std::shared_ptr<Pica::CpuDrawPacket> Packet(std::span<const Pica::OutputVertex> vertices) {
        const auto writer = PrepareDeferredVertices(static_cast<u32>(vertices.size()));
        Need(writer != nullptr, "intended deferred admission");
        auto packet=std::make_shared<Pica::CpuDrawPacket>();
        packet->impl->vertex_count=static_cast<u32>(vertices.size());
        packet->impl->hardware.resize(vertices.size()*sizeof(HardwareVertex));
        ++shader_batches; ++packet_conversions;
        writer(vertices,packet->impl->hardware);
        return packet;
    }
    std::vector<u8> Bytes() const {
        std::vector<u8> result(vertex_batch.size()*sizeof(HardwareVertex));
        if(!result.empty()) std::memcpy(result.data(),vertex_batch.data(),result.size());
        return result;
    }
    bool Reference(bool target) {
        const std::shared_ptr<Pica::CpuDrawPacket> deferred{};
        Framebuffer f{target}; const auto* framebuffer=&f;
        FramebufferHelper fb_helper;
#include "original-no-target.inc"
        draws.push_back(Bytes()); vertex_batch.clear(); return true;
    }
    bool Candidate(bool target, const std::shared_ptr<Pica::CpuDrawPacket>& deferred={}) {
        Framebuffer f{target}; const auto* framebuffer=&f;
        FramebufferHelper fb_helper;
#include "candidate-no-target.inc"
        // CodexAstraLocal: A target records the exact final draw payload. Actual
        // Vulkan Map/bind/draw order is a separate source-derived control.
        if(deferred) {
            const auto bytes=deferred->HardwareBytes();
            draws.emplace_back(bytes.begin(),bytes.end());
        } else draws.push_back(Bytes());
        vertex_batch.clear(); return true;
    }
};
#include "preflight.inc"
}

// CodexAstraLocal: Distinct values in all 88 consumed bytes make omission,
// ordering and quaternion changes observable. Raw f24 wrapper storage includes
// signed zero, finite cancellation, subnormal, infinity and quiet-NaN witnesses.
std::vector<Pica::OutputVertex> Vertices(u32 n,u32 seed,u32 pattern) {
    std::vector<Pica::OutputVertex> result(n);
    for(u32 i=0;i<n;++i) {
        std::array<float,24> lanes{};
        for(u32 j=0;j<24;++j) lanes[j]=float(seed*1009+i*31+j+1)/64.f;
        std::memcpy(&result[i],lanes.data(),sizeof(result[i]));
        auto f=[](float v){return Pica::f24::FromFloat32(v);};
        const float sign=(i%3==1)?-1.f:1.f;
        switch(pattern) {
        case 0: result[i].quat={f(sign),f(0.f),f(0.f),f(0.f)};break;
        case 1: result[i].quat={f(sign),f(-sign),f(sign),f(sign)};break;
        case 2: result[i].quat={f(std::bit_cast<float>(i%2?0x80000000U:0U)),f(1.f),f(0.f),f(0.f)};break;
        case 3: result[i].quat={f(std::bit_cast<float>(i%3==1?0x7f800000U:0U)),f(sign),f(0.f),f(0.f)};break;
        case 4: result[i].quat={f(std::bit_cast<float>(0x7fc00101U+i)),f(sign),f(0.f),f(0.f)};break;
        case 5: result[i].quat={f(std::bit_cast<float>(0x00000400U)),f(sign),f(0.f),f(0.f)};break;
        }
    }
    return result;
}
void EqualBytes(const std::vector<u8>& a,const std::vector<u8>& b,const char* why) {
    Need(a==b,why); compared_bytes+=a.size();
}
void EqualDraws(const Vulkan::RasterizerVulkan& a,const Vulkan::RasterizerVulkan& b) {
    Need(a.draws.size()==b.draws.size(),"draw count/order");
    for(std::size_t i=0;i<a.draws.size();++i) EqualBytes(a.draws[i],b.draws[i],"complete ordered 88B draw equality");
}

// CodexAstraLocal: The oracle appends every triangle through original AddTriangle
// and invokes the unchanged no-target return. Candidate admission must stop once
// retained bytes exist; later ordinary work concatenates before the one valid draw.
void Sequence(u32 first,u32 later,u32 missing,u32 pattern) {
    Vulkan::RasterizerVulkan original,candidate;
    const auto a=Vertices(first,1,pattern);
    original.Append(a); const auto packet=candidate.Packet(a);
    Need(original.Reference(false) && candidate.Candidate(false,packet),"no-target consumed once");
    EqualBytes(original.Bytes(),candidate.Bytes(),"no-target retained original 88B bytes");
    Need(candidate.draws.empty(),"no-target did not issue draw");
    Need(candidate.PrepareDeferredVertices(later)==nullptr,"retained batch rejects deferred replacement");
    for(u32 i=0;i<missing;++i) {
        const auto b=Vertices(later,2+i,(pattern+1)%6);
        original.Append(b); candidate.Append(b);
        original.Reference(false); candidate.Candidate(false);
        EqualBytes(original.Bytes(),candidate.Bytes(),"repeated no-target concatenation");
    }
    const auto b=Vertices(later,10,(pattern+2)%6);
    original.Append(b);candidate.Append(b);
    const auto expected=original.Bytes();
    original.Reference(true);candidate.Candidate(true);
    EqualDraws(original,candidate);
    Need(candidate.draws.size()==1 && candidate.draws[0].size()==(first+(missing+1)*later)*88,
         "one complete concatenated draw, no skipped/replayed vertices");
    EqualBytes(expected,candidate.draws[0],"original retained prefix before current draw");
    Need(original.vertex_batch.empty() && candidate.vertex_batch.empty(),"target consumes full retained batch");
    Need(original.shader_batches==candidate.shader_batches && candidate.packet_conversions==1,
         "same shading batch count, no retry after publication");
    // CodexAstraLocal: Once the retained batch is consumed, a later independent
    // packet can be admitted again without duplicating the earlier prefix.
    const auto c=Vertices(3,31,(pattern+3)%6);
    original.Append(c);const auto next=candidate.Packet(c);
    original.Reference(true);candidate.Candidate(true,next);EqualDraws(original,candidate);
    Need(candidate.packet_conversions==2 && original.shader_batches==candidate.shader_batches,
         "later packet is independent of retained draw");
    ++scenarios;
}

int main()try {
    static_assert(sizeof(VideoCore::RasterizerAccelerated::HardwareVertex)==88);
    for(u32 n:{3U,6U,63U,768U}) for(u32 next:{3U,9U})
        for(u32 missing:{0U,1U,2U})for(u32 pattern=0;pattern<6;++pattern)
            Sequence(n,next,missing,pattern);
    Vulkan::RasterizerVulkan raster;
    Need(!raster.PrepareDeferredVertices(0) && !raster.PrepareDeferredVertices(4),"empty/incomplete refusal");
    const auto inputs=Vertices(6,1,0);
    const auto packet=raster.Packet(inputs);
    packet->impl->submitted=false;
    bool caught=false;
    try{raster.Candidate(false,packet);}catch(const VideoCore::ShaderRecoveryError&){caught=true;}
    Need(caught && raster.vertex_batch.empty() && raster.draws.empty(),"unpublished is terminal before retention");
    packet->impl->submitted=true;
    packet->impl->error=std::make_exception_ptr(std::runtime_error("controlled worker failure"));
    caught=false;
    try{raster.Candidate(false,packet);}catch(const VideoCore::ShaderRecoveryError&){caught=true;}
    Need(caught && raster.vertex_batch.empty() && raster.draws.empty(),"failed is terminal before retention");
    // CodexAstraLocal: Check actual state-helper rejection, not only a raw mask
    // stub. Output-dependent blend-only admission must remain synchronous.
    const auto effective=Vulkan::PrepareComputeRectState(raster.regs,
        std::span<const VideoCore::RasterizerAccelerated::HardwareVertex>{raster.vertex_batch},true);
    Need(!effective && raster.PrepareDeferredVertices(6),"permanent state rejection supports deferred census");
    raster.regs.framebuffer.output_merger.alpha_test.enable.Assign(0);
    Need(!raster.PrepareDeferredVertices(6),"expandable or zero raw state remains synchronous");
    raster.compute_rect=false;
    Need(raster.PrepareDeferredVertices(6),"no compute decision needs output");
    raster.strict_compute=true; Need(!raster.PrepareDeferredVertices(6),"strict mode refusal");raster.strict_compute=false;
    raster.scheduler.enabled=false;Need(!raster.PrepareDeferredVertices(6),"no worker refusal");raster.scheduler.enabled=true;
    raster.stream_buffer.enabled=false;Need(!raster.PrepareDeferredVertices(6),"noncoherent stream refusal");raster.stream_buffer.enabled=true;
    raster.vertex_capture=true;Need(!raster.PrepareDeferredVertices(6),"capture refusal");raster.vertex_capture=false;
    raster.ready_vertex_attempt=true;Need(!raster.PrepareDeferredVertices(6),"accelerated attempt refusal");raster.ready_vertex_attempt=false;
    raster.cpu_bridge.ready=true;Need(!raster.PrepareDeferredVertices(6),"bridge refusal");
    std::cout<<"{\"scenarios\":"<<scenarios<<",\"checks\":"<<checks
             <<",\"compared_bytes\":"<<compared_bytes<<"}\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}

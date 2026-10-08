// CodexAstraLocal: Execute the exact queued Draw body with recording command and
// resource endpoints; this proves captured arguments, not Vulkan synchronization.
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"
#define VK_QUEUE_FAMILY_IGNORED 0xffffffffU
namespace vk {
enum class AccessFlagBits : u32 { eMemoryRead=1,eMemoryWrite=2,eShaderWrite=4,eShaderRead=8 };
struct AccessFlags {
    u32 bits{};
    AccessFlags()=default;
    AccessFlags(AccessFlagBits b):bits(static_cast<u32>(b)){}
    explicit AccessFlags(u32 b):bits(b){}
    bool operator==(const AccessFlags&)const=default;
};
AccessFlags operator|(AccessFlagBits a,AccessFlagBits b) {
    return AccessFlags{static_cast<u32>(a)|static_cast<u32>(b)};
}
enum class ImageLayout { eGeneral };
enum class ImageAspectFlagBits { eColor };
enum class PipelineStageFlagBits { eAllCommands,eComputeShader };
enum class PipelineBindPoint { eCompute };
enum class ShaderStageFlagBits { eCompute };
struct Range {ImageAspectFlagBits aspect;u32 level,levels,layer,layers;};
struct ImageMemoryBarrier {
    AccessFlags srcAccessMask,dstAccessMask;
    ImageLayout oldLayout,newLayout;
    u32 srcQueueFamilyIndex,dstQueueFamilyIndex;
    u64 image;
    Range subresourceRange;
};
}
unsigned checks{};
void Check(bool ok,const char* msg) {++checks;if(!ok)throw std::runtime_error(msg);}
namespace Vulkan {
enum class StateFlags : u32 {Pipeline=1,DescriptorSets=2};
u32 operator|(StateFlags a,StateFlags b){return static_cast<u32>(a)|static_cast<u32>(b);}
struct Trace {
    std::vector<vk::ImageMemoryBarrier> barriers;
    std::vector<std::array<vk::PipelineStageFlagBits,2>> stages;
    ComputeRectPacket packet{};
    u64 pipeline{},layout{},descriptor{};
    std::array<u32,3> dispatch{};
};
struct RecordingCommands {
    Trace* trace;
    void pipelineBarrier(vk::PipelineStageFlagBits a,vk::PipelineStageFlagBits b,
                         int,int,int,const vk::ImageMemoryBarrier& c) {
        trace->barriers.push_back(c);trace->stages.push_back({a,b});
    }
    void bindPipeline(vk::PipelineBindPoint,u64 p){trace->pipeline=p;}
    void bindDescriptorSets(vk::PipelineBindPoint,u64 layout,unsigned,u64 set,int) {
        trace->layout=layout;trace->descriptor=set;
    }
    void pushConstants(u64 layout,vk::ShaderStageFlagBits,unsigned offset,unsigned size,const void* p) {
        Check(layout==trace->layout && offset==0 && size==32,"packet push ABI");
        std::memcpy(&trace->packet,p,sizeof(trace->packet));
    }
    void dispatch(u32 x,u32 y,u32 z){trace->dispatch={x,y,z};}
};
struct Scheduler {
    std::vector<std::function<void(RecordingCommands)>> work;
    std::vector<u32> dirtied;
    template<class F> void Record(F&& f){work.emplace_back(std::forward<F>(f));}
    void MakeDirty(u32 flags){dirtied.push_back(flags);}
    std::vector<Trace> Drain() {
        std::vector<Trace> traces(work.size());
        for(unsigned i=0;i<work.size();++i)work[i]({&traces[i]});
        work.clear();return traces;
    }
};
struct Surface {
    u64 image{31},view{41};
    u64 Image()const{return image;}
    u64 StorageView()const{return view;}
};
struct Heap {u64 next{51};u64 Commit(){return next++;}};
struct Handle {u64 value;u64 operator*()const{return value;}};
struct Updates {
    std::vector<std::array<u64,3>> rows;
    void AddStorageImage(u64 set,u64 binding,u64 view){rows.push_back({set,binding,view});}
};
struct ComputeRectRenderer {
    std::unique_ptr<Heap> heap{std::make_unique<Heap>()};
    Updates updates;
    Scheduler scheduler;
    Handle layout{61},pipeline{71};
    u64 compute_draws{},compute_pixels{};
    void Draw(Surface&,const ComputeRectPacket&);
};
#include "draw.inc"
}
int main() {
    try {
        using namespace Vulkan;
        ComputeRectRenderer r;Surface surface;
        ComputeRectPacket full{{1,2,13,17},0x12345678,0xffffffff};
        ComputeRectPacket partial{{7,9,3,5},0x89abcdef,0xff00ff};
        const auto expected_full=full,expected_partial=partial;
        r.Draw(surface,full);
        surface.image=32;surface.view=42;r.layout.value=62;r.pipeline.value=72;
        r.Draw(surface,partial);
        Check(r.compute_draws==2 && r.compute_pixels==236,"exact useful draw and pixel counters");
        Check(r.scheduler.work.size()==2 && r.scheduler.dirtied==std::vector<u32>{3,3},"queued draws and graphics dirtied");
        Check(r.updates.rows==std::vector<std::array<u64,3>>{{51,0,41},{52,0,42}},"actual storage views and descriptor owners");
        full={{11,12,19,23},0,0xff};partial={{0,0,1,1},0xffffffff,0xffffffff};
        surface.image=999;surface.view=999;r.layout.value=999;r.pipeline.value=999;
        const auto traces=r.scheduler.Drain();
        for(unsigned i=0;i<2;++i) {
            const auto& t=traces[i];const auto& p=i?expected_partial:expected_full;
            Check(std::memcmp(&t.packet,&p,32)==0,"queued packet is immutable");
            Check(t.layout==61+i && t.pipeline==71+i && t.descriptor==51+i,"queued handles are immutable");
            Check(t.dispatch==std::array<u32,3>{u32((p.rect[2]+7)/8),u32((p.rect[3]+7)/8),1},"complete bounded dispatch dimensions");
            Check(t.barriers.size()==2 && t.stages.size()==2,"two ordered image barriers");
            const auto& in=t.barriers[0];const auto& after=t.barriers[1];
            Check(in.image==31+i && after.image==31+i,"queued image owner is immutable");
            Check(in.srcAccessMask.bits==3,"incoming prior reads and writes visible");
            Check(in.dstAccessMask.bits==(i?12:4),"partial mask read dependency");
            Check(after.srcAccessMask.bits==4 && after.dstAccessMask.bits==3,"outgoing writes visible");
            Check(t.stages[0]==std::array{vk::PipelineStageFlagBits::eAllCommands,vk::PipelineStageFlagBits::eComputeShader} &&
                  t.stages[1]==std::array{vk::PipelineStageFlagBits::eComputeShader,vk::PipelineStageFlagBits::eAllCommands},"barrier stage directions");
            for(const auto& b:t.barriers) {
                Check(b.oldLayout==vk::ImageLayout::eGeneral && b.newLayout==vk::ImageLayout::eGeneral,"layout contract unchanged");
                Check(b.srcQueueFamilyIndex==0xffffffff && b.dstQueueFamilyIndex==0xffffffff,"same queue ownership");
                Check(b.subresourceRange.level==0 && b.subresourceRange.levels==1 &&
                      b.subresourceRange.layer==0 && b.subresourceRange.layers==1,"same validated base subresource");
            }
        }
        std::cout<<"PASS "<<checks<<" exact Draw capture/barrier checks\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;
    }
}

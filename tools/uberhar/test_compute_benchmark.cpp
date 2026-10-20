// CodexAstraLocal: Exercise actual scratch request/owner/command bodies with
// explicit filesystem, scheduler, Vulkan-result and clock boundary adapters.
// No driver execution is modeled; original-input shader pixels have a separate gate.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <json.hpp>
#include "common/common_funcs.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/uberhar_activity.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/renderer_vulkan/uberhar_compute_benchmark.h"
#include "video_core/renderer_vulkan/uberhar_push_constants.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "video_core/shader_recovery_error.h"

using Json = nlohmann::json;
unsigned checks{};
void Check(bool ok, const char* reason) {
    ++checks;
    if (!ok) throw std::runtime_error(reason);
}
namespace Common { const char g_scm_rev[] = "synthetic-source-identity"; }
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                      fmt::string_view f, const fmt::format_args& a) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(f, a));
}
}
// CodexAstraLocal: Preserve the actual CPU HardwareVertex constructor/layout.
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
using Vertex = VideoCore::RasterizerAccelerated::HardwareVertex;
static_assert(sizeof(Vertex)==88);

// CodexAstraLocal: Controlled host time makes deadlines testable without sleeps.
struct Clock {
    using duration=std::chrono::nanoseconds;
    using rep=duration::rep;
    using period=duration::period;
    using time_point=std::chrono::time_point<Clock>;
    static constexpr bool is_steady=true;
    static inline duration value{};
    static time_point now() { return time_point{value}; }
};
namespace Settings {
#include "mode.inc"
struct ModeSetting { UberharTestMode mode=UberharTestMode::Compute; auto GetValue()const{return mode;} };
struct Values { ModeSetting uberhar_test_mode; } values;
#include "strict-policy.inc"
}
namespace AndroidUtils { bool raw=true; bool CanUseRawFS(){return raw;} }
namespace FileUtil {
// CodexAstraLocal: Real temporary files preserve exclusive-create/restart
// behavior; injected IO failures are confined to this provider boundary.
enum class UserPath { ConfigDir,DumpDir };
std::string root;
unsigned opens{}, writes{};
bool fail_write{}, fail_flush{}, fail_close{}, fail_directory{};
std::string GetUserPath(UserPath){return root+"/";}
bool CreateFullPath(const std::string& path) {
    if(fail_directory)return false;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());return true;
}
struct IOFile {
    FILE* file{};
    IOFile(const std::string& p,const char* mode){++opens;file=std::fopen(p.c_str(),mode);}
    ~IOFile(){if(file)std::fclose(file);}
    bool IsOpen()const{return file;}
    std::size_t GetSize(){auto pos=std::ftell(file);std::fseek(file,0,SEEK_END);auto n=std::ftell(file);std::fseek(file,pos,SEEK_SET);return n;}
    std::size_t ReadBytes(void* p,std::size_t n){return std::fread(p,1,n,file);}
    std::size_t WriteBytes(const void* p,std::size_t n){++writes;return std::fwrite(p,1,fail_write?n/2:n,file);}
    bool Flush(){return std::fflush(file)==0&&!fail_flush;}
    bool Close(){auto* f=file;file=nullptr;return std::fclose(f)==0&&!fail_close;}
};
}

namespace Vulkan {
using namespace ComputeBenchmarkData;
using namespace Pica::Shader::Generator;
constexpr u32 UploadBytes=MaxImageBytes+16384,ResultBytes=65536;
constexpr vk::ImageSubresourceRange ColorRange{vk::ImageAspectFlagBits::eColor,0,1,0,1};
constexpr vk::ImageSubresourceLayers ColorLayer{vk::ImageAspectFlagBits::eColor,0,0,1};
template<class T> T H(u64 value){return T{reinterpret_cast<typename T::CType>(value)};}
template<class T> u64 ID(T handle){return reinterpret_cast<u64>(static_cast<typename T::CType>(handle));}
template<class T> struct Handle { T value{}; T operator*()const{return value;} };
struct BufferOwner {
    int allocator{}; int allocation{};
    std::vector<u32> bytes=std::vector<u32>(UploadBytes/4);
    void* mapped=bytes.data();u64 allocated=UploadBytes;
    vk::Buffer handle{};vk::Buffer Handle()const{return handle;}
};
struct ImageOwner { VkImage image{};Handle<vk::Framebuffer> framebuffer; };
VkResult flush_result=VK_SUCCESS,invalidate_result=VK_SUCCESS;
unsigned flushes{},invalidates{};
VkResult vmaFlushAllocation(int,int,u64,u64){++flushes;return flush_result;}
VkResult vmaInvalidateAllocation(int,int,u64,u64){++invalidates;return invalidate_result;}

// CodexAstraLocal: Keep Vulkan structures real; only command endpoints record
// ordered operations/immutable arguments instead of invoking a driver.
struct Event {std::string kind;u64 a{},b{},c{};};
struct Trace {
    std::vector<Event> events;
    std::vector<vk::ImageMemoryBarrier> image_barriers;
    std::vector<vk::BufferMemoryBarrier> buffer_barriers;
    std::vector<ComputeRectPacket> packets;
    unsigned draws{},dispatches{};
};
struct RecordingCommands {
    Trace* t;
    void pipelineBarrier(vk::PipelineStageFlags a,vk::PipelineStageFlags b,vk::DependencyFlags,
        vk::ArrayProxy<const vk::MemoryBarrier>,vk::ArrayProxy<const vk::BufferMemoryBarrier> buffers,
        vk::ArrayProxy<const vk::ImageMemoryBarrier> images) {
        t->events.push_back({"barrier",u32(a),u32(b)});
        for(const auto& v:buffers)t->buffer_barriers.push_back(v);
        for(const auto& v:images)t->image_barriers.push_back(v);
    }
    void bindPipeline(vk::PipelineBindPoint route,vk::Pipeline p){t->events.push_back({"bind",u32(route),ID(p)});}
    void bindDescriptorSets(vk::PipelineBindPoint,vk::PipelineLayout,u32,
        vk::ArrayProxy<const vk::DescriptorSet> sets,vk::ArrayProxy<const u32>) {
        t->events.push_back({"descriptors",sets.size(),ID(*sets.begin())});
    }
    void pushConstants(vk::PipelineLayout,vk::ShaderStageFlags stages,u32,u32 size,const void* p) {
        t->events.push_back({"push",u32(stages),size});
        if(stages==vk::ShaderStageFlagBits::eCompute){ComputeRectPacket packet;std::memcpy(&packet,p,32);t->packets.push_back(packet);}
    }
    void dispatch(u32 x,u32 y,u32 z){++t->dispatches;t->events.push_back({"dispatch",x,y,z});}
    void clearColorImage(vk::Image image,vk::ImageLayout,const vk::ClearColorValue&,vk::ArrayProxy<const vk::ImageSubresourceRange>){t->events.push_back({"clear",ID(image)});}
    void resetQueryPool(vk::QueryPool,u32 first,u32 count){t->events.push_back({"reset_queries",first,count});}
    void copyBufferToImage(vk::Buffer,vk::Image image,vk::ImageLayout,const vk::BufferImageCopy& c){t->events.push_back({"upload",ID(image),c.imageExtent.width,c.imageExtent.height});}
    void writeTimestamp(vk::PipelineStageFlagBits stage,vk::QueryPool,u32 index){t->events.push_back({"stamp",index,u32(stage)});}
    void beginRenderPass(const vk::RenderPassBeginInfo& info,vk::SubpassContents){t->events.push_back({"begin",ID(info.framebuffer)});}
    void setViewport(u32,const vk::Viewport& v){t->events.push_back({"viewport",u32(v.width),u32(v.height)});}
    void setScissor(u32,const vk::Rect2D& s){t->events.push_back({"scissor",s.extent.width,s.extent.height});}
    void bindVertexBuffers(u32,vk::Buffer b,vk::DeviceSize offset){t->events.push_back({"vertices",ID(b),offset});}
    void draw(u32 count,u32 instances,u32 first,u32 base){Check(count==6&&instances==1&&base==0,"original six vertex command");++t->draws;t->events.push_back({"draw",first});}
    void endRenderPass(){t->events.push_back({"end"});}
    void copyImageToBuffer(vk::Image image,vk::ImageLayout,vk::Buffer b,const vk::BufferImageCopy&){t->events.push_back({"readback",ID(image),ID(b)});}
};
#include "state-flags.inc"
DECLARE_ENUM_FLAG_OPERATORS(StateFlags)
struct Scheduler {
    std::vector<std::function<void(RecordingCommands)>> work;
    u64 current=11,submitted{},completed{};StateFlags dirty=StateFlags::AllDirty;
    unsigned records{},queries{};bool fail_record{},terminal_record{};
    template<class F>void Record(F&& f){++records;if(terminal_record)throw VideoCore::ShaderRecoveryError("worker");if(fail_record)throw std::bad_alloc{};work.emplace_back(std::forward<F>(f));}
    void MakeDirty(StateFlags f){dirty|=f;}
    u64 CurrentTick()const{return current;}
    std::optional<u64> TrySubmittedTick()const{return submitted?std::optional<u64>{submitted}:std::nullopt;}
    bool IsFree(u64 t){++queries;return completed>=t;}
    Scheduler* GetMasterSemaphore(){return this;}
    u64 KnownGpuTick()const{return completed;}
    Trace Drain(){Trace t;for(auto& f:work)f({&t});work.clear();return t;}
};
struct RenderManager { unsigned ends{};void EndRendering(){++ends;} };
struct Device {
    vk::Result result=vk::Result::eSuccess;bool available=true,zero{},reverse{};mutable unsigned calls{};
    vk::Result getQueryPoolResults(vk::QueryPool,u32,u32 count,std::size_t bytes,void* data,
                                  vk::DeviceSize stride,vk::QueryResultFlags flags)const {
        ++calls;Check(count==4&&bytes==64&&stride==16&&!(flags&vk::QueryResultFlagBits::eWait),"nonblocking query ABI");
        std::array<u64,8> values{1000,u64(available),zero?1000u:2000u,u64(available),3000,u64(available),reverse?2999u:5500u,u64(available)};
        std::memcpy(data,values.data(),64);return result;
    }
};
struct Instance { Device device;const Device& GetDevice()const{return device;} };
unsigned preparations{},owners{},destroyed{};int prepare_failure{};
// CodexAstraLocal: The production interface is public only in this fixture so
// tests can inspect retained owners; constructor/fields and methods remain extracted.
class ComputeBenchmark {
public:
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit ComputeBenchmark(std::unique_ptr<Impl> value);
    ~ComputeBenchmark();
    static std::unique_ptr<ComputeBenchmark> Load(const Instance&,Scheduler&,RenderManager&,const Pica::Shader::Profile&,u64);
    static std::unique_ptr<ComputeBenchmark> LoadUnsupported(const Instance&,Scheduler&,RenderManager&,const Pica::Shader::Profile&,u64);
    static std::unique_ptr<ComputeBenchmark> LoadLibretro(const Instance&,Scheduler&,RenderManager&,const Pica::Shader::Profile&,u64);
    void Poll(u64);
    void FinishAfterDrain()noexcept;
};
struct ComputeBenchmark::Impl {
#include "owner-fields.inc"
    BufferOwner upload;
    std::array<BufferOwner,2> readbacks;
    Handle<vk::RenderPass> pass{H<vk::RenderPass>(21)};
    std::array<ImageOwner,2> images{{{static_cast<VkImage>(H<vk::Image>(31)),{H<vk::Framebuffer>(41)}},{static_cast<VkImage>(H<vk::Image>(32)),{H<vk::Framebuffer>(42)}}}};
    ImageOwner dummy{static_cast<VkImage>(H<vk::Image>(33)),{}};
    Handle<vk::QueryPool> queries{H<vk::QueryPool>(51)};
    std::array<vk::DescriptorSet,3> sets{H<vk::DescriptorSet>(61),H<vk::DescriptorSet>(62),H<vk::DescriptorSet>(63)};
    Handle<vk::PipelineLayout> compute_layout{H<vk::PipelineLayout>(71)},graphics_layout{H<vk::PipelineLayout>(72)};
    Handle<vk::Pipeline> compute_pipeline{H<vk::Pipeline>(81)};
    std::array<Handle<vk::Pipeline>,2> graphics_pipelines{{{H<vk::Pipeline>(82)},{H<vk::Pipeline>(83)}}};
#include "owner-declarations.inc"
    ~Impl(){--owners;++destroyed;}
};
// CodexAstraLocal: Heavy Vulkan preparation is the explicit adapter boundary;
// allocation/terminal failures still enter the actual Load containment code.
void ComputeBenchmark::Impl::Prepare(){
    ++preparations;++owners;
    if(prepare_failure==1)throw std::bad_alloc{};
    if(prepare_failure==2)throw VideoCore::ShaderRecoveryError("prepare terminal");
    if(prepare_failure==3)throw vk::DeviceLostError("prepare lost");
    vs_offset=MaxImageBytes+sizeof(workload.vertices);fs_offset=vs_offset+sizeof(VSUniformData);
    upload.handle=H<vk::Buffer>(91);readbacks[0].handle=H<vk::Buffer>(92);readbacks[1].handle=H<vk::Buffer>(93);
    timestamp_period=2;timestamp_bits=48;timestamp_mask=(u64{1}<<48)-1;status="ready";
}
#include "owner-methods.inc"
ComputeBenchmark::ComputeBenchmark(std::unique_ptr<Impl> p):impl{std::move(p)}{}
ComputeBenchmark::~ComputeBenchmark()=default;
#include "owner-loads.inc"
}

// CodexAstraLocal: Fixtures export the same fixed packets/original vertices and
// four device-interface profiles for independent real shader pixel comparison.
void Binary(const std::filesystem::path& p,const void* data,std::size_t size){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(data),size);Check(f.good(),"fixture write");}
template<class T>void Binary(const std::filesystem::path& p,const T& v){Binary(p,&v,sizeof(v));}
void Produce(const std::filesystem::path& out){
    using namespace Vulkan::ComputeBenchmarkData;
    using namespace Pica::Shader;
    using namespace Pica::Shader::Generator;
    std::filesystem::create_directories(out);
    Workload<Vertex> w;Json manifest{{"cases",Json::array()}};
    VideoCore::RasterizerAccelerated r;r.MakeSoftwareVertexLayout();
    manifest["stride"]=r.software_layout.bindings[0].byte_count.Value();
    for(unsigned a=0;a<8;++a){const auto& v=r.software_layout.attributes[a];manifest["attributes"].push_back({{"location",v.location.Value()},{"size",v.size.Value()},{"offset",v.offset.Value()}});}
    for(u32 c=0;c<CaseCount;++c){
        Check(Prepare(c,w),"all fixed workloads pass actual admission");
        const auto info=Cases[c];
        Binary(out/((std::to_string(c))+"-vertices.bin"),w.vertices);
        Binary(out/((std::to_string(c))+"-packets.bin"),w.packets);
        std::vector<u32> background(info.width*info.height),expected(background.size());u64 touched=0;
        for(u32 y=0;y<info.height;++y)for(u32 x=0;x<info.width;++x){const auto p=y*info.width+x;background[p]=Background(x,y);expected[p]=Expected(c,x,y);touched+=expected[p]!=background[p];}
        Check(touched>0,"independent useful expected pixels");
        Binary(out/(std::to_string(c)+"-background.bin"),background.data(),background.size()*4);
        Binary(out/(std::to_string(c)+"-expected.bin"),expected.data(),expected.size()*4);
        FSUniformData fs{};fs.framebuffer_scale=1;fs.depth_scale=-1;VSUniformData vs{};
        Binary(out/(std::to_string(c)+"-uniforms.bin"),fs);Binary(out/(std::to_string(c)+"-vs.bin"),vs);
        for(unsigned profile_id=0;profile_id<4;++profile_id){
            Profile profile{};profile.is_vulkan=true;profile.has_separable_shaders=true;profile.has_clip_planes=profile_id&1;profile.has_logic_op=!(profile_id&2);
            const FSConfig config{w.regs};const UserConfig user{};
            Check(GLSL::SupportsDynamicTev(config,user),"fixed generic family supports reference");
            const auto state=GLSL::MakeDynamicTevState(config,profile);
            Binary(out/(std::to_string(c)+"-"+std::to_string(profile_id)+"-state.bin"),state);
            std::ofstream(out/(std::to_string(profile_id)+".vert"))<<"#version 450\n#define VULKAN 1\n"<<GLSL::GenerateTrivialVertexShader(profile.has_clip_planes,true);
            std::ofstream(out/(std::to_string(c)+"-"+std::to_string(profile_id)+".frag"))<<"#version 450\n#define VULKAN 1\n"<<GLSL::FragmentModule{GLSL::MakeDynamicTevFamilyConfig(config,profile),user,profile,true}.Generate();
        }
        manifest["cases"].push_back({{"id",c},{"width",info.width},{"height",info.height},{"channels",info.channels},{"changed_pixels",touched}});
    }
    Check(!Prepare(CaseCount,w),"case cap");
    std::ofstream(out/"manifest.json")<<manifest.dump(2)<<'\n';
}

using namespace Vulkan;
constexpr u64 Title=0x0004000000055F00;
struct Environment {
    Instance instance;Scheduler scheduler;RenderManager renderer;Pica::Shader::Profile profile{};
    Environment(const std::filesystem::path& root,const std::string& name){
        FileUtil::root=(root/name).string();std::filesystem::create_directories(FileUtil::root);
        Settings::values.uberhar_test_mode.mode=Settings::UberharTestMode::Compute;AndroidUtils::raw=true;
        FileUtil::fail_write=FileUtil::fail_flush=FileUtil::fail_close=FileUtil::fail_directory=false;
        FileUtil::opens=FileUtil::writes=0;prepare_failure=0;flush_result=invalidate_result=VK_SUCCESS;Clock::value={};
        Common::UberharActivity::session=73;
        profile.is_vulkan=true;profile.has_separable_shaders=true;profile.has_clip_planes=true;profile.has_logic_op=true;
    }
    void Request(std::string id="one"){
        Json j{{"schema",1},{"enabled",true},{"benchmark_id",id},{"trigger","renderer_startup"},{"title_id",fmt::format("{:016X}",Title)}};
        Text(j.dump());
    }
    void Text(const std::string& s){std::ofstream(FileUtil::root+"/uberhar_compute_benchmark.json")<<s;}
    auto Load(){return ComputeBenchmark::Load(instance,scheduler,renderer,profile,Title);}
    Json Result(const std::string& id="one"){std::ifstream f(FileUtil::root+"/compute-benchmark-"+id+".result.json");Check(f.good(),"result exists");return Json::parse(f);}
    void Complete(ComputeBenchmark& b){
        auto& s=*b.impl;const auto c=Cases[s.next_pair/PairsPerCase];
        for(u32 y=0;y<c.height;++y)for(u32 x=0;x<c.width;++x)
            for(auto& r:s.readbacks)static_cast<u32*>(r.mapped)[y*c.width+x]=Expected(s.next_pair/PairsPerCase,x,y);
        scheduler.submitted=scheduler.completed=s.tick;
    }
};

void OwnerTests(const std::filesystem::path& out){
    Common::UberharActivity::session=73;
    {
        Environment e(out,"off");e.Request();
        for(auto mode:{Settings::UberharTestMode::Custom,Settings::UberharTestMode::Native,Settings::UberharTestMode::Automatic,Settings::UberharTestMode::ComboGeneric}){
            Settings::values.uberhar_test_mode.mode=mode;Check(!e.Load()&&FileUtil::opens==0,"other modes no request IO");
        }
        Settings::values.uberhar_test_mode.mode=Settings::UberharTestMode::Compute;
        Check(!ComputeBenchmark::LoadUnsupported(e.instance,e.scheduler,e.renderer,e.profile,Title)&&FileUtil::opens==0,"other platform no request IO");
        Check(!ComputeBenchmark::LoadLibretro(e.instance,e.scheduler,e.renderer,e.profile,Title)&&FileUtil::opens==0,"Libretro no request IO");
        AndroidUtils::raw=false;Check(!e.Load()&&FileUtil::opens==0,"raw provider guard before IO");
    }
    const std::vector<std::string> bad{"",std::string(1025,' '),"[]","{}","{\"schema\":1,\"schema\":1}","{\"x\":[[[[]]]]}","{\"schema\":1,\"enabled\":true,\"benchmark_id\":\"../x\",\"trigger\":\"renderer_startup\",\"title_id\":\"0004000000055F00\"}"};
    for(unsigned i=0;i<bad.size();++i){Environment e(out,"bad"+std::to_string(i));e.Text(bad[i]);auto n=preparations;Check(!e.Load()&&preparations==n,"malformed requests do not prepare");}
    // CodexAstraLocal: Strict JSON types/key identity are independent of its
    // byte/depth bound; every rejected request must avoid claiming/preparation.
    for(unsigned i=0;i<8;++i){
        Environment e(out,"types"+std::to_string(i));e.Request();
        std::ifstream file(FileUtil::root+"/uberhar_compute_benchmark.json");auto j=Json::parse(file);
        if(i==0)j["schema"]=1.5;if(i==1)j["enabled"]=1;
        if(i==2)j["title_id"]="0000000000000001";
        if(i==3){j.erase("benchmark_id");j["other"]="x";}
        if(i==4)j["extra"]=false;if(i==5)j["benchmark_id"]=std::string(49,'a');
        if(i==6)j["trigger"]="other";if(i==7)j["schema"]=true;
        e.Text(j.dump());auto n=preparations;Check(!e.Load()&&preparations==n,"strict request types and keys");
    }
    for(unsigned i=0;i<3;++i){
        Environment e(out,"claimio"+std::to_string(i));e.Request();
        FileUtil::fail_write=i==0;FileUtil::fail_flush=i==1;FileUtil::fail_close=i==2;
        const auto n=preparations;Check(!e.Load()&&preparations==n,"claim write failure prevents preparation");
        FileUtil::fail_write=FileUtil::fail_flush=FileUtil::fail_close=false;
        Check(!e.Load()&&preparations==n,"partial claim cannot rearm");
    }
    {
        Environment e(out,"claim");e.Request();auto b=e.Load();Check(b&&b->impl->profile==e.profile,"exact immutable profile snapshot");
        e.profile.has_clip_planes=false;Check(b->impl->profile.has_clip_planes,"live profile cannot change snapshot");
        auto n=preparations;Check(!e.Load()&&preparations==n,"claimed request cannot rearm");
        b->FinishAfterDrain();b.reset();Check(!e.Load()&&preparations==n,"claim survives owner recreation");
    }
    for(int failure=1;failure<=3;++failure){
        Environment e(out,"prepare"+std::to_string(failure));e.Request();prepare_failure=failure;bool terminal=false;
        try{auto b=e.Load();Check(failure==1&&b&&b->impl->reported,"local preparation fails closed");}
        catch(const VideoCore::ShaderRecoveryError&){terminal=true;}
        Check(terminal==(failure!=1),"preparation terminal error distinction");
        auto n=preparations;Check(!e.Load()&&preparations==n,"failed preparation consumes claim");
    }
    {
        // CodexAstraLocal: Queue acceptance without GPU completion cannot
        // authorize query reads, invalidation or another pair of host writes.
        Environment e(out,"accepted");e.Request();auto b=e.Load();b->Poll(Title);
        e.scheduler.submitted=b->impl->tick;const auto before=invalidates;
        b->Poll(Title);
        Check(b->impl->next_pair==0&&e.instance.device.calls==0&&invalidates==before&&
              e.scheduler.records==1,"accepted pair still needs GPU completion");
        e.scheduler.Drain();e.Complete(*b);b->FinishAfterDrain();
    }
    {
        Environment e(out,"full");e.Request();auto b=e.Load();Check(bool(b),"valid request");b->Poll(Title);
        Check(b->impl->enqueued_routes==2&&b->impl->pending,"one initial pair");
        auto initial=b->impl->workload.packets;b->impl->workload.packets={};
        for(unsigned pair=0;pair<CaseCount*PairsPerCase;++pair){
            auto& s=*b->impl;Check(s.next_pair==pair&&s.pending,"one pair active");
            b->Poll(Title);Check(s.enqueued_routes==2*(pair+1)&&e.instance.device.calls==pair,"pending pair never rerecords/reads queries");
            e.scheduler.completed=s.tick;b->Poll(Title);Check(s.next_pair==pair&&e.instance.device.calls==pair,"GPU alone cannot prove accepted submission");
            const auto trace=e.scheduler.Drain();
            Check(trace.draws==32&&trace.dispatches==32,"both complete route operation counts");
            Check(trace.packets.size()==32,"all immutable packet pushes");
            for(unsigned op=0;op<32;++op)Check(std::memcmp(&trace.packets[op],&initial[op&1],32)==0,"captured original packets");
            std::vector<Event> stamps;std::vector<std::size_t> uploads,reads,positions;
            for(std::size_t k=0;k<trace.events.size();++k){const auto& v=trace.events[k];if(v.kind=="stamp"){stamps.push_back(v);positions.push_back(k);}if(v.kind=="upload")uploads.push_back(k);if(v.kind=="readback")reads.push_back(k);}
            const unsigned first=pair&1;
            Check(stamps.size()==4&&stamps[0].a==pair*4+first*2&&stamps[1].a==stamps[0].a+1&&stamps[2].a==pair*4+(first^1)*2&&stamps[3].a==stamps[2].a+1,"ABBA disjoint query indices");
            Check(stamps[0].b==u32(vk::PipelineStageFlagBits::eTopOfPipe)&&
                  stamps[1].b==u32(vk::PipelineStageFlagBits::eBottomOfPipe)&&
                  stamps[2].b==u32(vk::PipelineStageFlagBits::eTopOfPipe)&&
                  stamps[3].b==u32(vk::PipelineStageFlagBits::eBottomOfPipe),"TOP BOTTOM timing stages");
            Check(uploads.size()==2&&reads.size()==2&&uploads[0]<positions[0]&&reads[0]>positions[1]&&uploads[1]<positions[2]&&reads[1]>positions[3],"reset/readback outside route stamps");
            Check(trace.buffer_barriers.size()==2,"both host readback barriers");
            for(const auto& barrier:trace.buffer_barriers)Check(barrier.srcAccessMask==vk::AccessFlagBits::eTransferWrite&&barrier.dstAccessMask==vk::AccessFlagBits::eHostRead,"host readback visibility");
            unsigned compute_in=0;
            for(const auto& barrier:trace.image_barriers)if(barrier.dstAccessMask&vk::AccessFlagBits::eShaderWrite){++compute_in;Check(bool(barrier.dstAccessMask&vk::AccessFlagBits::eShaderRead)==(Cases[pair/PairsPerCase].channels!=15),"compute RMW visibility");}
            Check(compute_in==32,"all production compute barriers");
            Check(e.scheduler.dirty==(StateFlags::Pipeline|StateFlags::DescriptorSets|StateFlags::FragmentConstants),"graphics state fully dirty");
            if(pair==0){
                // CodexAstraLocal: The new partial fragment route shares this
                // runtime transport. Retain the old generic restoration check
                // and require the same dirty-state repair after partial selection.
                for(const bool static_tev_cpu : {false,true}){
                // CodexAstraLocal: Execute the real next-graphics restore
                // branches with the SAME cached owner/dynamic/constants values.
                // Scratch work must force binding and pushes despite equality.
                Trace restore;RecordingCommands cmdbuf{&restore};
                const bool is_dirty=static_cast<u32>(e.scheduler.dirty&StateFlags::Pipeline)!=0;
                const bool constants_dirty=is_dirty||static_cast<u32>(e.scheduler.dirty&StateFlags::FragmentConstants)!=0;
                struct Pipeline {vk::Pipeline Handle()const{return H<vk::Pipeline>(201);}} pipeline;
                auto* selected=&pipeline;auto* bound_pipeline=&pipeline;
                struct Dynamic {Common::Rectangle<s32> viewport{0,0,64,32};Common::Rectangle<u32> scissor{0,32,64,0};};
                Dynamic dynamic,current_dynamic;
                ExactPushConstants<std::array<u32,32>> tev_push_constants;
                std::array<u32,32> constants{};const bool selected_fallback=!static_tev_cpu;
                Handle<vk::PipelineLayout> pipeline_layout{H<vk::PipelineLayout>(202)};
                tev_push_constants.UploadIfChanged(constants,[](const auto&){});
#include "graphics-restore.inc"
                Check(std::count_if(restore.events.begin(),restore.events.end(),[](const Event& v){return v.kind=="bind";})==1,"same graphics owner rebound after scratch");
                Check(std::count_if(restore.events.begin(),restore.events.end(),[](const Event& v){return v.kind=="viewport"||v.kind=="scissor";})==2,"equal dynamic viewport/scissor restored");
                Check(tev_push_constants.Stats().uploads==2,"same fragment constants reuploaded after scratch");
                }
            }
            e.Complete(*b);Clock::value+=std::chrono::milliseconds{1};++e.scheduler.current;b->Poll(Title);
            if(pair+1<CaseCount*PairsPerCase)initial=b->impl->workload.packets;
        }
        const auto result=e.Result();Check(result["complete"]==true&&result["pairs"].size()==32,"full64 only completes");
        Check(result["enqueued_routes"]==64&&result["submitted_routes"]==64&&result["completed_routes"]==64&&result["validated_routes"]==64,"64 lifecycle counts");
        for(const auto& row:result["pairs"])Check(row["compute_over_graphics"].is_number()&&row["timestamp_and_availability"].size()==8,"complete ratios/raw evidence");
        const auto calls=e.scheduler.records;b->Poll(Title);Check(calls==e.scheduler.records,"terminal complete stops work");
    }
    for(const std::string kind:{"deadline","title","run","pixel","unavailable","zero","reverse","querylost","flushlost","invalidatelost","recordfail","workerfail","writefail"}){
        Environment e(out,kind);e.Request();auto b=e.Load();bool terminal=false;
        if(kind=="flushlost")flush_result=VK_ERROR_DEVICE_LOST;
        if(kind=="recordfail")e.scheduler.fail_record=true;
        if(kind=="workerfail")e.scheduler.terminal_record=true;
        try{
            b->Poll(Title);
            if(kind=="deadline")Clock::value+=std::chrono::seconds{DeadlineSeconds};
            else if(kind=="run")Common::UberharActivity::session=74;
            else if(kind!="title"&&kind!="recordfail"&&kind!="workerfail"&&kind!="flushlost"){
                e.scheduler.Drain();e.Complete(*b);
                if(kind=="pixel")static_cast<u32*>(b->impl->readbacks[1].mapped)[0]^=1;
                if(kind=="unavailable")e.instance.device.available=false;
                if(kind=="zero")e.instance.device.zero=true;
                if(kind=="reverse")e.instance.device.reverse=true;
                if(kind=="querylost")e.instance.device.result=vk::Result::eErrorDeviceLost;
                if(kind=="invalidatelost")invalidate_result=VK_ERROR_DEVICE_LOST;
                if(kind=="writefail"){FileUtil::fail_write=true;Clock::value+=std::chrono::seconds{DeadlineSeconds};}
            }
            auto* identity=b->impl.get();const auto death_count=destroyed;
            b->Poll(kind=="title"?Title+1:Title);
            Check(b->impl.get()==identity&&destroyed==death_count,"failure keeps pending resource owner");
        }catch(const VideoCore::ShaderRecoveryError&){terminal=true;}
        const bool expected_terminal=kind=="querylost"||kind=="flushlost"||kind=="invalidatelost"||kind=="workerfail";
        Check(terminal==expected_terminal,"execution terminal error distinction");
        if(!terminal&&kind!="writefail"){
            const auto j=e.Result();Check(j["complete"]==false,"incomplete never qualifies");
            for(const auto& row:j["pairs"])Check(row["compute_over_graphics"].is_null(),"partial ratios withheld");
        }
        if(!terminal){auto n=e.scheduler.records;b->Poll(Title);Check(e.scheduler.records==n,"failed owner never retries");}
        const auto n=preparations;Check(!e.Load()&&preparations==n,"execution failure consumes request permanently");
    }
    // CodexAstraLocal: Force a partial result that otherwise has valid timings;
    // its ratio must still be withheld because the expected population is incomplete.
    {
        Environment e(out,"partial");e.Request();auto b=e.Load();b->Poll(Title);e.scheduler.Drain();e.Complete(*b);b->Poll(Title);
        b->FinishAfterDrain();const auto j=e.Result();Check(!j["complete"].get<bool>()&&j["pairs"].size()==1&&j["pairs"][0]["compute_over_graphics"].is_null(),"partial valid pair has no ratios");
    }
    Check(owners==0,"all test owners released after scope");
}
int main(int argc,char** argv){
    try{Check(argc==2,"output directory required");std::filesystem::path out=argv[1];std::filesystem::create_directories(out);Produce(out/"fixtures");OwnerTests(out/"owners");std::cout<<"PASS "<<checks<<" actual benchmark owner/command/data checks\n";}
    catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}

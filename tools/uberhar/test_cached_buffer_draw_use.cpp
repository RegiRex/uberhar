// CodexAstraLocal: Execute the actual accelerated enqueue body and no-target
// return with controlled endpoints. A true return without a draw must not stamp
// cached resources; real recording must stamp after all tick-changing setup.
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
using u8=unsigned char; using u32=unsigned; using s32=int; using u64=unsigned long long;
static unsigned checks{};
static void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
#define ASSERT(x) Require(bool(x),#x)
namespace vk {
using DeviceSize=u64;
struct CommandBuffer {
 unsigned* draws;
 template<class... T> void bindVertexBuffers(T&&...){}
 template<class... T> void drawIndexed(T&&...){++*draws;}
 template<class... T> void draw(T&&...){++*draws;}
};
}
struct Scheduler {
 u64 tick{1};unsigned draws{},records{};
 u64 CurrentTick()const{return tick;}
 template<class F>void Record(F&& f){++records;f(vk::CommandBuffer{&draws});}
};
struct Ring {
 Scheduler& scheduler;u64 last{};unsigned stamps{};
 void MarkDrawUse(){last=scheduler.CurrentTick();++stamps;}
};
struct Evidence {bool enabled{};explicit operator bool()const{return enabled;}void Recorded()const{}};
struct VertexCapture {
 bool attempt{},binding{},evidence{};
 bool HasAttempt()const{return attempt;}
 template<class... T>Evidence Commit(T&&...){return{evidence};}
};
struct ReadyPipeline {u64 Key()const{return 7;}};
struct HardwareVertex {std::array<u8,88> data;};
struct Packet {
 std::vector<HardwareVertex> vertices=std::vector<HardwareVertex>(3);
 std::vector<u8> bytes=std::vector<u8>(3*88,0x6d);
 const std::vector<u8>& HardwareBytes()const{return bytes;}
 u32 VertexCount()const{return vertices.size();}
};
// CodexAstraLocal: The production terminal type is represented only at this
// standalone recorder boundary; no application/log dependencies are linked.
namespace VideoCore {struct ShaderRecoveryError : std::runtime_error {using std::runtime_error::runtime_error;};}
struct Framebuffer {bool valid{};u64 Handle()const{return valid;}};
struct FramebufferHelper {mutable unsigned canceled{};void CancelInvalidation()const{++canceled;}};

// CodexAstraLocal: Only driver/optional-compiler endpoints are modeled. The
// actual production functions below decide whether and where a draw is queued.
struct RasterizerVulkan {
 Scheduler scheduler;
 Ring uniform_buffer{scheduler},texture_buffer{scheduler},texture_lf_buffer{scheduler};
 struct Regs {struct {u32 num_vertices{3};}pipeline;}regs;
 struct {int vs_setup{};}pica;
 struct {bool preferred{};}cpu_bridge;
 struct {struct {struct {u32 binding_count{1};}vertex_layout;}state;}pipeline_info;
 struct {u32 vs_input_index_min{};}vertex_info;
 std::array<u32,16> binding_offsets{};
 std::array<u64,16> vertex_buffers{};
 struct DrawParams {u32 vertex_count; s32 vertex_offset;u32 binding_count;std::array<u32,16> bindings;bool is_indexed;};
 ReadyPipeline ready_pipeline;
 ReadyPipeline* ready_vertex_pipeline{&ready_pipeline};
 bool async_shaders{true},ready_vertex_attempt{},bind_ready{true},bind_flush{},index_flush{};
 VertexCapture* vertex_capture{};
 struct Cache {
  RasterizerVulkan& r;
  template<class... T>bool BindPipeline(T&&...){if(r.bind_flush)++r.scheduler.tick;return r.bind_ready;}
  template<class... T>std::optional<int> CaptureVertexBinding(T&&...){return r.vertex_capture->binding?std::optional<int>{1}:std::nullopt;}
 }pipeline_cache{*this};
 void SetupIndexArray(){if(index_flush)++scheduler.tick;}
 bool AccelerateDrawBatchInternal(bool is_indexed);
 void MarkCachedShaderBuffersUsed();
 std::vector<HardwareVertex> vertex_batch;
 bool strict_compute{};
 struct {unsigned no_target{};}strict_compute_stats;
 bool NoTarget(bool packet){
  Framebuffer fb;const auto* framebuffer=&fb;FramebufferHelper fb_helper;
  std::shared_ptr<Packet> deferred=packet?std::make_shared<Packet>():nullptr;
#include "no-target.inc"
  throw std::runtime_error("no-target branch failed to return");
 }
};
#include "draw-use.inc"

int main(){
 try{
  unsigned cases{};
  for(bool ready_route:{false,true})for(bool pending:{false,true})
   for(bool indexed:{false,true})for(bool flush:{false,true})for(unsigned capture=0;capture<4;++capture){
    RasterizerVulkan r;r.ready_vertex_attempt=ready_route;r.bind_ready=!pending;
    r.index_flush=flush;r.bind_flush=flush;
    VertexCapture token{capture!=0,capture>=2,capture==3};r.vertex_capture=capture?&token:nullptr;
    const bool accepted=r.AccelerateDrawBatchInternal(indexed);
    if(pending){
     Require(accepted==!ready_route,"pending return contract unchanged");
     Require(r.scheduler.draws==0&&r.scheduler.records==0,"pending pipeline queues no draw");
     Require(r.uniform_buffer.stamps==0&&r.texture_buffer.stamps==0&&r.texture_lf_buffer.stamps==0,
             "no queued draw must not stamp");
    }else{
     Require(accepted&&r.scheduler.draws==1&&r.scheduler.records==1,"one actual accelerated draw");
     for(const auto* ring:{&r.uniform_buffer,&r.texture_buffer,&r.texture_lf_buffer}){
      Require(ring->stamps==1,"actual accelerated draw stamps each ring once");
      Require(ring->last==r.scheduler.CurrentTick(),"actual accelerated final tick");
     }
    }
    ++cases;
   }
  for(bool packet:{false,true})for(bool strict:{false,true}){
   RasterizerVulkan r;r.strict_compute=strict;
   bool returned{},terminal{};
   try{returned=r.NoTarget(packet);}catch(const VideoCore::ShaderRecoveryError&){terminal=true;}
   Require(strict?terminal&&!returned:returned&&!terminal,"no-target strict terminal or ordinary return");
   Require(r.scheduler.draws==0&&r.uniform_buffer.stamps==0&&r.texture_buffer.stamps==0&&r.texture_lf_buffer.stamps==0,
           "no-target queues no cached use");
   Require(r.vertex_batch.size()==(packet?3:0),"no-target retained packet bytes are not cleared on terminal failure");
   ++cases;
  }
  std::cout<<"PASS draw-use cases="<<cases<<" checks="<<checks<<'\n';
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 23;}
}

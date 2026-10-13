// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Exercise actual batch/pool/assembler and extracted production
// 88-byte append bodies with a forced late borrower, rollback and subsequent reuse.
#ifdef PREFIX_FORCE_CV
#include <sys/syscall.h>
#undef SYS_futex
#endif
#include <condition_variable>
#include <cfenv>
#include <iostream>
#include <mutex>
#include <thread>
#include <cstdlib>
#include "video_core/pica/uberhar_vertex_parallel_batch.h"
#include "video_core/pica/uberhar_vertex_append.h"
#include "video_core/rasterizer_accelerated.h"
using namespace Pica;
// CodexAstraLocal: Fail exactly the owner's next slot allocation to exercise
// real startup refusal without adding a production testing branch.
thread_local bool fail_next_allocation{};
void* operator new(std::size_t size) {
    if(fail_next_allocation){fail_next_allocation=false;throw std::bad_alloc{};}
    if(void* ptr=std::malloc(size?size:1)) return ptr;
    throw std::bad_alloc{};
}
void operator delete(void* ptr) noexcept {std::free(ptr);}
void operator delete(void* ptr,std::size_t) noexcept {std::free(ptr);}
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class,Level,const char*,unsigned,const char*,fmt::string_view f,const fmt::format_args& a) {
    throw std::runtime_error(fmt::vformat(f,a));
}
}
namespace VideoCore {
using Pica::f24;
struct Expose : RasterizerAccelerated { using RasterizerAccelerated::HardwareVertex; };
struct AppendSink {
    using HardwareVertex = Expose::HardwareVertex;
    std::vector<HardwareVertex> vertex_batch;
    bool BeginVertexAppend(u32, std::size_t&);
    void RollbackVertexAppend(std::size_t) noexcept;
    void AddTriangle(const OutputVertex&, const OutputVertex&, const OutputVertex&);
};
#include "actual_append.inc"
}
using VideoCore::AppendSink;
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
OutputVertex Value(u32 index) {
    std::array<float,24> lanes{};
    for(u32 i=0;i<24;++i) lanes[i] = float((index*7+i*3)%127+1)/32.0f;
    OutputVertex result; std::memcpy(&result,lanes.data(),sizeof(result)); return result;
}
void Seed(PrimitiveAssembler& a) {
    const auto ignore=[](const auto&,const auto&,const auto&){};
    a.SubmitVertex(Value(9000),ignore); a.SubmitVertex(Value(9001),ignore); a.Reset();
}
void Equal(const AppendSink& a,const AppendSink& b) {
    Need(a.vertex_batch.size()==b.vertex_batch.size(),"output size");
    Need(a.vertex_batch.empty() || std::memcmp(a.vertex_batch.data(),b.vertex_batch.data(),
         a.vertex_batch.size()*sizeof(AppendSink::HardwareVertex))==0,"hardware output bytes/order");
}

// CodexAstraLocal: One worker pauses after the first grain while the owner
// finishes other grains. Early append releases it; timeout is a failing control.
void Case(PipelineRegs::TriangleTopology topology, bool indexed, bool fail, bool later_chunk) {
    PrimitiveAssembler actual{topology}, expected{topology}; Seed(actual); Seed(expected);
    // CodexAstraLocal: Match initial unused bytes before comparing complete
    // rollback images; independently constructed objects can have different padding.
    std::memcpy(&expected, &actual, sizeof(actual));
    actual.SetWinding(); expected.SetWinding();
    AppendSink output, oracle;
    const u32 count=later_chunk?4608:512;
    const u32 failure_base=later_chunk?4096:0;
    std::mutex mutex;std::condition_variable cv;bool blocked{},released{};
    const auto owner=std::this_thread::get_id();
    const PrimitiveAssembler::TriangleHandler triangle=[&](const auto& a,const auto& b,const auto& c) {
        output.AddTriangle(a,b,c);
        std::lock_guard lock{mutex};
        if(blocked&&!released) {
            // CodexAstraLocal: These speculative append flags must disappear
            // after a later shader failure, while the shader's flags survive.
            std::feraiseexcept(FE_INVALID); released=true;cv.notify_all();
        }
    };
    const auto reference=[&](const auto& a,const auto& b,const auto& c){oracle.AddTriangle(a,b,c);};
    auto index=[&](u32 i){
        // CodexAstraLocal: The later chunk starts with still-live FIFO hits;
        // those inputs must be eligible before its first unfinished shader grain.
        if(indexed&&later_chunk&&i>=4096&&i<4098) return i-2;
        return indexed&&i%17==16?i-1:i;
    };
    NativeParallelBatch batch{2};Need(batch.Prepare(count),"prepare");
    VertexAppendTransaction transaction{actual,output,triangle};
    std::feclearexcept(FE_ALL_EXCEPT);
    bool threw{};
    try {
        batch.Run(count,indexed,index,[&](auto inputs,auto outputs) {
            const bool owner_thread=std::this_thread::get_id()==owner;
            const auto first=inputs.front().index;
            if(first>=failure_base) {
                std::unique_lock lock{mutex};
                if(!owner_thread && first>failure_base && !blocked) {
                    blocked=true;cv.notify_all();
                    Need(cv.wait_for(lock,std::chrono::seconds(3),[&]{return released;}),"prefix did not release late borrower");
                    if(fail){std::feraiseexcept(FE_DIVBYZERO);throw std::runtime_error("late shader");}
                } else if(owner_thread && !blocked) {
                    Need(cv.wait_for(lock,std::chrono::seconds(3),[&]{return blocked;}),"worker did not borrow");
                }
            }
            for(u32 i=0;i<inputs.size();++i)outputs[i]=Value(inputs[i].vertex);
        },transaction,true);
    } catch(const std::runtime_error& e) {Need(std::string_view{e.what()}=="late shader","unexpected failure");threw=true;}
    Need(blocked&&released,"actual early prefix required");Need(threw==fail,"late failure delivery");
    if(fail) {
        Need((std::fetestexcept(FE_INVALID)&FE_INVALID)==0,"speculative append FP retained");
        Need(std::fetestexcept(FE_DIVBYZERO)!=0,"shader FP lost");
    } else Need(batch.LastWork().prefix_inputs>0,"missing prefix accounting");
    for(u32 i=0;i<(fail?failure_base:count);++i) expected.SubmitVertex(Value(index(i)),reference);
    Equal(output,oracle);
    // CodexAstraLocal: A second ordinary batch exposes tail/winding restoration
    // and proves no worker retains the failed transaction or output generation.
    auto scalar=[&](const OutputVertex& v){actual.SubmitVertex(v,triangle);};
    batch.Run(300,false,[](u32 i){return i+10000;},[](auto in,auto out){
        for(u32 i=0;i<in.size();++i)out[i]=Value(in[i].vertex);
    },scalar);
    for(u32 i=0;i<300;++i)expected.SubmitVertex(Value(i+10000),reference);
    Equal(output,oracle);
    Need(std::memcmp(&actual,&expected,sizeof(actual))==0,"complete assembler rollback/state");
}

// CodexAstraLocal: No prepare/hook may execute when no worker can participate.
void OwnerOnly() {
    Common::Uberhar::ParallelWork pool{1};u32 processed{};
    auto result=pool.RunWithOwnerHook(512,64,[&](u32 a,u32 b){processed+=b-a;},
        []()->bool{throw std::runtime_error("owner-only prepare");},
        []{throw std::runtime_error("owner-only hook");});
    Need(processed==512&&result.worker_items==0,"owner-only output");
    // CodexAstraLocal: Requested workers can all fail to start. Setup must still
    // stay untouched, and setup refusal with live workers must execute exactly once.
    Common::Uberhar::ParallelWork unavailable{4};
    processed=0;
    fail_next_allocation=true;
    unavailable.RunWithOwnerHook(512,64,[&](u32 a,u32 b){processed+=b-a;},
        []()->bool{throw std::runtime_error("unavailable prepare");},
        []{throw std::runtime_error("unavailable hook");});
    Need(processed==512&&unavailable.CreatedWorkers()==0,"failed startup output");
    Common::Uberhar::ParallelWork refused{2};std::atomic<u32> total{};unsigned prepares{};
    refused.RunWithOwnerHook(512,64,[&](u32 a,u32 b){total.fetch_add(b-a);},
        [&]{++prepares;return false;}, []{throw std::runtime_error("refused hook");});
    Need(total==512&&prepares==1,"preparation refusal replayed/lost work");
}
int main() try {
    OwnerOnly();unsigned cases{};
    for(auto topology:{PipelineRegs::TriangleTopology::List,PipelineRegs::TriangleTopology::Strip,
                       PipelineRegs::TriangleTopology::Fan,PipelineRegs::TriangleTopology::Shader})
        for(bool indexed:{false,true})for(bool fail:{false,true})for(bool later:{false,true}){
            Case(topology,indexed,fail,later);++cases;
        }
    std::cout<<"PASS prefix cases="<<cases<<" owner-only rollback late-chunk reuse FP\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

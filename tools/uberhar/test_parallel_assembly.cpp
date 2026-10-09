// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Challenge ordered borrowing against actual inherited scalar
// assembly, including currently-unused serialized buffers and exception exits.
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"
using namespace Pica;
namespace Common::Log {
// CodexAstraLocal: Keep production assertion/log boundaries linkable in the
// finite fixture; unexpected errors are test failures, not silent omissions.
void Stop() {}
void FmtLogMessageImpl(Class,Level level,const char*,unsigned,const char*,fmt::string_view f,const fmt::format_args& a) {
    if(level>=Level::Error) throw std::runtime_error(fmt::vformat(f,a));
}
}
// CodexAstraLocal: Assertion counts and named failures distinguish an intended
// mutation rejection from a crash, timeout or unrelated compiler diagnostic.
u64 checks{};
void Need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}

// CodexAstraLocal: Initialize every synthetic byte before observing the two
// retained buffers. The archive access uses the actual private field order;
// raw buffer equality is stricter than serialized named-lane equality here.
OutputVertex Vertex(u32 ordinal) {
    std::array<float,24> values;
    for(u32 i=0;i<values.size();++i) values[i]=float(ordinal*37+i*3+1)/32.0f;
    OutputVertex result;std::memcpy(&result,values.data(),sizeof(result));return result;
}
struct Snapshot {
    std::vector<u8> bytes;
    template<class T>Snapshot& operator&(T& value) {
        if constexpr(std::is_same_v<T,std::array<OutputVertex,2>>) {
            for(auto& vertex:value)*this&vertex;
        }else{
            const auto* first=reinterpret_cast<const u8*>(&value);
            bytes.insert(bytes.end(),first,first+sizeof(T));
        }
        return *this;
    }
};
std::vector<u8> State(PrimitiveAssembler& a) {
    Snapshot s;boost::serialization::access::serialize(s,a,0);return s.bytes;
}
struct Thrown{};
void Equal(const std::vector<OutputVertex>& a,const std::vector<OutputVertex>& b) {
    Need(a.size()==b.size(),"triangle population");
    Need(a.empty()||std::memcmp(a.data(),b.data(),a.size()*sizeof(OutputVertex))==0,"triangle byte/order equality");
}

// CodexAstraLocal: Seed old buffer values, vary tails and winding, and compare
// both emitted values and final private state after normal or throwing handlers.
void Case(PipelineRegs::TriangleTopology topology,u32 tail,bool winding,u32 count,int throw_at) {
    PrimitiveAssembler scalar{topology},bulk{topology};
    const auto ignore=[](const auto&,const auto&,const auto&){};
    for(u32 i=0;i<2;++i){scalar.SubmitVertex(Vertex(900+i),ignore);bulk.SubmitVertex(Vertex(900+i),ignore);}
    scalar.Reset();bulk.Reset();
    for(u32 i=0;i<tail;++i){scalar.SubmitVertex(Vertex(700+i),ignore);bulk.SubmitVertex(Vertex(700+i),ignore);}
    if(winding){scalar.SetWinding();bulk.SetWinding();}
    std::vector<OutputVertex> vertices;vertices.reserve(count);
    for(u32 i=0;i<count;++i)vertices.push_back(Vertex(i));
    std::vector<OutputVertex> a,b;
    int acalls{},bcalls{};bool athrow{},bthrow{};
    auto callback=[&](auto& outputs,int& calls){return [&](const OutputVertex& x,const OutputVertex& y,const OutputVertex& z){
        outputs.push_back(x);outputs.push_back(y);outputs.push_back(z);
        if(calls++==throw_at)throw Thrown{};
    };};
    const PrimitiveAssembler::TriangleHandler ah=callback(a,acalls),bh=callback(b,bcalls);
    try{for(const auto& v:vertices)scalar.SubmitVertex(v,ah);}catch(const Thrown&){athrow=true;}
    try{bulk.SubmitOrdered(count,[&](u32 i)noexcept->const OutputVertex&{return vertices[i];},bh);}catch(const Thrown&){bthrow=true;}
    Need(athrow==bthrow,"exception point");Equal(a,b);Need(State(scalar)==State(bulk),"serialized/raw retained state");
    const auto next=[&](auto& out){return [&](const OutputVertex& x,const OutputVertex& y,const OutputVertex& z){out.push_back(x);out.push_back(y);out.push_back(z);};};
    for(u32 i=0;i<5;++i){scalar.SubmitVertex(Vertex(500+i),next(a));bulk.SubmitVertex(Vertex(500+i),next(b));}
    Equal(a,b);Need(State(scalar)==State(bulk),"continued tail/exception state");
}

// CodexAstraLocal: Persistent partitions include zero/one/two tails and storage
// boundaries. Each topology case retains externally issued winding in draw order.
void Partitions(PipelineRegs::TriangleTopology topology) {
    PrimitiveAssembler scalar{topology},bulk{topology};
    std::vector<OutputVertex> a,b,vertices;
    for(u32 i=0;i<9000;++i)vertices.push_back(Vertex(i));
    const auto h=[&](auto& out){return [&](const OutputVertex& x,const OutputVertex& y,const OutputVertex& z){out.push_back(x);out.push_back(y);out.push_back(z);};};
    // CodexAstraLocal: Define both unused buffer objects before state inspection.
    for(u32 i=0;i<2;++i){scalar.SubmitVertex(vertices[i],h(a));bulk.SubmitVertex(vertices[i],h(b));}
    scalar.Reset();bulk.Reset();a.clear();b.clear();
    u32 start{};
    for(u32 count:{0U,1U,2U,3U,4U,4095U,0U,4096U,5U}) {
        scalar.SetWinding();bulk.SetWinding();
        for(u32 i=0;i<count;++i)scalar.SubmitVertex(vertices[start+i],h(a));
        bulk.SubmitOrdered(count,[&](u32 i)noexcept->const OutputVertex&{return vertices[start+i];},h(b));
        Equal(a,b);Need(State(scalar)==State(bulk),"persistent partition state");start+=count;
    }
}
// CodexAstraLocal: Enumerate the complete finite topology/tail/winding matrix;
// storage-boundary cases and continuation after a throw use the same oracle.
int main()try{
    u32 cases{};
    for(auto topology:{PipelineRegs::TriangleTopology::List,PipelineRegs::TriangleTopology::Shader,
                       PipelineRegs::TriangleTopology::Strip,PipelineRegs::TriangleTopology::Fan}) {
        for(u32 tail:{0U,1U,2U})for(bool winding:{false,true}) {
            for(u32 count:{0U,1U,2U,3U,4U,5U,6U,7U,63U,64U,65U,255U,256U,257U,4095U,4096U,4097U}) {
                Case(topology,tail,winding,count,-1);++cases;
            }
            for(int event:{0,1,2,5}){Case(topology,tail,winding,24,event);++cases;}
        }
        Partitions(topology);
    }
    std::cout<<"{\"cases\":"<<cases<<",\"partitions\":36,\"checks\":"<<checks<<"}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

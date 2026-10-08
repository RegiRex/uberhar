// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Private bridge probe uses actual CPU conversion, assembly and trivial-VS generator.
#include <array>
#include <bit>
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
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*, fmt::string_view fmt_s, const fmt::format_args& a) {
    if (level>=Level::Error) throw std::runtime_error(fmt::vformat(fmt_s,a));
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
void Check(bool x,const char* s) { if(!x)throw std::runtime_error(s); }
void Binary(const std::filesystem::path& p,const void* data,size_t n) { std::ofstream s(p,std::ios::binary);s.write(static_cast<const char*>(data),n); Check(s.good(),"binary write"); }
// CodexAstraLocal: Compile-time complete-lane offsets rule out hidden padding/layout assumptions.
static_assert(sizeof(HV)==88);
static_assert(offsetof(HV,position)==0 && offsetof(HV,color)==16 && offsetof(HV,tex_coord0)==32 &&
              offsetof(HV,tex_coord1)==40 && offsetof(HV,tex_coord2)==48 && offsetof(HV,tex_coord0_w)==56 &&
              offsetof(HV,normquat)==60 && offsetof(HV,view)==76);
static_assert(sizeof(Pica::Shader::Generator::VSUniformData)==32);
static_assert(offsetof(Pica::Shader::Generator::VSUniformData,clip_coef)==16);
Pica::OutputVertex Make(unsigned index,bool fullscreen) {
    Pica::OutputVertex v{};
    constexpr std::array<float,4> ws{.75f,1.0f,1.5f,2.0f};
    constexpr float xs[4]={-1.f,1.f,-1.f,1.f},ys[4]={-1.f,-1.f,1.f,1.f};
    const float x=fullscreen?(index==1?3.f:-1.f):xs[index];
    const float y=fullscreen?(index==2?3.f:-1.f):ys[index];
    const float w=fullscreen?2.f:ws[index];
    v.pos={f24::FromFloat32(x*w),f24::FromFloat32(y*w),f24::FromFloat32(-.4f*w),f24::FromFloat32(w)};
    for(unsigned c=0;c<4;++c)v.color[c]=f24::FromFloat32(fullscreen?std::array{.4f,.6f,.8f,.5f}[c]:.2f+float((index+c)%4)*.2f);
    v.tc0={f24::FromFloat32(x*.75f+.5f),f24::FromFloat32(y*.75f+.5f)};
    v.tc1={f24::FromFloat32(y*.65f+.5f),f24::FromFloat32(x*.65f+.5f)};
    v.tc2={f24::FromFloat32(1-v.tc0.x.ToFloat32()),f24::FromFloat32(1-v.tc0.y.ToFloat32())};
    v.tc0_w=f24::FromFloat32(fullscreen?1.5f:.75f+.5f*index);
    const std::array<std::array<float,4>,4> qs{{{0,0,0,1},{0,-.707106769f,0,-.707106769f},{0,0,1,0},{.2f,.3f,.4f,.8f}}};
    for(unsigned c=0;c<4;++c)v.quat[c]=f24::FromFloat32(fullscreen?(c==3?1.f:0.f):qs[index][c]);
    v.view={f24::FromFloat32(fullscreen?.1f:.1f+.1f*index),f24::FromFloat32(fullscreen?.2f:-.2f+.15f*index),f24::FromFloat32(fullscreen?1.f:.8f+.1f*index)};
    return v;
}
int main(int argc,char** argv) {
    Check(argc==2,"output directory"); const std::filesystem::path out{argv[1]};std::filesystem::create_directories(out);
    J report;report["author"]="CodexAstraLocal";
    // CodexAstraLocal: Emit exact production layout values and complete generated trivial shaders.
    VideoCore::RasterizerAccelerated layout;layout.MakeSoftwareVertexLayout();
    report["stride"]=layout.software_layout.bindings[0].byte_count.Value();
    for(unsigned i=0;i<8;++i){const auto&a=layout.software_layout.attributes[i];report["attributes"].push_back({{"binding",a.binding.Value()},{"location",a.location.Value()},{"offset",a.offset.Value()},{"size",a.size.Value()},{"type",static_cast<unsigned>(a.type.Value())}});}
    for(bool clip:{false,true})std::ofstream(out/(clip?"trivial-clip.vert":"trivial.vert"))<<"#version 450\n#define VULKAN 1\n"<<Pica::Shader::Generator::GLSL::GenerateTrivialVertexShader(clip,true);
    // CodexAstraLocal: Real list/Shader/strip assembly retains reuse, winding and pending tails before AddTriangle.
    using Top=Pica::PipelineRegs::TriangleTopology;
    for(unsigned fixture=0;fixture<4;++fixture) {
        const bool fullscreen=fixture==0;
        std::array<Pica::OutputVertex,4> vertices;for(unsigned i=0;i<4;++i)vertices[i]=Make(i,fullscreen);
        VideoCore::RasterizerAccelerated sink;
        const Top topology=fixture==2?Top::Shader:fixture==3?Top::Strip:Top::List;
        Pica::PrimitiveAssembler assembly(topology);
        const std::vector<unsigned> indices=fullscreen?std::vector<unsigned>{0,1,2}:fixture==3?std::vector<unsigned>{0,1,2,3}:std::vector<unsigned>{0,1,2,2,1,3};
        const auto callback=[&](const auto&a,const auto&b,const auto&c){sink.AddTriangle(a,b,c);};
        for(unsigned i=0;i<indices.size();++i){if(fixture==2 && i==3)assembly.SetWinding();assembly.SubmitVertex(vertices[indices[i]],callback);}
        Check(sink.vertex_batch.size()==(fullscreen?3:6),"complete triangle count");
        if(fixture==1){Check(sink.vertex_batch[1].normquat.y>0 && sink.vertex_batch[4].normquat.y<0,"shared quaternion must differ per primitive");}
        Binary(out/("vertices-"+std::to_string(fixture)+".bin"),sink.vertex_batch.data(),sink.vertex_batch.size()*sizeof(HV));
        J row={{"id",fixture},{"count",sink.vertex_batch.size()},{"indices",indices},{"topology",static_cast<unsigned>(topology)},{"all_complete_triangles_retained",true}};
        report["fixtures"].push_back(row);
    }
    // CodexAstraLocal: Serialize semantic UBO fields into zero padding, not uninitialized object bytes.
    for(unsigned u=0;u<4;++u){
        Pica::Shader::Generator::VSUniformData uniform{};uniform.flip_viewport=u&1;uniform.enable_clip1=(u>>1)&1;
        uniform.clip_coef={1.f,.25f,0.f,.1f}; std::array<std::byte,32> bytes{};
        std::memcpy(bytes.data(),&uniform.enable_clip1,4);std::memcpy(bytes.data()+4,&uniform.flip_viewport,4);std::memcpy(bytes.data()+16,&uniform.clip_coef,16);
        Binary(out/("vs-uniform-"+std::to_string(u)+".bin"),bytes.data(),bytes.size());
    }
    std::ofstream(out/"abi.json")<<report.dump(2)<<'\n';std::cout<<"PASS actual88B CPU ABI, assembly, per-triangle sign and32B VS UBO\n";
}

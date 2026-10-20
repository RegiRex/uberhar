// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Synthetic whole-runner recipe gate. The same-source generic
// reference and actual legacy decoder independently check complete prepared input.
// Production interpreter/JIT/FIFO/output/assembler execute; memory mapping is modeled.
#include <array>
#include <bit>
#include <cstdio>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <json.hpp>
#include <nihstro/inline_assembly.h>
#include "common/logging/log.h"
#include "video_core/pica/vertex_loader.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_vertex_plan_cache.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_jit.h"
#include "video_core/shader/shader_jit_x64_compiler.h"
#include "inspector.h"
// CodexAstraLocal: Suppress ordinary production logs in this host fixture, but
// turn error-level messages into test failures so a hidden fallback cannot pass.
namespace Common::Log {
void Stop(){}
void FmtLogMessageImpl(Class,Level level,const char*,unsigned,const char*,fmt::string_view format,const fmt::format_args& args){
    if(level>=Level::Error)throw std::runtime_error(fmt::vformat(format,args));
}
}
using namespace Pica;using Json=nlohmann::json;
using O=nihstro::OpCode::Id;using D=nihstro::DestRegister;using S=nihstro::SourceRegister;
using Format=PipelineRegs::VertexAttributeFormat;using Topology=PipelineRegs::TriangleTopology;
using Semantic=RasterizerRegs::VSOutputAttributes::Semantic;
u64 assertions{};
void Check(bool condition,const char* text){++assertions;if(!condition)throw std::runtime_error(text);}
// CodexAstraLocal: Use the production bytecode assembler for arithmetic; explicitly
// constructed flow words have finite forward targets. All programs are synthetic.
struct Builder {
    ProgramCode program{};SwizzleData swizzle{};u32 pc{},words{};
    void Emit(const nihstro::InlineAsm& op) {
        const auto code=nihstro::InlineAsm::CompileToRawBinary({op});
        auto instruction=code.program.at(0);
        if(!code.swizzle_table.empty()) {
            const u32 pattern=code.swizzle_table.at(0).hex;
            u32 index=0;while(index<words&&swizzle[index]!=pattern)++index;
            if(index==words)swizzle[words++]=pattern;
            instruction.common.operand_desc_id=index;
        }
        program.at(pc++)=instruction.hex;
    }
    void Flow(O opcode,u32 destination,u32 uniform=0) {
        nihstro::Instruction instruction{};instruction.hex=0;instruction.opcode=opcode;
        instruction.flow_control.dest_offset=destination;
        instruction.flow_control.bool_uniform_id=uniform;
        program.at(pc++)=instruction.hex;
    }
};
void MakeProgram(ShaderSetup& setup,u32 kind) {
    Builder b;
    for(u32 entry:{0U,64U}) {
        b.pc=entry;const u32 input=entry?1:0;
        if(kind==0) {
            b.Emit({O::MOV,D::MakeOutput(0),S::MakeInput(input)});
            b.Emit({O::MOV,D::MakeOutput(1),S::MakeInput(1)});
            b.Emit({O::MOV,D::MakeOutput(2),S::MakeInput(2)});
        } else if(kind==1) {
            const std::array<const char*,4> masks{"x","y","z","w"};
            for(u32 row=0;row<4;++row)
                b.Emit({O::DP4,D::MakeTemporary(0),masks[row],S::MakeFloat(row),S::MakeInput(input)});
            for(u32 row=0;row<4;++row)
                b.Emit({O::DP4,D::MakeTemporary(1),masks[row],S::MakeFloat(4+row),S::MakeTemporary(0)});
            for(u32 row=0;row<4;++row)
                b.Emit({O::DP4,D::MakeOutput(0),masks[row],S::MakeFloat(8+row),S::MakeTemporary(1)});
            b.Emit({O::MOV,D::MakeOutput(1),S::MakeInput(1)});
            b.Emit({O::MOV,D::MakeOutput(2),S::MakeInput(2)});
        } else {
            b.Emit({O::MOVA,D{},"xy",S::MakeInput(4),"xy",S{},"",nihstro::InlineAsm::RelativeAddress::A1});
            b.Emit({O::CMP,D{},"xyzw",S::MakeInput(input),S::MakeFloat(0)});
            nihstro::Instruction cmp{b.program[b.pc-1]};
            cmp.common.compare_op.x=nihstro::Instruction::Common::CompareOpType::LessThan;
            cmp.common.compare_op.y=nihstro::Instruction::Common::CompareOpType::GreaterEqual;
            b.program[b.pc-1]=cmp.hex;
            b.Emit({O::ADD,D::MakeTemporary(0),"xyz",S::MakeTemporary(0),S::MakeInput(input)});
            b.Flow(O::JMPU,b.pc+2,0); // Runtime bool skips one partial temporary update.
            b.Emit({O::ADD,D::MakeTemporary(1),"xy",S::MakeTemporary(1),S::MakeInput(1)});
            b.Emit({O::MOV,D::MakeOutput(0),"xy",S::MakeTemporary(0)});
            b.Emit({O::MOV,D::MakeOutput(0),"zw",S::MakeInput(input)});
            b.Emit({O::MOV,D::MakeOutput(1),"x",S::MakeTemporary(1)});
            b.Emit({O::MOV,D::MakeOutput(2),"xyz",S::MakeInput(2)});
        }
        b.Emit({O::END});
    }
    setup.UpdateProgramCode(b.program,96);setup.UpdateSwizzleData(b.swizzle,b.words);
    for(u32 reg=0;reg<96;++reg)
        for(u32 lane=0;lane<4;++lane)
            setup.uniforms.f[reg][lane]=f24::FromFloat32(reg<12&&reg%4==lane?1.f:.03125f);
    setup.uniforms.b.fill(false);setup.uniforms.i.fill({0,0,0,0});
}

// CodexAstraLocal: Synthetic bytes use real retained format sequences, not copied
// game payloads. Layout4 introduces a live default; layout5 covers the missing sizes.
// These format sequences establish existence, never title workload weighting.
struct Fixture {
    static constexpr PAddr Base=0x14000000;
    static constexpr u32 Vertices=70000,Stride=192;
    std::array<std::vector<u8>,2> memory{std::vector<u8>(Vertices*Stride+512),std::vector<u8>(Vertices*Stride+512)};
    std::array<ShaderSetup,3> programs;
    std::array<u8,4096> indices8{};std::array<std::array<u16,4096>,3> indices16{};
    std::array<PipelineRegs,6> pipelines{};
    std::array<u32,6> attribute_counts{5,11,2,4,5,8};
    std::array<std::vector<std::pair<Format,u32>>,6> descriptions;
    PipelineRegs pipeline{};ShaderRegs regs{};RasterizerRegs raster{};AttributeBuffer defaults{};
    Memory::MemorySystem provider;PAddr current_base=Base;u32 layout{},bank{};u64 mappings{};
    std::size_t map_limit=Vertices*Stride+512;
    Fixture(){
        descriptions[0]={{Format::FLOAT,3},{Format::BYTE,3},{Format::SHORT,2},{Format::SHORT,2},{Format::UBYTE,4}};
        descriptions[1]={{Format::SHORT,3},{Format::BYTE,4},{Format::SHORT,2},{Format::UBYTE,2},{Format::SHORT,3},
            {Format::SHORT,4},{Format::SHORT,4},{Format::SHORT,4},{Format::SHORT,4},{Format::SHORT,4},{Format::SHORT,4}};
        descriptions[2]={{Format::FLOAT,3},{Format::FLOAT,2}};
        descriptions[3]={{Format::FLOAT,3},{Format::BYTE,3},{Format::SHORT,2},{Format::UBYTE,4}};
        descriptions[4]=descriptions[0];
        descriptions[5]={{Format::BYTE,1},{Format::UBYTE,2},{Format::SHORT,3},{Format::FLOAT,4},
            {Format::UBYTE,1},{Format::SHORT,2},{Format::FLOAT,3},{Format::BYTE,4}};
        for(u32 k=0;k<6;++k){std::array<u32,39> raw{};
            for(u32 a=0;a<attribute_counts[k];++a){auto[f,n]=descriptions[k][a];
                raw[1+a/8]|=(u32(f)|((n-1)<<2))<<((a%8)*4);
                // CodexAstraLocal: The four candidate layouts use actual retained
                // relative spacing, with synthetic bytes and fresh per-draw bases.
                static constexpr std::array<u32,4> strides{28,70,20,24};
                static constexpr std::array<std::array<u32,11>,4> offsets{{
                    {0,12,16,20,24}, {0,6,10,14,16,22,30,38,46,54,62},
                    {0,12}, {0,12,16,20}}};
                const u32 stride=k<4?strides[k]:Stride;
                const u32 offset=k<4?offsets[k][a]:a*16;
                raw[3+a*3]=offset;raw[4+a*3]=a;raw[5+a*3]=(stride<<16)|(1U<<28);
            }
            raw[2]|=(attribute_counts[k]-1)<<28;if(k==4)raw[2]|=1U<<(16+2);
            std::memcpy(&pipelines[k].vertex_attributes,raw.data(),sizeof(raw));
        }
        // CodexAstraLocal: Initialize raw bytes independently of format; the same
        // live span is decoded differently in mixed/churn populations. Values remain finite.
        for(u32 b=0;b<2;++b)for(u32 v=0;v<Vertices;++v)for(u32 a=0;a<12;++a)for(u32 c=0;c<4;++c){
            float value=(float((v*13+a*7+c*3+b)%61)-30.f)/32.f;
            std::memcpy(memory[b].data()+v*Stride+a*16+c*4,&value,4);
        }
        regs.output_mask.Assign(7);raster.vs_output_total.Assign(3);
        for(u32 a=0;a<3;++a){auto& m=raster.vs_output_attributes[a];
            m.map_x.Assign(Semantic(a*4));m.map_y.Assign(Semantic(a*4+1));m.map_z.Assign(Semantic(a*4+2));m.map_w.Assign(Semantic(a*4+3));}
        for(u32 k=0;k<3;++k)MakeProgram(programs[k],k);
        for(u32 i=0;i<4096;++i){indices8[i]=i%13;indices16[0][i]=(i*129)&65535;indices16[1][i]=i<64?i:i==64?0:i-1;indices16[2][i]=i%5;}
    }
    void SetDraw(u32 ordinal,u32 population,bool stress){
        layout=population<6?population:ordinal%(population==6?4:6);
        pipeline=pipelines[layout];bank=ordinal&1;current_base=Base+(ordinal%3)*0x02000000;
        pipeline.vertex_attributes.base_address.Assign(current_base/16);
        regs.max_input_attribute_index.Assign(attribute_counts[layout]-1);
        regs.input_attribute_to_register_map_low=0;regs.input_attribute_to_register_map_high=0;
        for(u32 a=0;a<attribute_counts[layout];++a){const u32 dst=stress&&ordinal%7==0?0:a;
            if(a<8)regs.input_attribute_to_register_map_low|=dst<<(a*4);
            else regs.input_attribute_to_register_map_high|=dst<<((a-8)*4);
            auto& loader=pipeline.vertex_attributes.attribute_loaders[a];
            // CodexAstraLocal: Only correctness stress uses odd addresses/stride;
            // ordinary fixture cases change bank, base, offset and register identity per draw.
            loader.data_offset.Assign(pipelines[layout].vertex_attributes.attribute_loaders[a].data_offset+
                (ordinal%2)*4+(stress&&ordinal%5==0?1:0));
            if(stress&&ordinal%11==0)loader.byte_count.Assign(0);
        }
        for(u32 a=0;a<16;++a)defaults[a]={f24::FromFloat32(float((a+ordinal)%7)),f24::FromFloat32(-float(a)),f24::Zero(),f24::One()};
        provider.Set(current_base,memory[bank]);map_limit=memory[bank].size();
        float live=0.25f+float(ordinal%9)/32.f;std::memcpy(memory[bank].data(),&live,4);
        for(auto& setup:programs){setup.uniforms.f[0][0]=f24::FromFloat32(.75f+(ordinal%5)*.03125f);setup.uniforms.b[0]=(ordinal&1)!=0;}
    }
    std::span<const u8> Map(PAddr address){++mappings;if(address<current_base||u64(address-current_base)>map_limit)return{};
        return{memory[bank].data()+(address-current_base),map_limit-(address-current_base)};}
};

// CodexAstraLocal: Keep the real virtual renderer boundary and source-extracted
// HardwareVertex/quaternion conversion; this sink stops before Vulkan upload.
struct TriangleSink {virtual~TriangleSink()=default;virtual void AddTriangle(const OutputVertex&,const OutputVertex&,const OutputVertex&)=0;};
struct Sink:TriangleSink{
#include "hardware-fields.inc"
    std::vector<HardwareVertex> vertex_batch;
    void AddTriangle(const OutputVertex&,const OutputVertex&,const OutputVertex&)override;
    static_assert(sizeof(HardwareVertex)==88);
};
#include "hardware-bodies.inc"
// CodexAstraLocal: Read serialized semantic state, never object/padding bytes.
// Initialize both otherwise-unused slots first so complete-state comparisons are defined.
void AppendVertex(std::vector<u32>& out, const OutputVertex& v) {
    const auto add = [&](const auto& field) {
        const auto* data = reinterpret_cast<const unsigned char*>(&field);
        for (std::size_t i = 0; i < sizeof(field); i += 4) {
            u32 word; std::memcpy(&word, data + i, 4); out.push_back(word);
        }
    };
    add(v.pos); add(v.quat); add(v.color); add(v.tc0); add(v.tc1);
    add(v.tc0_w); add(v.view); add(v.tc2);
}
struct ReadState {
    std::vector<u32> words;
    template <class T> ReadState& operator&(T& field) {
        if constexpr (std::is_same_v<T, std::array<OutputVertex, 2>>) {
            for (const auto& v : field) AppendVertex(words, v);
        } else {
            words.push_back(static_cast<u32>(field));
        }
        return *this;
    }
};
std::vector<u32> AssemblerState(PrimitiveAssembler& assembler) {
    ReadState reader;
    boost::serialization::access::serialize(reader, assembler, 0);
    return reader.words;
}
PrimitiveAssembler NewAssembler(Topology topology) {
    PrimitiveAssembler result;
    const PrimitiveAssembler::TriangleHandler unused = [](const auto&, const auto&, const auto&) {};
    OutputVertex v{}; result.SubmitVertex(v, unused); result.SubmitVertex(v, unused);
    result.Reconfigure(topology); return result;
}
std::vector<u32> StateWords(const ShaderUnit& unit){std::vector<u32> r;
    auto add=[&](const auto& v){auto n=r.size();r.resize(n+sizeof(v)/4);std::memcpy(r.data()+n,&v,sizeof(v));};
    add(unit.input);add(unit.temporary);add(unit.output);add(unit.address_registers);
    r.push_back(unit.conditional_code[0]);r.push_back(unit.conditional_code[1]);r.push_back(unit.output_bank);return r;}
// CodexAstraLocal: The actual production aggregate branch runs after this complete
// draw; its new buckets are compared with independent traced legacy invocations.
struct TransportTotals {
    u64 fused{}, legacy{};
    std::array<u64,5> recipes{};
};
struct Capture {std::vector<u32> outputs,triangles,states,misses;u64 legacy{},selected{},maps{},rescued{};
    TransportTotals transport{};
    void Clear(){outputs.clear();triangles.clear();states.clear();misses.clear();legacy=selected=maps=rescued=0;transport={};}};
// CodexAstraLocal: Semantic negative controls must change the whole-runner
// result; actual source mutations are compiled separately by the Python gate.
enum class Mutant{None,StaleSource,StaleDefault,ReverseAlias,LoseW,BypassBound};
// CodexAstraLocal: Build the actual loader/plan fresh, preserve live index-domain
// rescue and legacy escape, then execute one uninterrupted original FIFO draw.
// Every invocation records semantic state; there is no dormant timing mode.
template<bool Candidate>
__attribute__((noinline)) NativeVertexCounts OneDraw(ShaderEngine& engine,Fixture& f,NativeVertexPlanCache& plans,
    PrimitiveAssembler& assembler,TriangleSink& target,Capture& cap,u32 population,u32 kind,u32 ordinal,u32 count,u32 pattern,
    bool stress=false,Mutant mutant=Mutant::None,bool escaped=false){
    f.SetDraw(ordinal,population,stress);const VertexLoader loader(f.provider,f.pipeline);
    auto& setup=f.programs[kind];engine.SetupBatch(setup,(ordinal&1)?64:0);
    const auto& plan=plans.Get(f.regs,f.raster);const bool indexed=pattern!=3;const u32 offset=indexed?0:65536;
    const u32 maxdomain=pattern==0?255:65535;
    using InputPlan=std::conditional_t<Candidate,NativeVertexInputPlan,NativeVertexInputPlanReference>;
    InputPlan inputs;const auto old_maps=f.mappings;
    const auto prepare=[&](u32 maximum){
        const auto describe=[&](u32 a){return loader.DescribeNativeInput(a);};
        const auto map=[&](PAddr address){return f.Map(address);};
        if constexpr(Candidate)return inputs.Prepare(f.regs,loader.GetNumTotalAttributes(),f.current_base,maximum,describe,map,count!=0);
        else return inputs.Prepare(f.regs,loader.GetNumTotalAttributes(),f.current_base,maximum,describe,map);
    };
    // CodexAstraLocal: The fixture stores actual live 8/16-bit index arrays; values
    // are not precomputed converted vertices. The later mutation intentionally escapes a rescued span.
    const auto& indices8=f.indices8;
    auto& indices16=f.indices16[escaped?2:pattern==1?0:1];
    if(escaped)indices16[6]=1;
    if(escaped)f.map_limit=5*Fixture::Stride+512;
    const u32 maximum=indexed?maxdomain:offset+(count?count-1:0);
    auto ready=prepare(maximum);bool rescued=false;
    if(indexed&&(ready==InputPlan::Result::ShortMapping||ready==InputPlan::Result::AddressWrap)){
        u32 found=0;for(u32 i=0;i<count;++i)found=std::max(found,pattern==0?u32(indices8[i]):u32(indices16[i]));
        ready=prepare(found);rescued=ready==InputPlan::Result::Ready;}
    Check(ready==InputPlan::Result::Ready,"input Prepare failed");
    ShaderUnit unit;unit.output_bank=(ordinal&2)!=0;
    const auto context=count?engine.BindForDraw(setup):ShaderRunContext{};
    cap.Clear();cap.selected=InputPlanInspector::Selected(inputs);cap.maps=f.mappings-old_maps;cap.rescued=rescued;
    AttributeBuffer olddefaults{};std::array<u8,512> oldsource{};
    // CodexAstraLocal: Only stale-data controls need pre-write snapshots;
    // ordinary correctness draws read live bytes and defaults directly.
    if(mutant==Mutant::StaleDefault)olddefaults=f.defaults;
    if(mutant==Mutant::StaleSource)std::memcpy(oldsource.data(),f.memory[f.bank].data(),oldsource.size());
    u64 escaped_input_vertices=0; // CodexAstraLocal: Existing production escape count.
    const auto vertex_at=[&](u32 i)->u32{if(escaped&&i==6)indices16[i]=127;
        return !indexed?offset+i:pattern==0?u32(indices8[i]):u32(indices16[i]);};
    const PrimitiveAssembler::TriangleHandler triangle=[&](const OutputVertex&a,const OutputVertex&b,const OutputVertex&c){
        AppendVertex(cap.triangles,a);AppendVertex(cap.triangles,b);AppendVertex(cap.triangles,c);target.AddTriangle(a,b,c);};
    const auto run=[&](const auto& shader){
        const auto shade=[&]<bool Sample>(u32 vertex,u32 index){static_assert(!Sample);
            if(stress){float value=.5f+float(index%7)/32.f;
                std::memcpy(f.memory[f.bank].data(),&value,4);f.defaults[2].x=f24::FromFloat32(float(index+9));}
            // CodexAstraLocal: Copy before input writes, then decode the identical
            // live bytes/defaults through the actual legacy loader. This oracle
            // checks all input lanes, including untouched lanes, independently of
            // the generic and recipe paths sharing the same Convert template.
            ShaderUnit legacy_input_oracle = unit;
            if constexpr(Candidate) {
                if(mutant==Mutant::None) {
                    AttributeBuffer raw;
                    loader.LoadVertex(f.current_base,index,vertex,raw,f.defaults);
                    plan.LoadInput(legacy_input_oracle,raw);
                }
            }
            if(inputs.CanLoad(vertex)||mutant==Mutant::BypassBound){
                if(mutant==Mutant::StaleSource){std::array<u8,512> live;std::memcpy(live.data(),f.memory[f.bank].data(),512);
                    std::memcpy(f.memory[f.bank].data(),oldsource.data(),512);inputs.Load(unit,f.defaults,vertex);
                    std::memcpy(f.memory[f.bank].data(),live.data(),512);}
                else if(mutant==Mutant::StaleDefault)inputs.Load(unit,olddefaults,vertex);
                else if(mutant==Mutant::ReverseAlias)InputPlanInspector::Reverse(inputs,unit,f.defaults,vertex);
                else{inputs.Load(unit,f.defaults,vertex);if(mutant==Mutant::LoseW)InputPlanInspector::LoseW(inputs,unit);}
            }else{AttributeBuffer input;loader.LoadVertex(f.current_base,index,vertex,input,f.defaults);plan.LoadInput(unit,input);
                escaped_input_vertices += inputs.Ready();
                ++cap.legacy;}
            if constexpr(Candidate) {
                if(mutant==Mutant::None) {
                    bool identical=true;
                    for(u32 reg=0;reg<16;++reg)for(u32 lane=0;lane<4;++lane)
                        identical &= std::bit_cast<u32>(unit.input[reg][lane]) ==
                                     std::bit_cast<u32>(legacy_input_oracle.input[reg][lane]);
                    Check(identical,"actual legacy input oracle mismatch");
                }
            }
            shader(unit);cap.misses.push_back(index);const auto state=StateWords(unit);cap.states.insert(cap.states.end(),state.begin(),state.end());
            return plan.Convert(unit);
        };
        const auto submit=[&](const OutputVertex& output){AppendVertex(cap.outputs,output);assembler.SubmitVertex(output,triangle);};
        NativeVertexSamples unused;return RunNativeVertexBatch<false>(count,indexed,vertex_at,shade,submit,unused);
    };
    const auto result=context?run([&](ShaderUnit& s){context.Run(s);}):run([&](ShaderUnit&s){engine.Run(setup,s);});
    // CodexAstraLocal: Alias only names to execute the unchanged extracted
    // production branch. The baseline keeps its original two aggregates.
    const auto& input_plan=inputs;
    const auto& counts=result;
    auto& native_input_fused_vertices=cap.transport.fused;
    auto& native_input_legacy_vertices=cap.transport.legacy;
    auto& native_input_recipe_invocations=cap.transport.recipes;
    if constexpr(Candidate) {
#include "candidate-account.inc"
    } else {
#include "baseline-account.inc"
    }
    if constexpr(Candidate) {
        const u64 prepared=result.invocations-cap.legacy;
        const u64 sum=std::accumulate(cap.transport.recipes.begin(),cap.transport.recipes.end(),u64{});
        Check(sum==cap.transport.fused&&sum==prepared,"recipe conservation/escape accounting");
        constexpr std::array<u32,6> slots{1,2,3,4,0,0};
        const u32 expected_slot=count?slots[f.layout]:0;
        Check(inputs.RecipeSlot()==expected_slot,"actual prepared tag changed");
        for(u32 slot=0;slot<5;++slot)
            Check(cap.transport.recipes[slot]==(slot==expected_slot?prepared:0),"wrong executed recipe bucket");
    }
    return result;
}

// CodexAstraLocal: Empty/tail batches compare without passing null data pointers
// to memcmp; nonempty hardware data still requires exact byte equality.
bool Equal(const Capture&a,const Capture&b,const Sink&sa,const Sink&sb,PrimitiveAssembler&aa,PrimitiveAssembler&ab,
           NativeVertexCounts na,NativeVertexCounts nb){
    return na.invocations==nb.invocations&&na.hits==nb.hits&&a.outputs==b.outputs&&a.triangles==b.triangles&&
        a.states==b.states&&a.misses==b.misses&&a.legacy==b.legacy&&a.maps==b.maps&&a.rescued==b.rescued&&
        a.transport.fused==b.transport.fused&&a.transport.legacy==b.transport.legacy&&
        AssemblerState(aa)==AssemblerState(ab)&&sa.vertex_batch.size()==sb.vertex_batch.size()&&
        (sa.vertex_batch.empty()||std::memcmp(sa.vertex_batch.data(),sb.vertex_batch.data(),sa.vertex_batch.size()*sizeof(Sink::HardwareVertex))==0);
}
// CodexAstraLocal: Semantic comparisons cover both real engines; explicit shape
// selection assertions stop a candidate that accidentally always takes fallback.
Json Correctness(Fixture& f){u64 comparisons=0,selected=0;Json negatives=Json::array();
    for(bool jit:{false,true})for(u32 pop=0;pop<8;++pop)for(u32 pattern=0;pattern<4;++pattern){
        auto e0=CreateEngine(jit),e1=CreateEngine(jit);NativeVertexPlanCache p0,p1;Sink s0,s1;Capture c0,c1;
        auto a0=NewAssembler(Topology::List),a1=NewAssembler(Topology::List);u32 ordinal=1;
        for(auto top:{Topology::List,Topology::Shader,Topology::Strip,Topology::Fan}){
            a0.Reconfigure(top);a1.Reconfigure(top);
            for(u32 count:{0U,1U,2U,3U,63U,64U,65U,66U,96U,960U,4096U}){
                if(ordinal%3==0){a0.SetWinding();a1.SetWinding();}if(ordinal%13==0){a0.Reset();a1.Reset();}
                s0.vertex_batch.clear();s1.vertex_batch.clear();
                auto n0=OneDraw<false>(*e0,f,p0,a0,s0,c0,pop,ordinal%3,ordinal,count,pattern,true);
                auto n1=OneDraw<true>(*e1,f,p1,a1,s1,c1,pop,ordinal%3,ordinal,count,pattern,true);
                Check(Equal(c0,c1,s0,s1,a0,a1,n0,n1),"complete draw differs");
                const bool expected=count&&f.layout<4;Check((c1.selected!=0)==expected,"selection/fallback population wrong");
                selected+=c1.selected!=0;++comparisons;++ordinal;
            }
        }
    }
    for(auto m:{Mutant::StaleSource,Mutant::StaleDefault,Mutant::ReverseAlias,Mutant::LoseW,Mutant::BypassBound}){
        auto e0=CreateEngine(true),e1=CreateEngine(true);NativeVertexPlanCache p0,p1;Sink s0,s1;Capture c0,c1;
        auto a0=NewAssembler(Topology::Shader),a1=NewAssembler(Topology::Shader);
        const u32 pop=m==Mutant::StaleDefault?4:0;const u32 ordinal=m==Mutant::ReverseAlias?7:m==Mutant::StaleSource?22:m==Mutant::BypassBound?1:11;
        auto n0=OneDraw<false>(*e0,f,p0,a0,s0,c0,pop,0,ordinal,96,1,true,Mutant::None,m==Mutant::BypassBound);
        auto n1=OneDraw<true>(*e1,f,p1,a1,s1,c1,pop,0,ordinal,96,1,true,m,m==Mutant::BypassBound);
        Check(!Equal(c0,c1,s0,s1,a0,a1,n0,n1),"required negative control escaped");
        negatives.push_back({{"mutant",u32(m)},{"rejected",true},{"reference_legacy",c0.legacy},{"mutant_legacy",c1.legacy}});
    }
    // CodexAstraLocal: Escaping a rescued bound is also a positive whole-runner
    // comparison, with real legacy decoding and original mapped input transport.
    {auto e0=CreateEngine(true),e1=CreateEngine(true);NativeVertexPlanCache p0,p1;Sink s0,s1;Capture c0,c1;
        auto a0=NewAssembler(Topology::Shader),a1=NewAssembler(Topology::Shader);
        auto n0=OneDraw<false>(*e0,f,p0,a0,s0,c0,0,0,1,96,1,false,Mutant::None,true);
        auto n1=OneDraw<true>(*e1,f,p1,a1,s1,c1,0,0,1,96,1,false,Mutant::None,true);
        Check(Equal(c0,c1,s0,s1,a0,a1,n0,n1)&&c1.rescued&&c1.legacy==1,"live escape/reference contract");++comparisons;}
    return{{"named_families",64},{"whole_draw_comparisons",comparisons},{"selected_draws",selected},{"negative_controls",negatives}};
}

// CodexAstraLocal: Direct same-object sequences prove that selection belongs to
// the current prepared ops, including false nonempty gates and early failures.
Json InputContracts(Fixture& f){u64 checked=0;ShaderRegs regs{};regs.max_input_attribute_index.Assign(0);
    AttributeBuffer defaults{};std::array<u8,64> data{};NativeVertexInputPlan p;NativeVertexInputPlanReference reference;
    ShaderUnit a,b;
    const std::array<u32,12> bits{0U,0x80000000U,0x3f800000U,0xbf800000U,0x7f800000U,0xff800000U,
        0x7fc12345U,0x7f812345U,1U,0x80000001U,0x007fffffU,0x00800000U};
    for(auto format:{Format::BYTE,Format::UBYTE,Format::SHORT,Format::FLOAT})for(u32 n=1;n<=4;++n)
        for(u32 value:bits){for(u32 i=0;i<4;++i)std::memcpy(data.data()+1+i*4,&value,4);
            NativeInputAttribute d{1,0,n,format,false};const auto desc=[&](u32){return d;};
            const auto map=[&](PAddr address){return std::span<const u8>{data}.subspan(address);};
            Check(p.Prepare(regs,1,0,0,desc,map,true)==NativeVertexInputPlan::Result::Ready,"direct candidate prepare");
            Check(reference.Prepare(regs,1,0,0,desc,map)==NativeVertexInputPlanReference::Result::Ready,"direct reference prepare");
            Check(InputPlanInspector::Selected(p)==0&&p.RecipeSlot()==0,"unknown complete shape accepted");
            reference.Load(a,defaults,0);p.Load(b,defaults,0);
            Check(StateWords(a)==StateWords(b),"special input conversion changed");++checked;
        }
    const auto prepare=[&](PAddr base,u64 last,bool enabled){const VertexLoader loader(f.provider,f.pipeline);
        const auto desc=[&](u32 i){return loader.DescribeNativeInput(i);};const auto map=[&](PAddr v){return f.Map(v);};
        const auto candidate=p.Prepare(f.regs,loader.GetNumTotalAttributes(),base,last,desc,map,enabled);
        const auto inherited=reference.Prepare(f.regs,loader.GetNumTotalAttributes(),base,last,desc,map);
        Check(u32(candidate)==u32(inherited)&&p.Ready()==reference.Ready(),"prepare result changed");
        return candidate;
    };
    const auto compare=[&](u32 vertex){ShaderUnit inherited,candidate;
        reference.Load(inherited,f.defaults,vertex);p.Load(candidate,f.defaults,vertex);
        Check(StateWords(inherited)==StateWords(candidate),"current plan/default/alias transport changed");};
    for(u32 ordinal=0;ordinal<14;++ordinal)for(u32 pop:{0U,4U,5U,2U,1U,3U}){
        f.SetDraw(ordinal,pop,true);Check(prepare(f.current_base,255,true)==NativeVertexInputPlan::Result::Ready,"sequence prepare");
        Check(bool(InputPlanInspector::Selected(p))==(pop<4),"sequence selected wrong complete shape");
        Check(p.RecipeSlot()==(pop<4?pop+1:0),"sequence selected wrong recipe tag");
        compare(0);compare(17);
        // CodexAstraLocal: Selection stores no self pointer; value copies/moves
        // must load their own copied ops while sharing only the pinned backing data.
        auto copied=p;auto moved=std::move(copied);ShaderUnit expected,moved_unit;
        reference.Load(expected,f.defaults,17);moved.Load(moved_unit,f.defaults,17);
        Check(StateWords(expected)==StateWords(moved_unit),"copied/moved plan aliases wrong object");
        Check(prepare(f.current_base,255,false)==NativeVertexInputPlan::Result::Ready,"disabled prepare");
        Check(InputPlanInspector::Selected(p)==0&&p.RecipeSlot()==0,"disabled/empty gate retained earlier recipe");compare(17);
        Check(prepare(f.current_base,255,true)==NativeVertexInputPlan::Result::Ready,"re-enable prepare");
        f.map_limit=12;Check(prepare(f.current_base,255,true)==NativeVertexInputPlan::Result::ShortMapping,"short mapping status");
        Check(!p.Ready()&&!InputPlanInspector::Selected(p)&&p.RecipeSlot()==0,"failed mapping retained earlier recipe");
        f.map_limit=f.memory[f.bank].size();Check(prepare(f.current_base,255,true)==NativeVertexInputPlan::Result::Ready,"retry did not rebuild recipe");
        compare(17);Check(prepare(0xffffffffU,255,true)==NativeVertexInputPlan::Result::AddressWrap,"wrap status");
        Check(!p.Ready()&&!InputPlanInspector::Selected(p)&&p.RecipeSlot()==0,"wrap retained earlier recipe");++checked;
    }
    for(u32 value:bits){f.SetDraw(0,2,false);std::memcpy(f.memory[0].data(),&value,4);
        Check(prepare(f.current_base,0,true)==NativeVertexInputPlan::Result::Ready,"float3 setup");
        Check(InputPlanInspector::Selected(p),"selected special conversion not exercised");compare(0);++checked;}
    return{{"conversion_and_lifetime_cases",checked},{"same_object_copy_move_failure_disable_retry",true}};
}

// CodexAstraLocal: The finite gate produces correctness evidence only; no timing
// option or full-workload extrapolation accompanies this synthetic population.
int main(int argc,char**argv)try{
    if(argc!=2)throw std::runtime_error("report path required");
    Json report{{"author","CodexAstraLocal"},
        {"scope","same-source generic reference plus actual legacy-input oracle; production software components with modeled raw mapping; no complete PicaCore/MemoryRef/GPU/Thor execution"}};
    Fixture fixture;
    report["input_contracts"]=InputContracts(fixture);
    report["correctness"]=Correctness(fixture);
    report["assertions"]=assertions;
    std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';
    if(!out)throw std::runtime_error("report write failed");
    std::printf("PASS %llu full draws, %llu assertions, 5 semantic negative controls\n",
        static_cast<unsigned long long>(report["correctness"]["whole_draw_comparisons"].get<u64>()),
        static_cast<unsigned long long>(assertions));
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}

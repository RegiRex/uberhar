// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Execute the actual per-draw factory, extracted zero-count/diagnostic
// call sites and production FIFO/transport/assembler with live state. No GPU or device claim.
#include <array>
#include <bit>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
#include <nihstro/inline_assembly.h>
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/shader_setup.h" // CodexAstraLocal: Use the complete real setup type directly.
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/pica/uberhar_vertex_plan_cache.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_jit.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}
using namespace Pica;
using O=nihstro::OpCode::Id;
using D=nihstro::DestRegister;
using S=nihstro::SourceRegister;
using Format=PipelineRegs::VertexAttributeFormat;
using Topology=PipelineRegs::TriangleTopology;
using Semantic=RasterizerRegs::VSOutputAttributes::Semantic;
// CodexAstraLocal: Count semantic assertions and observe the real profiler macro
// using tiny call-count adapters, not a profiler storage/scheduling simulation.
u64 assertions{}, profile_enters{}, profile_leaves{};
void Check(bool condition,const char* text) { ++assertions; if(!condition) throw std::runtime_error(text); }
#if MICROPROFILE_ENABLED
// CodexAstraLocal: Production interpreter defines the shared GPU_Shader token.
MicroProfileToken MicroProfileGetToken(const char*,const char*,uint32_t,MicroProfileTokenType) { return 42; }
uint64_t MicroProfileEnter(MicroProfileToken token) { Check(token==42,"wrong profile token");return ++profile_enters; }
void MicroProfileLeave(MicroProfileToken token,uint64_t) { Check(token==42,"wrong profile leave token");++profile_leaves; }
#endif

// CodexAstraLocal: Observe real engine calls without replacing SetupBatch, JIT or
// interpreter execution. This detects accidental binding/shading of empty draws.
class CountingEngine final : public ShaderEngine {
public:
    explicit CountingEngine(bool jit):engine(CreateEngine(jit)) {}
    void SetupBatch(ShaderSetup& setup,u32 entry) override {engine->SetupBatch(setup,entry);}
    void Run(const ShaderSetup& setup,ShaderUnit& unit) const override {++runs;engine->Run(setup,unit);}
    ShaderRunContext BindForDraw(const ShaderSetup& setup) const override {++binds;return engine->BindForDraw(setup);}
    const char* EngineName() const override {return engine->EngineName();}
    mutable u64 binds{},runs{};
private:
    std::unique_ptr<ShaderEngine> engine;
};

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

struct Fixture {
    static constexpr PAddr Base=0x14000000;
    static constexpr u32 Vertices=70000,Stride=80;
    std::vector<u8> memory=std::vector<u8>(Vertices*Stride);
    std::array<NativeInputAttribute,5> attributes{};
    ShaderRegs regs{};RasterizerRegs raster{};AttributeBuffer defaults{};
    std::array<ShaderSetup,3> programs;
    u64 mappings{};
    Fixture(u32 seed,u32 layout) {
        regs.max_input_attribute_index.Assign(4);
        regs.input_attribute_to_register_map_low=0x43210;
        regs.output_mask.Assign(7);raster.vs_output_total.Assign(3);
        for(u32 a=0;a<3;++a) {
            auto& map=raster.vs_output_attributes[a];
            map.map_x.Assign(static_cast<Semantic>(a*4));map.map_y.Assign(static_cast<Semantic>(a*4+1));
            map.map_z.Assign(static_cast<Semantic>(a*4+2));map.map_w.Assign(static_cast<Semantic>(a*4+3));
        }
        for(u32 a=0;a<5;++a) {
            auto format=layout==0?Format::FLOAT:static_cast<Format>(a%4);
            // Address-register input remains a small live default in mixed layouts.
            attributes[a]={a*16,Stride,layout==2?1+a%4:4,format,a==4|| (layout==2&&a==2)};
        }
        std::mt19937 rng{seed};
        for(u32 v=0;v<Vertices;++v) for(u32 a=0;a<5;++a) {
            const auto& attr=attributes[a];auto* data=memory.data()+v*Stride+attr.offset;
            for(u32 c=0;c<attr.elements;++c) {
                const s32 raw=static_cast<s32>(rng()%257)-128;
                switch(attr.format) {
                case Format::BYTE: {s8 x=raw;std::memcpy(data+c,&x,1);break;}
                case Format::UBYTE: {u8 x=raw;std::memcpy(data+c,&x,1);break;}
                case Format::SHORT: {s16 x=raw*31;std::memcpy(data+c*2,&x,2);break;}
                case Format::FLOAT: {float x=raw/128.f;std::memcpy(data+c*4,&x,4);break;}
                }
            }
        }
        for(u32 k=0;k<3;++k)MakeProgram(programs[k],k);
    }
    void Change(u32 ordinal,ShaderSetup& setup) {
        defaults[4]={f24::FromFloat32(float(ordinal%4)),f24::FromFloat32(float(ordinal%3)),f24::Zero(),f24::One()};
        defaults[2]={f24::FromFloat32((ordinal%7)/8.f),f24::Zero(),f24::One(),f24::One()};
        setup.uniforms.f[0][0]=f24::FromFloat32(.75f+(ordinal%5)*.03125f);
        setup.uniforms.b[0]=(ordinal&1)!=0;
        // CodexAstraLocal: The same absolute update is applied before each route;
        // live backing bytes and default values must never become cached results.
        memory[0]=static_cast<u8>(ordinal);
    }
    std::span<const u8> Map(PAddr address) {
        ++mappings;
        if(address<Base||u64(address-Base)>memory.size())return {};
        return {memory.data()+(address-Base),memory.size()-(address-Base)};
    }
};

struct Buffers {
    std::vector<OutputVertex> vertices=std::vector<OutputVertex>(4096);
    std::vector<OutputVertex> triangles=std::vector<OutputVertex>(3 * 4098);
    u32 vertex_count{},triangle_count{};
    std::vector<u32> state;
    std::vector<u32> misses;
    void Reset() {vertex_count=triangle_count=0;state.clear();misses.clear();}
};
std::vector<u32> StateWords(const ShaderUnit& unit) {
    std::vector<u32> result;
    const auto append=[&](const auto& value) {
        const std::size_t old=result.size();result.resize(old+sizeof(value)/4);
        std::memcpy(result.data()+old,&value,sizeof(value));
    };
    append(unit.input);append(unit.temporary);append(unit.output);append(unit.address_registers);
    result.push_back(unit.conditional_code[0]);result.push_back(unit.conditional_code[1]);result.push_back(unit.output_bank);
    return result;
}

// CodexAstraLocal: Compare inherited dispatch, prepared ordinary dispatch and
// the actual extracted diagnostic adapter. Each full draw owns exactly one FIFO
// and live ShaderUnit, including untouched prefix/tail state and both output banks.
template<int Variant>
NativeVertexCounts OneDraw(CountingEngine& engine,Fixture& fixture,NativeVertexPlanCache& plans,
    PrimitiveAssembler& assembler,Buffers& buffers,u32 kind,u32 ordinal,u32 count,u32 pattern) {
    auto& setup=fixture.programs[kind];fixture.Change(ordinal,setup);
    const u32 entry=(ordinal&1)?64:0;
    engine.SetupBatch(setup,entry);
    const auto& plan=plans.Get(fixture.regs,fixture.raster);
    NativeVertexInputPlan inputs;
    const bool indexed=pattern!=3;
    const u32 offset=indexed?0:65536;
    const u32 maximum=indexed?65535:offset+(count?count-1:0);
    Check(inputs.Prepare(fixture.regs,5,Fixture::Base,maximum,
        [&](u32 a){return fixture.attributes[a];},[&](PAddr address){return fixture.Map(address);})==NativeVertexInputPlan::Result::Ready,"input plan failed");
    ShaderUnit unit;unit.output_bank=(ordinal&2)!=0;
    const u64 bind_before=engine.binds,run_before=engine.runs;
    const u64 enter_before=profile_enters,leave_before=profile_leaves;
    buffers.Reset();
    const auto vertex_at=[&](u32 i)->u32 {
        if(pattern==0)return i%13;
        if(pattern==1)return (i*129)&65535;
        if(pattern==2)return i%5==0?65535:(i/3)%67;
        return i+offset;
    };
    const PrimitiveAssembler::TriangleHandler sink=[&](const OutputVertex& a,const OutputVertex& b,const OutputVertex& c) {
        Check(buffers.triangle_count+3<=buffers.triangles.size(),"triangle capacity");
        buffers.triangles[buffers.triangle_count++]=a;buffers.triangles[buffers.triangle_count++]=b;buffers.triangles[buffers.triangle_count++]=c;
    };
    const auto submit=[&](const OutputVertex& out) {
        buffers.vertices[buffers.vertex_count++]=out;assembler.SubmitVertex(out,sink);
    };
    const auto execute=[&](const auto& run_shader) {
        const auto shade=[&]<bool Sample>(u32 vertex,u32 index) {
            static_assert(!Sample);inputs.Load(unit,fixture.defaults,vertex);
            run_shader(unit);buffers.misses.push_back(index);return plan.Convert(unit);
        };
        NativeVertexSamples unused;
        return RunNativeVertexBatch<false>(count,indexed,vertex_at,shade,submit,unused);
    };
    NativeVertexCounts counts{};bool prepared=false;
    if constexpr(Variant==0) {
        counts=execute([&](ShaderUnit& state){engine.Run(setup,state);});
    } else {
        // CodexAstraLocal: Test production call-site snippets using only modeled
        // enclosing draw/engine names; the factory, zero-count rule and adapter
        // bodies are read from PicaCore by the test driver rather than recopied.
        struct {u32 num_vertices;} pipeline{count};
        const auto* shader_engine=&engine;const auto& vs_setup=setup;
#include "draw_context_binding.inc"
        prepared=bool(context);
        const bool expect_prepared=count&&std::string_view(engine.EngineName())=="cpu_jit"&&!MICROPROFILE_ENABLED;
        Check(prepared==expect_prepared,"unexpected prepared/fallback policy");
        if constexpr(Variant==1) {
            counts=context ? execute([&](ShaderUnit& state){context.Run(state);})
                           : execute([&](ShaderUnit& state){engine.Run(setup,state);});
        } else {
#include "draw_diagnostic_dispatch.inc"
            counts=execute(run_diagnostic);
        }
    }
    Check(engine.binds-bind_before==u64(Variant!=0&&count!=0),"empty or duplicate draw binding");
    Check(engine.runs-run_before==(prepared?0:counts.invocations),"wrong inherited per-miss run coverage");
    if constexpr(MICROPROFILE_ENABLED) {
        Check(profile_enters-enter_before==counts.invocations,"profiler enter scope removed");
        Check(profile_leaves-leave_before==counts.invocations,"profiler leave scope removed");
    }
    buffers.state=StateWords(unit);return counts;
}
void Equal(const Buffers& a,const Buffers& b,NativeVertexCounts x,NativeVertexCounts y) {
    Check(x.hits==y.hits&&x.invocations==y.invocations,"FIFO counts differ");
    Check(a.vertex_count==b.vertex_count&&a.triangle_count==b.triangle_count,"submit counts differ");
    Check(std::memcmp(a.vertices.data(),b.vertices.data(),a.vertex_count*sizeof(OutputVertex))==0,"submitted vertices differ");
    Check(std::memcmp(a.triangles.data(),b.triangles.data(),a.triangle_count*sizeof(OutputVertex))==0,"triangle order/tails differ");
    Check(a.state==b.state&&a.misses==b.misses,"live shader state or miss invocation order differs");
}
// CodexAstraLocal: Rebind every changed entry/program/draw while preserving
// persistent assemblers across zero/short tails, FIFO eviction and winding flips.
u64 Equivalence(u32 seed) {
    u64 comparisons=0;
    for(bool jit:{false,true})for(u32 layout=0;layout<3;++layout) {
        Fixture f{seed,layout};CountingEngine a{jit},b{jit},c{jit};
        std::array<NativeVertexPlanCache,3> plans;
        for(auto topology:{Topology::List,Topology::Shader,Topology::Strip,Topology::Fan}) {
            std::array<PrimitiveAssembler,3> assemblers{PrimitiveAssembler{topology},PrimitiveAssembler{topology},PrimitiveAssembler{topology}};
            std::array<Buffers,3> buffers;u32 ordinal=0;
            for(u32 pattern=0;pattern<4;++pattern)for(u32 count:{0U,1U,2U,3U,63U,64U,65U,96U,960U,4096U}) {
                const u32 kind=ordinal%3;
                if(topology==Topology::Shader&&ordinal%4==0)for(auto& assembler:assemblers)assembler.SetWinding();
                const auto n0=OneDraw<0>(a,f,plans[0],assemblers[0],buffers[0],kind,ordinal,count,pattern);
                const auto n1=OneDraw<1>(b,f,plans[1],assemblers[1],buffers[1],kind,ordinal,count,pattern);
                const auto n2=OneDraw<2>(c,f,plans[2],assemblers[2],buffers[2],kind,ordinal,count,pattern);
                Equal(buffers[0],buffers[1],n0,n1);Equal(buffers[0],buffers[2],n0,n2);
                comparisons+=2;++ordinal;
            }
        }
    }
    return comparisons;
}

// CodexAstraLocal: Direct controls distinguish live borrowed uniforms and fresh
// entry/program rebinding from a superficially equivalent cached old invocation.
void ApiChecks() {
    static_assert(sizeof(ShaderRunContext)==3*sizeof(void*));
    ShaderSetup setup;MakeProgram(setup,1);
    auto interpreter=CreateEngine(false);interpreter->SetupBatch(setup,0);
    Check(!interpreter->BindForDraw(setup),"interpreter must retain inherited Run");
    auto jit=CreateEngine(true);ShaderSetup unprepared;
    Check(!jit->BindForDraw(unprepared),"unprepared setup unexpectedly bound");
    jit->SetupBatch(setup,0);const auto bound=jit->BindForDraw(setup);
    if constexpr(MICROPROFILE_ENABLED) {
        Check(!bound,"profiler must retain inherited Run");
        return;
    }
    Check(bool(bound),"prepared JIT did not bind");
    ShaderUnit initial,a,b;
    for(auto& input:initial.input)input={f24::One(),f24::One(),f24::One(),f24::One()};
    a.input=b.input=initial.input;jit->Run(setup,initial);
    setup.uniforms.f[0][0]=f24::FromFloat32(17.5f);
    jit->Run(setup,a);bound.Run(b);
    Check(StateWords(a)==StateWords(b),"bound call froze uniform contents");
    Check(StateWords(a)!=StateWords(initial),"uniform liveness control was ineffective");
    jit->SetupBatch(setup,64);const auto changed_entry=jit->BindForDraw(setup);
    Check(changed_entry.entry!=bound.entry,"new entry was not resolved");
    MakeProgram(setup,0);jit->SetupBatch(setup,64);
    const auto changed_program=jit->BindForDraw(setup);
    Check(changed_program.program!=bound.program,"new program was not rebound");
    a=ShaderUnit{};b=ShaderUnit{};a.input=b.input=initial.input;
    a.input[1]=b.input[1]=Common::Vec4<f24>::AssignToAll(f24::FromFloat32(2.5f));
    jit->Run(setup,a);changed_program.Run(b);
    Check(StateWords(a)==StateWords(b),"rebound entry/program execution differs");
}
int main() try {
    ApiChecks();const u64 comparisons=Equivalence(817261);
    Check(comparisons==1920,"equivalence population missing");
    Check(profile_enters==profile_leaves,"unbalanced profiler scope");
    std::printf("PASS: %llu full-draw comparisons; %llu assertions; profiler=%d enters=%llu leaves=%llu\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(assertions),
        MICROPROFILE_ENABLED,static_cast<unsigned long long>(profile_enters),static_cast<unsigned long long>(profile_leaves));
    return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}

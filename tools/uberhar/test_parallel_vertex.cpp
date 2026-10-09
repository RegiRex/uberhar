// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Synthetic certificate proof uses the actual interpreter and
// host x64 JIT. Grain resets vary carry and current inputs; no guest payload,
// Vulkan, complete PicaCore scheduler or A64 execution is modeled as tested here.
#include <array>
#include <bit>
#include <cfenv>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_parallel_vertex.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/pica/uberhar_vertex_parallel_batch.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit_x64_compiler.h"

// CodexAstraLocal: Only logging is adapted. Production errors become failures,
// so a hidden interpreter/JIT recovery cannot silently qualify a certificate.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}

namespace {
using namespace Pica;
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
using Pattern = nihstro::SwizzlePattern;
u64 checks{}, cases{}, comparisons{}, accepted{}, rejected{}, composed_draws{};
void Need(bool ok, const std::string& why) {
    ++checks;
    if (!ok) throw std::runtime_error(why);
}

// CodexAstraLocal: Encode actual ISA bitfields, including inverted operands and
// lane selectors. Descriptor deduplication respects the smaller MAD index field.
struct Fixture {
    ProgramCode code{};
    SwizzleData swizzles{};
    u32 cursor{}, descriptors{}, entry{}, output_mask{1};
    u16 booleans{};
    bool special_inputs{};
    bool flag_inputs{};
    Fixture() {
        nihstro::Instruction end{}; end.opcode = O::END;
        code.fill(end.hex);
    }
    void At(u32 pc) { cursor = pc; }
    void Arithmetic(O op, D dest, u8 mask, S a, S b = S::MakeInput(1),
                    S c = S::MakeInput(2), u32 relative = 0, u32 rotate = 0) {
        Pattern p{};
        for (u32 lane = 0; lane < 4; ++lane) {
            p.SetDestComponentEnabled(lane, (mask & (1U << lane)) != 0);
            p.SetSelectorSrc1(lane, static_cast<Pattern::Selector>((lane + rotate) & 3));
            p.SetSelectorSrc2(lane, static_cast<Pattern::Selector>(lane));
            p.SetSelectorSrc3(lane, static_cast<Pattern::Selector>(lane));
        }
        u32 descriptor{};
        while (descriptor < descriptors && swizzles[descriptor] != p.hex) ++descriptor;
        if (descriptor == descriptors) swizzles[descriptors++] = p.hex;
        nihstro::Instruction i{}; i.opcode = op;
        const auto info = nihstro::OpCode(op).GetInfo();
        const bool inverse = (info.subtype & nihstro::OpCode::Info::SrcInversed) != 0;
        if (info.type == nihstro::OpCode::Type::MultiplyAdd) {
            Need(descriptor < 32, "MAD descriptor bound");
            i.mad.dest = dest; i.mad.src1 = a; i.mad.operand_desc_id = descriptor;
            if (inverse) { i.mad.src2i = b; i.mad.src3i = c; }
            else { i.mad.src2 = b; i.mad.src3 = c; }
            i.mad.address_register_index = relative;
        } else {
            i.common.dest = dest; i.common.operand_desc_id = descriptor;
            if (inverse) { i.common.src1i = a; i.common.src2i = b; }
            else { i.common.src1 = a; i.common.src2 = b; }
            i.common.address_register_index = relative;
            if (op == O::CMP) {
                i.common.compare_op.x = nihstro::Instruction::Common::CompareOpType::LessThan;
                i.common.compare_op.y = nihstro::Instruction::Common::CompareOpType::GreaterEqual;
            }
        }
        code.at(cursor++) = i.hex;
    }
    void Flow(O op, u32 target = 0, u32 length = 0, u32 uniform = 0) {
        nihstro::Instruction i{}; i.opcode = op;
        i.flow_control.dest_offset = target; i.flow_control.num_instructions = length;
        i.flow_control.bool_uniform_id = uniform;
        if (op == O::IFC || op == O::CALLC || op == O::JMPC) {
            i.flow_control.op = nihstro::Instruction::FlowControlType::JustX;
            i.flow_control.refx = 1;
        }
        code.at(cursor++) = i.hex;
    }
    void Setup(ShaderSetup& setup) const {
        setup.UpdateProgramCode(code); setup.UpdateSwizzleData(swizzles);
        setup.entry_point = entry;
        for (u32 i = 0; i < 16; ++i) setup.uniforms.b[i] = (booleans & (1U << i)) != 0;
        for (u32 i = 0; i < 96; ++i) for (u32 lane = 0; lane < 4; ++lane)
            setup.uniforms.f[i][lane] = f24::FromFloat32(float(i + lane + 1) / 8);
        setup.uniforms.i = {};
    }
};

// CodexAstraLocal: Both banks and all non-input carry begin identically for a
// draw, but use many nonzero seeds. Inputs then change per invocation. Only the
// active selected output bytes are promised; unused final state is not compared.
ShaderUnit Seed(u32 seed, bool bank) {
    ShaderUnit state;
    state.output_bank = bank;
    for (u32 reg = 0; reg < 16; ++reg) for (u32 lane = 0; lane < 4; ++lane) {
        state.temporary[reg][lane] = f24::FromFloat32(float(1 + seed * 5 + reg + lane) / 16);
        state.input[reg][lane] = f24::FromFloat32(float(3 + seed + reg + lane) / 16);
        for (u32 output_bank = 0; output_bank < 2; ++output_bank)
            state.output[output_bank][reg][lane] = f24::FromFloat32(float(31 + seed * 2 + reg + lane + output_bank) / 16);
    }
    state.address_registers[0] = seed % 4;
    state.address_registers[1] = seed % 3;
    state.address_registers[2] = seed % 2;
    state.conditional_code[0] = seed & 1;
    state.conditional_code[1] = seed & 2;
    return state;
}
void Input(ShaderUnit& state, u32 vertex, bool special = false, bool flags = false) {
    for (u32 reg = 0; reg < 8; ++reg) for (u32 lane = 0; lane < 4; ++lane) {
        float value = float(1 + (vertex * 7 + reg * 3 + lane) % 17) / 4;
        if (reg == 0 && lane == 0) value = float(vertex % 4);
        state.input[reg][lane] = f24::FromFloat32(value);
    }
    // CodexAstraLocal: Bit transport includes signed zeros, infinities, NaNs and
    // subnormals; each engine is compared to its own serial arithmetic contract.
    if (special) {
        constexpr std::array<u32, 8> values{0, 0x80000000, 0x7f800000, 0xff800000,
                                          0x7fc12345, 0x7f800001, 1, 0x807fffff};
        state.input[1].x = f24::FromFloat32(std::bit_cast<float>(values[vertex % values.size()]));
    }
    // CodexAstraLocal: A discarded multiplication lane becomes 0*Inf only after
    // a prior invocation writes its temporary. Output-only analysis misses this
    // serial/grain exception-flag difference even though selected pixels match.
    if (flags) {
        state.input[1].y = f24::FromFloat32(std::bit_cast<float>(0x7f800000U));
        state.input[2].y = f24::Zero();
    }
}
using Output = std::array<u32, 64>;
Output Capture(const ShaderUnit& unit, u32 mask) {
    Output result{};
    for (u32 reg = 0; reg < 16; ++reg) if (mask & (1U << reg)) {
        std::memcpy(result.data() + reg * 4, &unit.output[unit.output_bank][reg], 16);
    }
    return result;
}

template<class Run>
void CompareGrains(const Fixture& fixture, const std::string& name, Run&& run) {
    for (u32 seed = 0; seed < 4; ++seed) for (bool bank : {false, true}) {
        const ShaderUnit initial = Seed(seed, bank);
        std::array<Output, 19> serial{};
        auto state = initial;
        std::feclearexcept(FE_ALL_EXCEPT);
        for (u32 vertex = 0; vertex < serial.size(); ++vertex) {
            Input(state, vertex, fixture.special_inputs, fixture.flag_inputs); run(state); serial[vertex] = Capture(state, fixture.output_mask);
        }
        const int serial_flags = std::fetestexcept(FE_ALL_EXCEPT);
        for (u32 grain : {1U, 2U, 7U}) {
            auto separate = initial;
            std::feclearexcept(FE_ALL_EXCEPT);
            for (u32 vertex = 0; vertex < serial.size(); ++vertex) {
                if (vertex % grain == 0) separate = initial;
                Input(separate, vertex, fixture.special_inputs, fixture.flag_inputs); run(separate);
                ++comparisons;
                Need(Capture(separate, fixture.output_mask) == serial[vertex],
                     "accepted certificate changes serial carry output: " + name);
            }
            Need(std::fetestexcept(FE_ALL_EXCEPT) == serial_flags,
                 "accepted certificate changes serial carry FP flags: " + name);
        }
    }
}
void Case(const Fixture& fixture, bool expected, const std::string& name, bool runnable = true) {
    ++cases;
    const auto proof = AnalyzeParallelVertex(fixture.code, fixture.swizzles, fixture.entry,
                                            fixture.booleans, fixture.output_mask);
    if (proof.Supported()) {
        ++accepted;
        if (runnable) {
            ShaderSetup setup; fixture.Setup(setup);
            Shader::InterpreterEngine interpreter; interpreter.SetupBatch(setup, fixture.entry);
            CompareGrains(fixture, name + "/interpreter", [&](ShaderUnit& state) { interpreter.Run(setup, state); });
            Shader::JitShader jit; jit.Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
            CompareGrains(fixture, name + "/x64", [&](ShaderUnit& state) { jit.Run(setup, state, fixture.entry); });
        }
    } else {
        ++rejected;
    }
    Need(proof.Supported() == expected, "certificate admission: " + name + " status=" + std::to_string(int(proof.status)) + " pc=" + std::to_string(proof.pc));
}

// CodexAstraLocal: Arithmetic cases independently exercise every recognized
// operation, all destination masks, rotated swizzles, self-alias and read order.
void ArithmeticCases() {
    constexpr std::array ops{O::MOV,O::FLR,O::ADD,O::MUL,O::MIN,O::MAX,O::SGE,O::SLT,
                            O::SGEI,O::SLTI,O::DP3,O::DP4,O::DPH,O::DPHI,
                            O::RCP,O::RSQ,O::EX2,O::LG2,O::MAD,O::MADI};
    for (O op : ops) for (u8 mask = 1; mask < 16; ++mask) {
        Fixture f;
        f.Arithmetic(O::MOV,D::MakeTemporary(0),15,S::MakeInput(0));
        f.Arithmetic(op,D::MakeTemporary(0),mask,S::MakeTemporary(0),S::MakeInput(1),S::MakeInput(2),0,mask%4);
        f.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeTemporary(0)); f.Flow(O::END);
        Case(f,true,"arithmetic/"+std::to_string(int(op))+"/"+std::to_string(mask));
    }
    Fixture immutable;
    immutable.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeTemporary(7)); immutable.Flow(O::END);
    Case(immutable,true,"never-written temp keeps draw seed");
    Fixture carry;
    carry.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeTemporary(0));
    carry.Arithmetic(O::MOV,D::MakeTemporary(0),15,S::MakeInput(0));carry.Flow(O::END);
    Case(carry,false,"mutable temp read before write");
    Fixture flags; flags.flag_inputs = true;
    flags.Arithmetic(O::MUL,D::MakeOutput(0),1,S::MakeTemporary(0),S::MakeInput(1));
    flags.Arithmetic(O::MOV,D::MakeTemporary(0),2,S::MakeInput(2));flags.Flow(O::END);
    Case(flags,false,"discarded SIMD lane carry changes flags");
    Fixture partial;
    partial.Arithmetic(O::MOV,D::MakeTemporary(0),7,S::MakeInput(0));
    partial.Arithmetic(O::DPH,D::MakeOutput(0),15,S::MakeTemporary(0),S::MakeInput(1));
    partial.Arithmetic(O::MOV,D::MakeTemporary(0),8,S::MakeInput(2));partial.Flow(O::END);
    Case(partial,false,"conservative full operand includes replaced DPH W");
    nihstro::Instruction i{partial.code[1]};i.opcode=O::DP4;partial.code[1]=i.hex;
    Case(partial,false,"DP4 consumes W carry");
    Fixture address;
    address.Arithmetic(O::MOVA,D{},3,S::MakeInput(0));
    address.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeFloat(4),S{},S{},1);address.Flow(O::END);
    Case(address,true,"defined relative address");
    std::swap(address.code[0],address.code[1]);
    Case(address,false,"relative address carry");
    for (O op : {O::MOV, O::MUL}) {
        Fixture special; special.special_inputs = true;
        special.Arithmetic(op,D::MakeOutput(0),15,S::MakeInput(1),S::MakeInput(0));special.Flow(O::END);
        Case(special,true,"special input bits");
    }
}

// CodexAstraLocal: Branch alternatives must intersect; Boolean flow uses the
// exact draw snapshot, and suffix-sharing calls are legitimate finite contexts.
void FlowCases() {
    for (u32 then_mask=0;then_mask<16;++then_mask) for (u32 else_mask=0;else_mask<16;++else_mask) {
        Fixture f;
        f.Arithmetic(O::CMP,D{},15,S::MakeInput(0),S::MakeFloat(4));
        f.Flow(O::IFC,4,1);
        f.Arithmetic(O::MOV,D::MakeTemporary(0),then_mask,S::MakeInput(1));
        f.Flow(O::NOP);
        f.Arithmetic(O::MOV,D::MakeTemporary(0),else_mask,S::MakeInput(2));
        f.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeTemporary(0));f.Flow(O::END);
        Case(f,then_mask==else_mask,"condition join/"+std::to_string(then_mask)+"/"+std::to_string(else_mask));
    }
    for(u16 booleans : {u16(0),u16(1)}) {
        Fixture f;f.booleans=booleans;
        f.Flow(O::IFU,3,1);f.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(0));f.Flow(O::NOP);
        f.Arithmetic(O::MOV,D::MakeOutput(0),3,S::MakeInput(1));f.Flow(O::END);
        Case(f,true,"uniform-selected path");
        Fixture back;back.booleans=booleans;back.entry=8;
        back.At(0);back.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(0));back.Flow(O::END);
        back.At(8);back.Flow(O::JMPU,0);back.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(1));back.Flow(O::END);
        Case(back,true,"acyclic backward uniform jump");
        Fixture keyed;keyed.booleans=booleans;
        keyed.Flow(O::IFU,3,0);keyed.Arithmetic(O::MOV,D::MakeTemporary(0),15,S::MakeInput(0));keyed.Flow(O::NOP);
        keyed.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeTemporary(0));
        keyed.Arithmetic(O::MOV,D::MakeTemporary(0),15,S::MakeInput(1));keyed.Flow(O::END);
        Case(keyed,booleans!=0,"uniform snapshot changes independence");
    }
    Fixture calls;
    calls.Flow(O::CALL,8,4);calls.Flow(O::CALL,10,2);calls.Flow(O::END);
    calls.At(8);calls.Arithmetic(O::MOV,D::MakeTemporary(0),15,S::MakeInput(0));calls.Flow(O::NOP);
    calls.Arithmetic(O::ADD,D::MakeOutput(0),15,S::MakeInput(1),S::MakeInput(2));calls.Flow(O::NOP);
    Case(calls,true,"overlapping suffix calls");
    Fixture output;
    output.Arithmetic(O::CMP,D{},15,S::MakeInput(0),S::MakeFloat(4));output.Flow(O::IFC,4,0);
    output.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(0));output.Flow(O::NOP);output.Flow(O::END);
    Case(output,false,"conditional output carry");
    Fixture condition;
    condition.Flow(O::IFC,3,1);condition.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(1));condition.Flow(O::NOP);
    condition.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(2));
    condition.Arithmetic(O::CMP,D{},15,S::MakeInput(0),S::MakeFloat(4));condition.Flow(O::END);
    Case(condition,false,"condition code carry");
    Fixture cycle;cycle.booleans=1;cycle.Flow(O::JMPU,0);Case(cycle,false,"cycle",false);
    Fixture loop;loop.Flow(O::LOOP,2);Case(loop,false,"loop serial",false);
    Fixture emit;emit.Flow(O::EMIT);Case(emit,false,"geometry serial",false);
    Fixture closure;closure.booleans=1;closure.Flow(O::CALL,8,1);closure.Flow(O::END);closure.At(8);closure.Flow(O::JMPU,9);
    Case(closure,false,"jump at call boundary",false);
    Fixture entered_if;entered_if.entry=2;entered_if.Flow(O::IFU,4,1);entered_if.Flow(O::NOP);entered_if.Flow(O::NOP);
    entered_if.Flow(O::NOP);entered_if.Flow(O::NOP);entered_if.Flow(O::END);
    Case(entered_if,false,"entry into compiled true body",false);

    // CodexAstraLocal: A false IF still transfers control and must not be
    // accepted where interpreter return closure would override that transfer.
    Fixture false_closure;false_closure.Flow(O::CALL,8,1);false_closure.Flow(O::END);
    false_closure.At(8);false_closure.Flow(O::IFU,10,1);false_closure.Flow(O::NOP);false_closure.Flow(O::NOP);
    Case(false_closure,false,"false IF at call boundary",false);
    Fixture selected;
    selected.Arithmetic(O::CMP,D{},15,S::MakeInput(0),S::MakeFloat(4));selected.Flow(O::IFC,4,0);
    selected.Arithmetic(O::MOV,D::MakeOutput(1),15,S::MakeInput(0));selected.Flow(O::NOP);selected.Flow(O::END);
    Case(selected,true,"unconsumed output carry is irrelevant");
    selected.output_mask=3;Case(selected,false,"output mask changes independence");

    // CodexAstraLocal: Work/stack limits and malformed instructions fail closed;
    // these programs are never executed merely to test bounded rejection.
    Fixture depth;
    depth.Flow(O::CALL,8,3);depth.Flow(O::END);
    depth.At(8);depth.Flow(O::CALL,16,3);depth.Flow(O::NOP);depth.Flow(O::NOP);
    depth.At(16);depth.Flow(O::CALL,24,3);depth.Flow(O::NOP);depth.Flow(O::NOP);
    depth.At(24);depth.Arithmetic(O::MOV,D::MakeOutput(0),15,S::MakeInput(0));depth.Flow(O::NOP);depth.Flow(O::NOP);
    Case(depth,true,"three call frames");
    depth.At(24);depth.Flow(O::CALL,32,3);depth.At(32);depth.Flow(O::NOP);depth.Flow(O::NOP);depth.Flow(O::NOP);
    Case(depth,false,"four call frames",false);
    Fixture wide;
    for(u32 i=0;i<50;++i)wide.Flow(O::CALL,1024,200);
    wide.Flow(O::END);wide.At(1024);for(u32 i=0;i<200;++i)wide.Flow(O::NOP);
    const auto bounded=AnalyzeParallelVertex(wide.code,wide.swizzles,0,0,1);
    Need(bounded.status==ParallelVertexStatus::WorkLimit && bounded.nodes==ParallelVertexDetail::MaxNodes,"bounded context graph");
    Fixture bad;bad.code[0]=0x14U<<26;Case(bad,false,"unknown opcode",false);
    Need(!AnalyzeParallelVertex(bad.code,{},0,0,1).Supported(),"empty descriptors rejected");
    Need(!AnalyzeParallelVertex(bad.code,bad.swizzles,4095,0,1).Supported(),"implicit END rejected");
    Need(!AnalyzeParallelVertex(bad.code,bad.swizzles,0,0,0x10000).Supported(),"output mask bounds");
    Uniforms uniforms{};for(u32 bit=0;bit<16;++bit){uniforms.b.fill(false);uniforms.b[bit]=true;
        Need(ParallelVertexBooleanUniforms(uniforms)==(1U<<bit),"exact uniform cache bits");}
}

// CodexAstraLocal: Share one real compiled program concurrently across private
// units. This checks the admitted call/data contract, not the production pool's
// scheduling, affinity, guest mapping or FP-environment propagation.
void ConcurrentCase() {
    Fixture f;
    f.Arithmetic(O::MOV,D::MakeTemporary(0),15,S::MakeInput(0));
    f.Arithmetic(O::MUL,D::MakeOutput(0),15,S::MakeTemporary(0),S::MakeInput(1));f.Flow(O::END);
    Need(AnalyzeParallelVertex(f.code,f.swizzles,0,0,1).Supported(),"concurrent certificate");
    ShaderSetup setup;f.Setup(setup);Shader::JitShader jit;jit.Compile(&setup.GetProgramCode(),&setup.GetSwizzleData());
    std::array<Output,129> expected{};
    ShaderUnit serial;
    for(u32 i=0;i<expected.size();++i){Input(serial,i);jit.Run(setup,serial,0);expected[i]=Capture(serial,1);}
    std::array<std::future<std::vector<Output>>,3> jobs;
    for(u32 job=0;job<jobs.size();++job)jobs[job]=std::async(std::launch::async,[&,job]{
        std::vector<Output> out;ShaderUnit unit;
        for(u32 i=job;i<expected.size();i+=jobs.size()){Input(unit,i);jit.Run(setup,unit,0);out.push_back(Capture(unit,1));}
        return out;
    });
    for(u32 job=0;job<jobs.size();++job){const auto actual=jobs[job].get();for(u32 n=0;n<actual.size();++n)
        Need(actual[n]==expected[job+n*jobs.size()],"concurrent outputs differ");}
}

// CodexAstraLocal: Compose the actual certificate, mapped input plan, x64 JIT,
// 64-entry FIFO, worker pool, output conversion and persistent assembler. This
// checks complete ordered output bytes against the inherited serial runner;
// the memory provider is an explicitly bounded host array, not guest pinning.
void ComposedBatchCases() {
    Fixture fixture;
    fixture.output_mask = 3;
    fixture.Arithmetic(O::MOV, D::MakeTemporary(0), 15, S::MakeInput(0));
    for (u32 i = 0; i < 12; ++i)
        fixture.Arithmetic(O::MAD, D::MakeTemporary(0), 15, S::MakeTemporary(0),
                           S::MakeFloat(0), S::MakeInput(1));
    fixture.Arithmetic(O::MOV, D::MakeOutput(0), 15, S::MakeTemporary(0));
    fixture.Arithmetic(O::MOV, D::MakeOutput(1), 15, S::MakeInput(2));
    fixture.Flow(O::END);
    Need(AnalyzeParallelVertex(fixture.code, fixture.swizzles, 0, 0, 3).Supported(),
         "composed shader certificate");
    ShaderSetup setup;
    fixture.Setup(setup);
    Shader::JitShader jit;
    jit.Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
    ShaderRegs shader{};
    shader.max_input_attribute_index.Assign(2);
    shader.input_attribute_to_register_map_low = 0x210;
    shader.output_mask.Assign(3);
    RasterizerRegs rasterizer{};
    rasterizer.vs_output_total.Assign(2);
    for (u32 attribute = 0; attribute < 2; ++attribute) {
        auto& map = rasterizer.vs_output_attributes[attribute];
        const u32 first = attribute == 0 ? 0 : 8;
        using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;
        map.map_x.Assign(static_cast<Semantic>(first));
        map.map_y.Assign(static_cast<Semantic>(first + 1));
        map.map_z.Assign(static_cast<Semantic>(first + 2));
        map.map_w.Assign(static_cast<Semantic>(first + 3));
    }
    NativeVertexPlan output{shader, rasterizer};
    Need(output.Supported(), "composed output plan");
    constexpr u32 VertexCount = 521;
    std::vector<float> storage(VertexCount * 12);
    AttributeBuffer defaults{};
    NativeVertexInputPlan input;
    const auto prepared = input.Prepare(shader, 3, 0, VertexCount - 1,
        [](u32 attribute) {
            return NativeInputAttribute{attribute * 16, 48, 4,
                                        PipelineRegs::VertexAttributeFormat::FLOAT, false};
        }, [&](PAddr address) {
            const auto bytes = std::as_bytes(std::span{storage});
            return std::span<const u8>{reinterpret_cast<const u8*>(bytes.data()) + address,
                                       bytes.size() - address};
        }, true);
    Need(prepared == NativeVertexInputPlan::Result::Ready, "composed input preparation");
    using Triangle = std::array<OutputVertex, 3>;
    const auto same = [](const auto& a, const auto& b) {
        return a.size() == b.size() && (a.empty() ||
            std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
    };
    for (u32 cores : {1U, 2U, 6U}) {
        NativeParallelBatch batch{cores};
        for (auto topology : {PipelineRegs::TriangleTopology::List,
                              PipelineRegs::TriangleTopology::Strip,
                              PipelineRegs::TriangleTopology::Fan,
                              PipelineRegs::TriangleTopology::Shader}) {
            PrimitiveAssembler serial_assembler{topology}, parallel_assembler{topology};
            for (bool indexed : {false, true}) {
                for (u32 count : {1U, 127U, 4095U, 4097U, 9001U}) {
                    ++composed_draws;
                    for (u32 i = 0; i < storage.size(); ++i)
                        storage[i] = float(1 + (i * 13 + composed_draws) % 31) / 32;
                    setup.uniforms.f[0] = Common::Vec4<f24>::AssignToAll(
                        f24::FromFloat32(float(3 + composed_draws % 5) / 8));
                    const auto vertex_at = [&](u32 i) {
                        return indexed ? (i % 193 < 96 ? i % 37 : (i * 71) % VertexCount)
                                       : i % VertexCount;
                    };
                    std::vector<OutputVertex> serial, parallel;
                    std::vector<Triangle> serial_triangles, parallel_triangles;
                    if (topology == PipelineRegs::TriangleTopology::Shader && composed_draws % 3 == 0) {
                        serial_assembler.SetWinding(); parallel_assembler.SetWinding();
                    }
                    ShaderUnit serial_unit;
                    NativeVertexSamples samples;
                    const auto expected = RunNativeVertexBatch<false>(count, indexed, vertex_at,
                        [&]<bool>(u32 vertex, u32) {
                            input.Load(serial_unit, defaults, vertex);
                            jit.Run(setup, serial_unit, 0);
                            return output.Convert(serial_unit);
                        }, [&](const OutputVertex& value) {
                            serial.push_back(value);
                            serial_assembler.SubmitVertex(value, [&](auto a, auto b, auto c) {
                                serial_triangles.push_back({a, b, c});
                            });
                        }, samples);
                    Need(batch.Prepare(count), "composed scratch preparation");
                    const auto actual = batch.Run(count, indexed, vertex_at,
                        [&](auto invocations, auto results) {
                            ShaderUnit unit;
                            for (u32 i = 0; i < invocations.size(); ++i) {
                                input.Load(unit, defaults, invocations[i].vertex);
                                jit.Run(setup, unit, 0);
                                results[i] = output.Convert(unit);
                            }
                        }, [&](const OutputVertex& value) {
                            parallel.push_back(value);
                            parallel_assembler.SubmitVertex(value, [&](auto a, auto b, auto c) {
                                parallel_triangles.push_back({a, b, c});
                            });
                        });
                    Need(same(serial, parallel), "composed output bytes differ");
                    Need(same(serial_triangles, parallel_triangles), "composed triangles differ");
                    Need(expected.invocations == actual.invocations && expected.hits == actual.hits,
                         "composed FIFO invocation counts differ");
                    Need(batch.LastWork().owner_invocations + batch.LastWork().worker_invocations ==
                             actual.invocations, "composed worker accounting differs");
                    Need(serial_assembler.IsEmpty() == parallel_assembler.IsEmpty() &&
                             serial_assembler.HasPendingWinding() == parallel_assembler.HasPendingWinding(),
                         "composed assembler tail differs");
                }
            }
        }
    }
}
} // namespace

int main() try {
    ArithmeticCases();FlowCases();ConcurrentCase();ComposedBatchCases();
    std::cout<<"PASS cases="<<cases<<" accepted="<<accepted<<" rejected="<<rejected
             <<" comparisons="<<comparisons<<" checks="<<checks
             <<" composed_draws="<<composed_draws
             <<" certificate_bytes="<<sizeof(ParallelVertexCertificate)<<'\n';
} catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;
}

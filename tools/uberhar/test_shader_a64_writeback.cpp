// CodexAstraLocal: Focused baseline/candidate execution of actual generated A64
// writeback. This regression uses no title/renderer/device or timing route.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nihstro/inline_assembly.h>
#include "video_core/pica/shader_unit.h"
#include "Baseline.h"
#include "Candidate.h"

using namespace Pica;
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
using B = Shader::Baseline::JitShader;
using C = Shader::Candidate::JitShader;
u64 assertions{}, comparisons{}, executions{}, programs{}, controls{};
void Check(bool v, const char* error) {
    ++assertions;
    if (!v) throw std::runtime_error(error);
}

// CodexAstraLocal: Exact layout for the existing sequential ABI bridge. All
// four wrappers tail-call the public real Run/BindForDraw entry, verified by host.
struct alignas(16) Observation {
    u64 before_sp, after_sp, expected_lr, after_lr;
    std::array<u64, 11> gprs;
    u64 padding;
    std::array<std::array<u64, 2>, 8> vectors;
    u64 before_fpcr, after_fpcr, before_fpsr, after_fpsr;
};
static_assert(sizeof(Observation) == 288 && offsetof(Observation, before_fpcr) == 256);
using Callback = void (*)(const void*, const ShaderSetup*, ShaderUnit*, u32);
extern "C" void InvokeWithAbiCheck(Callback, const void*, const ShaderSetup*, ShaderUnit*,
                                    u32, u64, Observation*);
extern "C" __attribute__((noinline)) void BaselineRun(
    const void* s, const ShaderSetup* setup, ShaderUnit* unit, u32 entry) {
    static_cast<const B*>(s)->Run(*setup, *unit, entry);
}
extern "C" __attribute__((noinline)) void CandidateRun(
    const void* s, const ShaderSetup* setup, ShaderUnit* unit, u32 entry) {
    static_cast<const C*>(s)->Run(*setup, *unit, entry);
}
extern "C" __attribute__((noinline)) void BaselineBound(
    const void* s, const ShaderSetup* setup, ShaderUnit* unit, u32 entry) {
    static_cast<const B*>(s)->BindForDraw(*setup, entry).Run(*unit);
}
extern "C" __attribute__((noinline)) void CandidateBound(
    const void* s, const ShaderSetup* setup, ShaderUnit* unit, u32 entry) {
    static_cast<const C*>(s)->BindForDraw(*setup, entry).Run(*unit);
}
u64 Fpcr() { u64 x; asm volatile("mrs %0, fpcr" : "=r"(x)); return x; }
u64 Fpsr() { u64 x; asm volatile("mrs %0, fpsr" : "=r"(x)); return x; }
void SetFp(u64 c, u64 s) {
    asm volatile("msr fpcr, %0\nmsr fpsr, %1\nisb" :: "r"(c), "r"(s) : "memory");
}
struct RestoreFp {
    u64 c = Fpcr(), s = Fpsr();
    ~RestoreFp() { SetFp(c, s); }
};
void CheckAbi(const Observation& o, u64 seed) {
    Check(o.before_sp == o.after_sp && (o.after_sp & 15) == 0, "stack contract");
    Check(o.expected_lr == o.after_lr, "LR contract");
    Check(o.gprs[0] == seed, "X19 contract");
    for (u32 i = 1; i < o.gprs.size(); ++i)
        Check(o.gprs[i] == u64(0x1f+i)*0x0101010101010101ULL, "GPR contract");
    for (u32 i = 0; i < o.vectors.size(); ++i) for (u32 half = 0; half < 2; ++half)
        Check(o.vectors[i][half] == u64(8+i)*0x0101010101010101ULL, "vector ABI contract");
    Check(o.before_fpcr == o.after_fpcr, "FPCR changed");
    Check((o.after_fpsr & o.before_fpsr) == o.before_fpsr, "sticky flags lost");
}

// CodexAstraLocal: Synthetic instructions exercise actual nihstro descriptor
// packing, including the separate MAD descriptor. No modeled writer executes.
struct Builder {
    ProgramCode code{};
    SwizzleData swizzle{};
    u32 descriptors{};
    Builder() {
        code.fill(static_cast<u32>(O::NOP) << 26);
        code.back() = static_cast<u32>(O::END) << 26;
    }
    void Op(u32 pc, const nihstro::InlineAsm& op, u32 mask = 15) {
        // CodexAstraLocal: The inherited inline binary helper omits MultiplyAdd
        // despite its constructor supporting it; retain the constructor's exact
        // bitfields and apply the same descriptor deduplication locally.
        nihstro::ShaderBinary assembled;
        if (op.full_instruction.instr.opcode.Value().GetInfo().type == nihstro::OpCode::Type::MultiplyAdd) {
            assembled.program.push_back(op.full_instruction.instr);
            assembled.swizzle_table.push_back(op.full_instruction.swizzle);
        } else assembled = nihstro::InlineAsm::CompileToRawBinary({op});
        auto instruction = assembled.program.at(0);
        if (!assembled.swizzle_table.empty()) {
            auto pattern = assembled.swizzle_table.at(0);
            pattern.dest_mask = mask;
            u32 id = 0;
            while (id < descriptors && swizzle[id] != pattern.hex) ++id;
            if (id == descriptors) swizzle.at(descriptors++) = pattern.hex;
            if (instruction.opcode.Value().GetInfo().type == nihstro::OpCode::Type::MultiplyAdd)
                instruction.mad.operand_desc_id = id;
            else instruction.common.operand_desc_id = id;
        }
        code.at(pc) = instruction.hex;
    }
    void End(u32 pc) { Op(pc, {O::END}); }
    void Emit(u32 pc, u32 vertex) {
        nihstro::Instruction instruction{};
        instruction.opcode = O::SETEMIT;
        instruction.setemit.vertex_id = vertex;
        code.at(pc) = instruction.hex;
        Op(pc+1, {O::EMIT});
    }
};
struct Spec { O op; u32 mask, index; bool output, emit; u32 entry; };
Builder Build(const Spec& s) {
    Builder b;
    const D dest = s.output ? D::MakeOutput(s.index) : D::MakeTemporary(s.index);
    const S a = !s.output && s.op == O::ADD ? S::MakeTemporary(s.index) : S::MakeInput(0);
    const S input = S::MakeInput(1);
    const u32 pc = s.entry;
    if (s.op == O::MOV) b.Op(pc, {s.op, dest, a, "wzyx"}, s.mask);
    else if (s.op == O::FLR || s.op == O::RCP || s.op == O::RSQ || s.op == O::EX2 || s.op == O::LG2)
        b.Op(pc, {s.op, dest, a}, s.mask);
    else if (s.op == O::MAD)
        b.Op(pc, {s.op, dest, a, input, S::MakeInput(2)}, s.mask);
    else b.Op(pc, {s.op, dest, a, input}, s.mask);
    if (s.emit) {
        b.Emit(pc+1, 0);
        b.Op(pc+3, {O::MOV, dest, S::MakeInput(1), "wzyx"}, s.mask);
        b.Emit(pc+4, 1);
        b.End(pc+6);
    } else if (!s.output) {
        // CodexAstraLocal: A full immediate read after a narrow store exercises
        // alias/order/byte preservation, without retaining a register cache.
        b.Op(pc+1, {O::MOV, D::MakeOutput(14), S::MakeTemporary(s.index)});
        b.End(pc+2);
    } else b.End(pc+1);
    return b;
}

// CodexAstraLocal: Seed semantic fields with finite values and exact raw special
// patterns; never compare padding or unrelated emitter pointer addresses.
u32 Pattern(u32 index, u32 seed) {
    static constexpr std::array<u32, 16> bits{0, 0x80000000, 1, 0x80000001,
        0x007fffff, 0x00800000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
        0x7fc12345, 0x7f812345, 0xffc54321, 0xff854321, 0x3f800000, 0xbf800000};
    if (seed == 0) return 0x3e000000U + index*0x1743U;
    if (seed == 1) return bits[index % bits.size()];
    return (index*0x9e3779b9U) ^ 0xa5b6123fU;
}
void Raw(f24& lane, u32 word) { std::memcpy(&lane, &word, 4); }
u32 Raw(const f24& lane) { u32 word; std::memcpy(&word, &lane, 4); return word; }
struct Holder {
    GeometryEmitter emitter{};
    Handlers handlers;
    ShaderUnit unit{&emitter};
    Holder(u32 seed, bool bank) {
        emitter.handlers = &handlers;
        emitter.output_mask = 0xffff;
        handlers.vertex_handler = [](const AttributeBuffer&) { throw std::runtime_error("unexpected primitive emit"); };
        handlers.winding_setter = [] { throw std::runtime_error("unexpected winding"); };
        for (u32 r = 0; r < 16; ++r) for (u32 lane = 0; lane < 4; ++lane) {
            Raw(unit.input[r][lane], Pattern(r*4+lane, seed));
            Raw(unit.temporary[r][lane], Pattern(67+r*4+lane, seed));
            for (u32 b = 0; b < 2; ++b)
                Raw(unit.output[b][r][lane], Pattern(133+b*67+r*4+lane, seed));
            for (u32 v = 0; v < 3; ++v)
                Raw(emitter.buffer[v][r][lane], Pattern(400+v*67+r*4+lane, seed));
        }
        // CodexAstraLocal: Even a mask-zero multiply must retain the invalid
        // exception from ordinary FMUL(0,Inf), following inherited FMULX.
        Raw(unit.input[0][0], 0); Raw(unit.input[1][0], 0x7f800000);
        unit.address_registers[0] = -5; unit.address_registers[1] = 11; unit.address_registers[2] = 3;
        unit.conditional_code[0] = seed & 1; unit.conditional_code[1] = !(seed & 1);
        unit.output_bank = bank;
    }
};
std::vector<u32> State(const Holder& h) {
    std::vector<u32> out;
    auto append = [&](const auto& value) {
        const auto start = out.size(); out.resize(start+sizeof(value)/4);
        std::memcpy(out.data()+start, &value, sizeof(value));
    };
    append(h.unit.input); append(h.unit.temporary); append(h.unit.output);
    append(h.unit.address_registers);
    out.push_back(h.unit.conditional_code[0]); out.push_back(h.unit.conditional_code[1]);
    out.push_back(h.unit.output_bank); out.push_back(h.emitter.emit_state.raw);
    append(h.emitter.buffer); out.push_back(h.emitter.output_mask);
    return out;
}
void RawOracle(const Holder& before, const Holder& after, const Spec& s) {
    if (s.op != O::MOV || s.emit) return;
    const auto& old = s.output ? before.unit.output[before.unit.output_bank][s.index] : before.unit.temporary[s.index];
    const auto& actual = s.output ? after.unit.output[before.unit.output_bank][s.index] : after.unit.temporary[s.index];
    for (u32 lane = 0; lane < 4; ++lane) {
        const u32 expected = (s.mask & (8U >> lane)) ? Raw(before.unit.input[0][3-lane]) : Raw(old[lane]);
        Check(Raw(actual[lane]) == expected, "independent raw MOV oracle");
    }
}

// CodexAstraLocal: Retain generated bytes and writeback offsets from both real
// compilers. This code observes; it does not replace emitted arithmetic.
template<class J>
void WriteCode(std::ofstream& assembly, std::ofstream& meta, const J& jit, u32 id,
               const char* name, const Spec& spec) {
    assembly << ".global " << name << '_' << id << "\n.type " << name << '_' << id
             << ", %function\n" << name << '_' << id << ":\n";
    for (u32 word : jit.ProbeCode()) assembly << ".inst " << word << '\n';
    assembly << ".size " << name << '_' << id << ", .-" << name << '_' << id << '\n';
    meta << "{\"id\":" << id << ",\"variant\":\"" << name << "\",\"opcode\":" << u32(spec.op)
         << ",\"mask\":" << spec.mask << ",\"output\":" << (spec.output ? "true" : "false")
         << ",\"index\":" << spec.index << ",\"emit\":" << (spec.emit ? "true" : "false")
         << ",\"words\":" << jit.ProbeCode().size() << ",\"writeback\":[";
    bool comma{};
    for (const auto& r : jit.ProbeRanges()) {
        if (comma) meta << ',';
        meta << '[' << r[0] << ',' << r[1] << ',' << r[2] << ']'; comma = true;
    }
    meta << "]}\n";
}
void CheckUnchanged(const B& b, const C& c, u32 mask) {
    Check(b.ProbeRanges().size() == c.ProbeRanges().size(), "writer count changed");
    for (std::size_t n = 0; n < b.ProbeRanges().size(); ++n) {
        const auto x = b.ProbeRanges()[n], y = c.ProbeRanges()[n];
        Check(x[0] == y[0], "writer identity changed");
        // CodexAstraLocal: Full stores and deliberately unselected masks retain
        // their identical emitted writer; only the first selected writer differs.
        if (n != 0 || !(mask == 0 || mask == 1 || mask == 2 || mask == 3 || mask == 4 || mask == 8 || mask == 12)) {
            if (n != 0 && b.ProbeRanges().size() > 1 && mask != 15) continue;
            std::vector<u32> old(b.ProbeCode().begin()+x[1]/4, b.ProbeCode().begin()+x[2]/4);
            std::vector<u32> now(c.ProbeCode().begin()+y[1]/4, c.ProbeCode().begin()+y[2]/4);
            Check(old == now, "inherited writer bytes changed");
        }
    }
}
void Pair(B& b, C& c, const Spec& spec, u32 id, u32 seed, bool bank, u64 mode,
          std::ofstream* trace = nullptr, int raw_rotation = -1) {
    Holder left(seed, bank), right(seed, bank), before(seed, bank);
    // CodexAstraLocal: Rotate every raw special payload through each enabled
    // MOV lane, including the second EMIT bank. No arithmetic conversion may
    // quiet signaling NaNs or erase signed zeros/subnormals in these stores.
    if (raw_rotation >= 0) {
        Check(spec.op == O::MOV, "raw payload cohort requires MOV");
        for (Holder* holder : {&left, &right, &before}) {
            for (u32 lane = 0; lane < 4; ++lane) {
                Raw(holder->unit.input[0][lane], Pattern(raw_rotation+lane, 1));
                Raw(holder->unit.input[1][lane], Pattern(raw_rotation+3-lane, 1));
            }
        }
    }
    ShaderSetup setup;
    Observation x{}, y{};
    const u64 abi_seed = 0xa123456789000000ULL + id;
    const u64 flags = seed == 2 ? 0x08000080 : 0;
    SetFp(mode, flags);
    InvokeWithAbiCheck(seed & 1 ? BaselineBound : BaselineRun, &b, &setup, &left.unit,
                       spec.entry, abi_seed, &x);
    SetFp(mode, flags);
    InvokeWithAbiCheck(seed & 1 ? CandidateBound : CandidateRun, &c, &setup, &right.unit,
                       spec.entry, abi_seed, &y);
    executions += 2;
    CheckAbi(x, abi_seed); CheckAbi(y, abi_seed);
    Check(x.after_fpcr == y.after_fpcr && x.after_fpcr == mode, "FPCR comparison");
    Check(x.after_fpsr == y.after_fpsr, "FPSR comparison");
    const auto a = State(left), d = State(right);
    Check(a == d, "raw state comparison");
    Check(left.unit.emitter_ptr == &left.emitter && right.unit.emitter_ptr == &right.emitter,
          "emitter ownership changed");
    RawOracle(before, left, spec); RawOracle(before, right, spec);
    if (spec.mask == 0 && spec.op == O::MUL) Check((x.after_fpsr & 1) != 0, "zero-mask arithmetic not executed");
    if (trace) {
        const std::array<u64, 8> header{id, seed, bank, mode, x.after_fpsr, y.after_fpsr, a.size(), u64(raw_rotation+1)};
        trace->write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        trace->write(reinterpret_cast<const char*>(a.data()), a.size()*4);
        trace->write(reinterpret_cast<const char*>(d.data()), d.size()*4);
    }
    ++comparisons;
}

// CodexAstraLocal: Deliberate defects patch actual candidate machine words
// through Oaknut-generated replacement instructions. Each must fail an exact
// state or FPSR assertion, not crash, timeout or an unrelated ABI check.
void Defect(u32 which) {
    using namespace oaknut;
    using namespace oaknut::util;
    Spec spec{which == 3 ? O::MUL : O::MOV, which == 3 ? 0U : 8U, 0, true, false, 0};
    auto source = Build(spec);
    B b; C c;
    b.Compile(&source.code, &source.swizzle); c.Compile(&source.code, &source.swizzle);
    const auto range = c.ProbeRanges().at(0);
    std::vector<u32> words;
    oaknut::VectorCodeGenerator gen(words);
    std::ptrdiff_t offset = range[1];
    if (which == 0) { gen.STR(S1, X4, 4); offset = range[2]-4; }
    if (which == 1) { gen.STR(Q1, X4); offset = range[2]-4; }
    if (which == 2) { gen.MOV(W5, WZR); offset = range[1]+4; }
    if (which == 3) {
        offset = c.ProbeLabel(spec.entry);
        while (offset+std::ptrdiff_t(words.size()*4) < range[1]) gen.NOP();
    }
    Check(!words.empty(), "empty defect patch");
    c.ProbeReplace(offset, words);
    try {
        Pair(b, c, spec, 9000+which, 1, true, 0);
        throw std::runtime_error("deliberate defect escaped");
    } catch (const std::runtime_error& error) {
        const std::string expected = which == 3 ? "FPSR comparison" : "raw state comparison";
        Check(error.what() == expected, "defect failed outside intended contract");
        std::printf("{\"defect\":%u,\"detected\":true,\"error\":\"%s\"}\n", which, error.what());
    }
    ++controls;
}

int main(int argc, char** argv) {
    RestoreFp restore;
    try {
        Check(argc == 2, "output directory required");
        const std::string directory = argv[1];
        std::ofstream assembly(directory+"/emitted.S"), meta(directory+"/emitted.jsonl");
        std::ofstream trace(directory+"/state-comparisons.bin", std::ios::binary);
        assembly << "// CodexAstraLocal: Observed generated A64 words, never executed from this object.\n.text\n.p2align 2\n";
        const std::array<u64, 6> modes{0, 1ULL<<22, 2ULL<<22, 3ULL<<22, 1ULL<<24, (1ULL<<24)|(1ULL<<25)};
        std::vector<Spec> specs;
        for (u32 mask = 0; mask < 16; ++mask) for (bool output : {false, true})
            for (u32 index : {0U, 15U}) for (O op : {O::MOV, O::ADD, O::MUL, O::SGE})
                specs.push_back({op, mask, index, output, false, index ? 7U : 0U});
        for (u32 mask = 0; mask < 16; ++mask)
            for (O op : {O::DP3, O::DP4, O::DPH, O::FLR, O::MAX, O::MIN, O::RCP, O::RSQ,
                         O::EX2, O::LG2, O::SLT, O::MAD})
                specs.push_back({op, mask, 15, true, false, 0});
        for (u32 mask = 0; mask < 16; ++mask) for (u32 index : {0U, 15U})
            specs.push_back({O::MOV, mask, index, true, true, 0});
        for (u32 id = 0; id < specs.size(); ++id) {
            const auto spec = specs[id];
            const auto shader = Build(spec);
            B b; C c;
            b.Compile(&shader.code, &shader.swizzle); c.Compile(&shader.code, &shader.swizzle);
            WriteCode(assembly, meta, b, id, "baseline", spec);
            WriteCode(assembly, meta, c, id, "candidate", spec);
            // CodexAstraLocal: Preserve the exact current generated program if
            // an inherited execution/fixture boundary faults before completion.
            assembly.flush(); meta.flush();
            CheckUnchanged(b, c, spec.mask);
            for (u64 mode : modes) for (bool bank : {false, true}) for (u32 seed = 0; seed < 3; ++seed)
                Pair(b, c, spec, id, seed, bank, mode, &trace);
            // CodexAstraLocal: The regular cohort covers six FPCR modes. This
            // separate raw MOV cohort spans all 16 payload rotations, both
            // banks/entry APIs, and ordinary versus DN+FZ control settings.
            if (spec.op == O::MOV) {
                for (u64 mode : {modes.front(), modes.back()})
                    for (bool bank : {false, true}) for (u32 seed : {1U, 2U})
                        for (int rotation = 0; rotation < 16; ++rotation)
                            Pair(b, c, spec, id, seed, bank, mode, &trace, rotation);
            }
            ++programs;
        }
        for (u32 defect = 0; defect < 4; ++defect) Defect(defect);
        assembly.close(); meta.close(); trace.close();
        Check(assembly.good() && meta.good() && trace.good(), "artifact write failure");
        std::printf("{\"passed\":true,\"programs\":%llu,\"paired_states\":%llu,\"executions\":%llu,\"assertions\":%llu,\"defects\":%llu,\"masks\":16,\"fpcr_modes\":6,\"raw_rotations\":16}\n",
            (unsigned long long)programs, (unsigned long long)comparisons, (unsigned long long)executions,
            (unsigned long long)assertions, (unsigned long long)controls);
        return 0;
    } catch (const std::exception& error) {
        std::printf("{\"passed\":false,\"programs\":%llu,\"error\":\"%s\"}\n",
                    (unsigned long long)programs, error.what());
        return 1;
    } catch (const char* error) {
        std::printf("{\"passed\":false,\"programs\":%llu,\"assembler_error\":\"%s\"}\n",
                    (unsigned long long)programs, error);
        return 1;
    }
}

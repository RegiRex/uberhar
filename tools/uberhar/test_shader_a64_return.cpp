// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Execute synthetic shaders through the real A64 JIT under
// bounded local QEMU. Compare its control flow with the production interpreter
// and observe the caller ABI before the test bridge restores its own frame.
#include <array>
#include <barrier>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <nihstro/inline_assembly.h>
#include "common/uberhar_parallel_work.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit_a64_compiler.h"

using namespace Pica;
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
struct alignas(16) Observation {
    u64 before_sp, after_sp, expected_lr, after_lr;
    std::array<u64, 11> gprs;
    u64 padding;
    std::array<std::array<u64, 2>, 8> vectors;
    u64 before_fpcr, after_fpcr, before_fpsr, after_fpsr;
};
static_assert(offsetof(Observation, gprs) == 32);
static_assert(offsetof(Observation, vectors) == 128);
static_assert(offsetof(Observation, before_fpcr) == 256);
static_assert(sizeof(Observation) == 288);

// CodexAstraLocal: Both wrappers use public production entry points. The driver
// verifies their emitted tail-call paths before relying on seeded caller X19.
extern "C" __attribute__((noinline)) void PublicRun(
    const Shader::JitShader* shader, const ShaderSetup* setup, ShaderUnit* state, u32 entry) {
    shader->Run(*setup, *state, entry);
}
extern "C" __attribute__((noinline)) void PublicBoundRun(
    const Shader::JitShader* shader, const ShaderSetup* setup, ShaderUnit* state, u32 entry) {
    shader->BindForDraw(*setup, entry).Run(*state);
}
using Callback = decltype(&PublicRun);
extern "C" void InvokeWithAbiCheck(Callback, const Shader::JitShader*, const ShaderSetup*,
                                    ShaderUnit*, u32, u64, Observation*);
u64 assertions{};
void Check(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}

// CodexAstraLocal: Assemble finite synthetic programs, with separate ranges for
// subroutines and deliberately unused CALLs. Arithmetic uses the real assembler.
struct Builder {
    ProgramCode program{};
    SwizzleData swizzle{};
    u32 words{};
    bool uses_helpers{};
    Builder() {
        program.fill(static_cast<u32>(O::NOP) << 26);
        program.back() = static_cast<u32>(O::END) << 26;
    }
    void Emit(u32 pc, const nihstro::InlineAsm& op) {
        const auto code = nihstro::InlineAsm::CompileToRawBinary({op});
        auto instruction = code.program.at(0);
        if (!code.swizzle_table.empty()) {
            const u32 pattern = code.swizzle_table.at(0).hex;
            u32 index = 0;
            while (index < words && swizzle[index] != pattern) ++index;
            if (index == words) swizzle.at(words++) = pattern;
            instruction.common.operand_desc_id = index;
        }
        program.at(pc) = instruction.hex;
    }
    void Add(u32 pc) { Emit(pc, {O::ADD, D::MakeTemporary(0), S::MakeTemporary(0), S::MakeInput(0)}); }
    void Store(u32 pc) { Emit(pc, {O::MOV, D::MakeOutput(0), S::MakeTemporary(0)}); }
    void End(u32 pc) { Emit(pc, {O::END}); }
    void Flow(u32 pc, O opcode, u32 destination, u32 length = 0, u32 uniform = 0) {
        nihstro::Instruction instruction{};
        instruction.opcode = opcode;
        instruction.flow_control.dest_offset = destination;
        instruction.flow_control.num_instructions = length;
        if (opcode == O::CALLC || opcode == O::IFC || opcode == O::BREAKC) {
            instruction.flow_control.op = nihstro::Instruction::FlowControlType::JustX;
            instruction.flow_control.refx = true;
        } else if (opcode == O::LOOP) {
            instruction.flow_control.int_uniform_id = uniform;
        } else {
            instruction.flow_control.bool_uniform_id = uniform;
        }
        program.at(pc) = instruction.hex;
    }
    void Helpers(u32 pc) {
        uses_helpers = true;
        Emit(pc, {O::EX2, D::MakeTemporary(14), S::MakeInput(1)});
        Emit(pc + 1, {O::LG2, D::MakeTemporary(15), S::MakeInput(2)});
        // CodexAstraLocal: EX2(0) and LG2(1) make helper calls observable. Their
        // inherited approximations receive the explicit fixture tolerance below.
        Emit(pc + 2, {O::MOV, D::MakeOutput(1), S::MakeTemporary(14)});
        Emit(pc + 3, {O::MOV, D::MakeOutput(2), S::MakeTemporary(15)});
    }
};
struct Case {
    std::string name;
    Builder b;
    u32 entry{};
    std::array<u32, 2> adds{};
    bool nested_loop{};
};
std::vector<Case> Cases() {
    std::vector<Case> cases;
    const auto make = [&](const char* name, u32 a = 0, u32 b = 0) -> Case& {
        cases.push_back({name, {}, 0, {a, b}, false});
        return cases.back();
    };
    { auto& c = make("main_unused_call"); c.b.End(3); c.b.Flow(200, O::CALL, 0, 3); }
    { auto& c = make("plain_main", 1, 1); c.b.Add(0); c.b.Store(1); c.b.End(2); }
    { auto& c = make("main_interior_boundary", 2, 2); c.b.Add(0); c.b.Add(3);
      c.b.Store(4); c.b.End(5); c.b.Flow(200, O::CALL, 0, 3); }
    { auto& c = make("direct_boundary_entry"); c.entry = 3; c.b.End(3); c.b.Flow(200, O::CALL, 0, 3); }
    { auto& c = make("arbitrary_entry", 2, 2); c.entry = 32; c.b.Add(32); c.b.Add(34);
      c.b.Store(35); c.b.End(36); c.b.Flow(200, O::CALL, 32, 2); }
    { auto& c = make("last_slot_entry"); c.entry = MAX_PROGRAM_CODE_LENGTH - 1; }
    { auto& c = make("ordinary_call", 2, 2); c.b.Flow(0, O::CALL, 32, 2); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Add(32); }
    { auto& c = make("nested_calls", 4, 4); c.b.Flow(0, O::CALL, 32, 3); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Add(32); c.b.Flow(33, O::CALL, 64, 3);
      c.b.Add(64); c.b.Flow(65, O::CALL, 96, 2); c.b.Add(96); }
    { auto& c = make("conditional_call", 1, 2); c.b.Flow(0, O::CALLC, 32, 2); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Add(32); }
    { auto& c = make("uniform_call", 1, 2); c.b.Flow(0, O::CALLU, 32, 2); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Add(32); }
    { auto& c = make("nested_conditional_call", 2, 3); c.b.Flow(0, O::CALL, 32, 3); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Add(32); c.b.Flow(33, O::CALLC, 64, 2); c.b.Add(64); }
    { auto& c = make("shared_suffix_calls", 5, 5); c.b.Flow(0, O::CALL, 32, 4);
      c.b.Flow(1, O::CALL, 33, 3); c.b.Store(2); c.b.End(3);
      c.b.Add(32); c.b.Add(33); c.b.Add(35); }
    { auto& c = make("conditional_body", 1, 1); c.b.Flow(0, O::CALL, 32, 5);
      c.b.Store(1); c.b.End(2); c.b.Flow(32, O::IFC, 35, 1); c.b.Add(33); c.b.Add(35); }
    { auto& c = make("main_helpers", 1, 1); c.b.Helpers(0); c.b.Add(4); c.b.Store(5);
      c.b.End(6); c.b.Flow(200, O::CALL, 0, 4); }
    { auto& c = make("callee_helpers", 2, 2); c.b.Flow(0, O::CALL, 32, 6); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Helpers(32); c.b.Add(36); }
    { auto& c = make("nested_callee_helpers", 3, 3); c.b.Flow(0, O::CALL, 32, 3); c.b.Add(1);
      c.b.Store(2); c.b.End(3); c.b.Add(32); c.b.Flow(33, O::CALL, 64, 6);
      c.b.Helpers(64); c.b.Add(68); }
    { auto& c = make("main_loop_call", 4, 4); c.b.Flow(0, O::LOOP, 3);
      c.b.Flow(1, O::CALL, 32, 2); c.b.Add(2); c.b.Store(4); c.b.End(5); c.b.Add(32); }
    { auto& c = make("callee_loop", 4, 4); c.b.Flow(0, O::CALL, 32, 5);
      c.b.Store(1); c.b.End(2); c.b.Flow(32, O::LOOP, 35); c.b.Add(33); c.b.Add(34); }
    { auto& c = make("callee_loop_helpers", 2, 2); c.b.Flow(0, O::CALL, 32, 8);
      c.b.Store(1); c.b.End(2); c.b.Flow(32, O::LOOP, 38); c.b.Helpers(33); c.b.Add(37); }
    { auto& c = make("nested_loop_boundary", 6, 6); c.nested_loop = true;
      c.b.Flow(0, O::LOOP, 5); c.b.Flow(1, O::LOOP, 3, 0, 1); c.b.Add(2);
      c.b.Store(6); c.b.End(7); c.b.Flow(200, O::CALL, 2, 1); }
    return cases;
}

// CodexAstraLocal: Compare only initialized architectural fields, never padding
// or emitter ownership. Distinct carry/banks remain live across every execution.
std::vector<u32> StateWords(const ShaderUnit& unit) {
    std::vector<u32> result;
    const auto append = [&](const auto& value) {
        const auto old = result.size(); result.resize(old + sizeof(value) / sizeof(u32));
        std::memcpy(result.data() + old, &value, sizeof(value));
    };
    append(unit.input); append(unit.temporary); append(unit.output); append(unit.address_registers);
    result.push_back(unit.conditional_code[0]); result.push_back(unit.conditional_code[1]);
    result.push_back(unit.output_bank);
    return result;
}
void Initialize(ShaderUnit& unit, bool condition, bool bank) {
    for (u32 r = 0; r < 16; ++r) for (u32 lane = 0; lane < 4; ++lane) {
        unit.input[r][lane] = f24::FromFloat32(float(r * 4 + lane + 1) / 32);
        unit.temporary[r][lane] = f24::FromFloat32(float(r + lane + 2) / 16);
        for (u32 b = 0; b < 2; ++b)
            unit.output[b][r][lane] = f24::FromFloat32(-float(b * 64 + r * 4 + lane + 1) / 32);
    }
    for (u32 lane = 0; lane < 4; ++lane) {
        unit.input[1][lane] = f24::Zero();
        unit.input[2][lane] = f24::One();
    }
    unit.conditional_code[0] = condition; unit.conditional_code[1] = !condition;
    unit.address_registers[0] = 1; unit.address_registers[1] = -2; unit.address_registers[2] = 3;
    unit.output_bank = bank;
}

// CodexAstraLocal: FP control must survive a generated call; sticky status may
// gain arithmetic exceptions but must not lose the caller's pre-existing flags.
u64 ReadFpcr() { u64 x; asm volatile("mrs %0, fpcr" : "=r"(x)); return x; }
u64 ReadFpsr() { u64 x; asm volatile("mrs %0, fpsr" : "=r"(x)); return x; }
void SetFp(u64 control, u64 status) {
    asm volatile("msr fpcr, %0\nmsr fpsr, %1\nisb" :: "r"(control), "r"(status) : "memory");
}
struct RestoreFp {
    u64 control = ReadFpcr(), status = ReadFpsr();
    ~RestoreFp() { SetFp(control, status); }
};
void CheckAbi(const Observation& o, u64 seed) {
    Check(o.before_sp == o.after_sp, "stack pointer not restored");
    Check((o.before_sp & 15) == 0, "unaligned call stack");
    Check(o.expected_lr == o.after_lr, "link register not restored");
    Check(o.gprs[0] == seed, "caller X19 not restored");
    for (u32 n = 1; n < o.gprs.size(); ++n)
        Check(o.gprs[n] == u64(0x1f + n) * 0x0101010101010101ULL, "callee GPR not restored");
    for (u32 n = 0; n < o.vectors.size(); ++n) for (u32 half = 0; half < 2; ++half)
        Check(o.vectors[n][half] == u64(8 + n) * 0x0101010101010101ULL, "callee vector not restored");
    Check(o.before_fpcr == o.after_fpcr, "FP control changed");
    Check((o.after_fpsr & o.before_fpsr) == o.before_fpsr, "FP sticky status lost");
}

// CodexAstraLocal: Control/carry/output fields remain bit-exact except the two
// existing polynomial helper results. Check their finite 0/1 fixtures within
// 1e-5 before normalizing only those sixteen result words for the full comparison;
// this is not a new accuracy bound for arbitrary EX2/LG2 inputs.
void CheckState(const ShaderUnit& actual, const std::vector<u32>& expected, bool helpers) {
    auto words = StateWords(actual);
    if (helpers) {
        for (u32 base : {120U, 124U, 132U + 64U * actual.output_bank,
                        136U + 64U * actual.output_bank}) {
            for (u32 lane = 0; lane < 4; ++lane) {
                const u32 i = base + lane;
                const float value = std::bit_cast<float>(words[i]);
                Check(std::isfinite(value) &&
                          std::abs(value - std::bit_cast<float>(expected[i])) <= 1e-5f,
                      "helper result outside fixture tolerance");
                words[i] = expected[i];
            }
        }
    }
    Check(words == expected, "interpreter/JIT architectural state differs");
}

// CodexAstraLocal: Share only the compiled immutable program/live read-only
// uniforms. Four forced participants each own eight ShaderUnits; checks happen
// after the synchronous join, so neither assertion accounting nor ABI recovery
// globals are touched concurrently. No renderer or FIFO work is modeled here.
void ConcurrentCase(const Case& c, const Shader::JitShader& shader, ShaderSetup& setup,
                    Common::Uberhar::ParallelWork& pool) {
    constexpr u32 count = 32, grain = 8;
    Shader::InterpreterEngine reference;
    reference.SetupBatch(setup, c.entry);
    const auto context = shader.BindForDraw(setup, c.entry);
    for (u64 round = 0; round < 4; ++round) {
        const u64 control = (round << 22) | ((round & 1) << 24) | ((round >> 1) << 25);
        constexpr u64 status = 0x08000081;
        SetFp(0, 0);
        setup.uniforms.b[0] = (round & 1) != 0;
        std::array<ShaderUnit, count> expected;
        std::array<ShaderUnit, count> actual;
        std::array<u8, count> environments{};
        for (u32 i = 0; i < count; ++i) {
            Initialize(actual[i], setup.uniforms.b[0], (i & 1) != 0);
            for (u32 lane = 0; lane < 4; ++lane) {
                actual[i].input[0][lane] = f24::FromFloat32(float(i + lane + 1) / 32);
                actual[i].temporary[0][lane] = f24::FromFloat32(float(i + lane + 3) / 16);
            }
            expected[i] = actual[i];
            reference.Run(setup, expected[i]);
        }
        SetFp(control, status);
        std::barrier together{4};
        const auto result = pool.Run(count, grain, [&](u32 first, u32 last) {
            together.arrive_and_wait();
            for (u32 i = first; i < last; ++i) {
                environments[i] = ReadFpcr() == control && (ReadFpsr() & status) == status;
                context.Run(actual[i]);
            }
        });
        Check(result.owner_items == grain && result.worker_items == count - grain,
              "shared JIT did not execute owner and worker work");
        Check(result.working_threads == 4, "shared JIT missing participant");
        Check(pool.CreatedWorkers() == 3, "shared JIT worker ownership");
        Check(ReadFpcr() == control, "shared JIT changed owner FP control");
        Check((ReadFpsr() & status) == status, "shared JIT lost owner sticky status");
        for (u32 i = 0; i < count; ++i) {
            Check(environments[i], "shared JIT worker FP environment differs");
            CheckState(actual[i], StateWords(expected[i]), c.b.uses_helpers);
        }
    }
}

int main(int argc, char** argv) {
    RestoreFp restore;
    const std::string filter = argc == 2 ? argv[1] : "";
    u64 runs = 0, case_count = 0;
    std::string active;
    try {
        Common::Uberhar::ParallelWork pool{4};
        for (const auto& c : Cases()) {
            if (!filter.empty() && filter != c.name) continue;
            active = c.name;
            ShaderSetup setup;
            setup.UpdateProgramCode(c.b.program, MAX_PROGRAM_CODE_LENGTH);
            setup.UpdateSwizzleData(c.b.swizzle, c.b.words);
            setup.uniforms = {};
            setup.uniforms.i[0] = {1, 2, static_cast<u8>(c.nested_loop ? 0 : 1), 0};
            setup.uniforms.i[1] = {2, 2, 0, 0};
            Shader::InterpreterEngine reference;
            reference.SetupBatch(setup, c.entry);
            Shader::JitShader shader;
            shader.Compile(&c.b.program, &c.b.swizzle);
            for (bool condition : {false, true}) for (bool bank : {false, true}) {
                setup.uniforms.b[0] = condition;
                ShaderUnit initial; Initialize(initial, condition, bank);
                ShaderUnit expected = initial;
                SetFp(0, 0); reference.Run(setup, expected);
                for (u32 lane = 0; lane < 4; ++lane) {
                    const float value = initial.temporary[0][lane].ToFloat32() +
                        c.adds[condition] * initial.input[0][lane].ToFloat32();
                    Check(expected.temporary[0][lane].ToFloat32() == value, "fixture control-flow count");
                }
                const auto words = StateWords(expected);
                for (Callback callback : {&PublicRun, &PublicBoundRun})
                    for (u64 seed : {0xa5317d2900000003ULL, 0x7612000000000022ULL, 0xffffffffffffffffULL})
                        for (u64 rounding = 0; rounding < 4; ++rounding) {
                            ShaderUnit actual = initial;
                            Observation observed{};
                            SetFp((rounding << 22) | (1ULL << 24) | (1ULL << 25), 0x08000081);
                            InvokeWithAbiCheck(callback, &shader, &setup, &actual, c.entry, seed, &observed);
                            CheckAbi(observed, seed);
                            CheckState(actual, words, c.b.uses_helpers);
                            ++runs;
                        }
            }
            ConcurrentCase(c, shader, setup, pool);
            runs += 128;
            ++case_count;
            std::printf("{\"case\":\"%s\",\"passed\":true}\n", c.name.c_str());
            std::fflush(stdout);
        }
        Check(case_count != 0, "unknown test case");
        std::printf("{\"passed\":true,\"cases\":%llu,\"executions\":%llu,\"assertions\":%llu,\"parallel_jobs\":%llu}\n",
                    static_cast<unsigned long long>(case_count), static_cast<unsigned long long>(runs),
                    static_cast<unsigned long long>(assertions), static_cast<unsigned long long>(case_count * 4));
        return 0;
    } catch (const std::exception& error) {
        std::printf("{\"passed\":false,\"case\":\"%s\",\"error\":\"%s\"}\n", active.c_str(), error.what());
        return 1;
    }
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Exercise production interpreter control flow and prove ordinary Run
// calls allocate no heap storage. This is not a benchmark of a Thor game workload.
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <nihstro/inline_assembly.h>
#include "common/logging/log.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/shader/shader_interpreter.h"

namespace {
bool count_allocations = false;
unsigned allocations = 0;
} // namespace
void* operator new(std::size_t size) {
    if (count_allocations)
        ++allocations;
    if (void* value = std::malloc(size ? size : 1))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* value) noexcept {
    std::free(value);
}
void operator delete[](void* value) noexcept {
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log
void Check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
void Assemble(Pica::ShaderSetup& setup, std::initializer_list<nihstro::InlineAsm> instructions) {
    const auto binary = nihstro::InlineAsm::CompileToRawBinary(instructions);
    for (unsigned i = 0; i < binary.program.size(); ++i)
        setup.UpdateProgramCode(i, binary.program[i].hex);
    for (unsigned i = 0; i < binary.swizzle_table.size(); ++i)
        setup.UpdateSwizzleData(i, binary.swizzle_table[i].hex);
}
int main() {
    using O = nihstro::OpCode::Id;
    const auto input = nihstro::SourceRegister::MakeInput(0);
    const auto temporary = nihstro::SourceRegister::MakeTemporary(0);
    const auto output = nihstro::DestRegister::MakeOutput(0);
    Pica::Shader::InterpreterEngine engine;
    Pica::ShaderSetup loop;
    Assemble(loop, {{O::MOV, temporary, input},
                    {O::LOOP, 0},
                    {O::ADD, temporary, temporary, input},
                    {nihstro::InlineAsm::Type::EndLoop},
                    {O::MOV, output, temporary},
                    {O::END}});
    loop.uniforms.i[0] = {2, 0, 1, 0};
    engine.SetupBatch(loop, 0);
    Pica::ShaderSetup call;
    Assemble(call, {{O::NOP}, {O::END}, {O::MOV, output, input}, {O::END}});
    nihstro::Instruction branch{};
    branch.opcode = O::CALL;
    branch.flow_control.dest_offset = 2;
    branch.flow_control.num_instructions = 1;
    call.UpdateProgramCode(0, branch.hex);
    engine.SetupBatch(call, 0);
    Pica::ShaderSetup conditional;
    Assemble(conditional,
             {{O::NOP}, {O::MOV, output, input}, {O::MOV, output, temporary}, {O::END}});
    branch = {};
    branch.opcode = O::IFC;
    branch.flow_control.dest_offset = 2;
    branch.flow_control.num_instructions = 1;
    branch.flow_control.op = nihstro::Instruction::FlowControlType::Op::JustX;
    branch.flow_control.refx = true;
    conditional.UpdateProgramCode(0, branch.hex);
    engine.SetupBatch(conditional, 0);
    count_allocations = true;
    for (unsigned i = 0; i < 10000; ++i) {
        Pica::ShaderUnit state;
        state.input[0] = Common::Vec4<Pica::f24>::AssignToAll(Pica::f24::FromFloat32(2));
        engine.Run(loop, state);
        Check(state.output[0][0].x.ToFloat32() == 8, "loop result changed");
        engine.Run(call, state);
        Check(state.output[0][0].x.ToFloat32() == 2, "call result changed");
        state.conditional_code[0] = i & 1;
        engine.Run(conditional, state);
        Check(state.output[0][0].x.ToFloat32() == ((i & 1) ? 2 : 8), "conditional result changed");
    }
    count_allocations = false;
    Check(allocations == 0, "vertex interpreter allocated on the heap");
    std::puts("PASS: 30000 interpreter loop/call/conditional invocations; zero heap allocations");
}

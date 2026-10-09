// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Execute actual JitEngine selection with deliberately identical
// hash prefixes but distinct full source bytes. Exact artifact identity is a
// prerequisite for a certificate made from the current ShaderSetup.
#include <array>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <nihstro/shader_bytecode.h>
#include "common/logging/log.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/pica/uberhar_parallel_vertex.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit.h"

// CodexAstraLocal: Only diagnostic logging/settings are adapted by the driver;
// both actual execution engines and their generated call contexts stay intact.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}
using namespace Pica;
using O = nihstro::OpCode::Id;
using S = nihstro::SourceRegister;
using D = nihstro::DestRegister;
unsigned checks{};
void Need(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }

struct Program {
    ProgramCode code{};
    SwizzleData swizzles{};
    Program(O op, bool rotate = false) {
        nihstro::Instruction end{}; end.opcode = O::END; code.fill(end.hex);
        nihstro::Instruction instruction{}; instruction.opcode = op;
        instruction.common.dest = D::MakeOutput(0);
        instruction.common.src1 = S::MakeFloat(0);
        instruction.common.src2 = S::MakeInput(0);
        code[0] = instruction.hex;
        instruction.opcode = O::MOV; instruction.common.src1 = S::MakeInput(1);
        code[4] = instruction.hex;
        nihstro::SwizzlePattern swizzle{};
        for (unsigned lane = 0; lane < 4; ++lane) {
            swizzle.SetDestComponentEnabled(lane, true);
            swizzle.SetSelectorSrc1(lane, static_cast<nihstro::SwizzlePattern::Selector>((lane + rotate) % 4));
            swizzle.SetSelectorSrc2(lane, static_cast<nihstro::SwizzlePattern::Selector>(lane));
        }
        swizzles[0] = swizzle.hex;
    }
    void Set(ShaderSetup& setup) const {
        // CodexAstraLocal: The public upload API permits a reported prefix size;
        // zero creates identical real hash inputs while full compiled arrays
        // differ. This deliberately simulates a combined-hash collision.
        setup.UpdateProgramCode(code, 0);
        setup.UpdateSwizzleData(swizzles, 0);
        for (u32 lane = 0; lane < 4; ++lane)
            setup.uniforms.f[0][lane] = f24::FromFloat32(float(3 + lane));
    }
};

void Compare(Shader::JitEngine& jit, ShaderSetup& setup, u32 entry) {
    jit.SetupBatch(setup, entry);
    Need(AnalyzeParallelVertex(setup.GetProgramCode(), setup.GetSwizzleData(), entry,
                              ParallelVertexBooleanUniforms(setup.uniforms), 1).Supported(),
         "source independence certificate");
    const auto context = jit.BindForDraw(setup);
    Need(bool(context), "bound actual compiled owner");
    Shader::InterpreterEngine interpreter;
    interpreter.SetupBatch(setup, entry);
    for (bool bank : {false, true}) for (unsigned value = 0; value < 9; ++value) {
        ShaderUnit expected, actual;
        expected.output_bank = actual.output_bank = bank;
        for (u32 reg = 0; reg < 2; ++reg) for (u32 lane = 0; lane < 4; ++lane)
            actual.input[reg][lane] = expected.input[reg][lane] =
                f24::FromFloat32(float(1 + value * 7 + reg * 3 + lane));
        interpreter.Run(setup, expected);
        context.Run(actual);
        Need(std::memcmp(&expected.output[bank][0], &actual.output[bank][0], 16) == 0,
             "compiled artifact differs from current certified source");
    }
}

int main() try {
    Shader::JitEngine jit;
    Program add{O::ADD}, multiply{O::MUL}, rotate{O::ADD, true};
    ShaderSetup first, second, third;
    add.Set(first); multiply.Set(second); rotate.Set(third);
    Need(first.GetProgramCodeHash() == second.GetProgramCodeHash() &&
             first.GetSwizzleDataHash() == third.GetSwizzleDataHash(), "actual hash collision");
    Compare(jit, first, 0); const auto* add_owner = first.cached_shader;
    Compare(jit, second, 0); const auto* multiply_owner = second.cached_shader;
    Compare(jit, third, 0); const auto* rotate_owner = third.cached_shader;
    Need(add_owner != multiply_owner && add_owner != rotate_owner && multiply_owner != rotate_owner,
         "colliding compiled owners aliased");
    for (unsigned round = 0; round < 8; ++round) {
        Compare(jit, first, round % 2 ? 4 : 0);
        Compare(jit, second, 0);
        Compare(jit, third, 0);
    }
    Need(first.cached_shader == add_owner && second.cached_shader == multiply_owner &&
             third.cached_shader == rotate_owner, "cache reuse changed compiled owner");
    // CodexAstraLocal: Same-setup mutation must defeat the revision fast path;
    // unchanged reuploads then reuse the exact existing compiled owner.
    multiply.Set(first); Compare(jit, first, 0);
    Need(first.cached_shader == multiply_owner, "revision failed to rebind");
    rotate.Set(first); Compare(jit, first, 0);
    Need(first.cached_shader == rotate_owner, "descriptor revision failed to rebind");
    const auto old_revision = first.GetCodeRevision();
    rotate.Set(first); Compare(jit, first, 0);
    Need(first.GetCodeRevision() != old_revision && first.cached_shader == rotate_owner,
         "identical upload did not safely reuse owner");
    // CodexAstraLocal: A live uniform change is not source identity and remains
    // visible to the same borrowed context without recompilation.
    const auto live = jit.BindForDraw(first);
    first.uniforms.f[0] = Common::Vec4<f24>::AssignToAll(f24::FromFloat32(17));
    ShaderUnit live_unit; live_unit.input[0] = Common::Vec4<f24>::AssignToAll(f24::FromFloat32(5));
    live.Run(live_unit);
    Need(live_unit.output[0][0].x.ToFloat32() == 22, "live uniforms frozen by source identity");
    Compare(jit, first, 0);
    // CodexAstraLocal: Copy construction keeps the OpenGL cache's guest-state
    // use valid. Copy assignment must invalidate the destination's own proof
    // epoch rather than importing a possibly convergent source revision.
    {
        Shader::JitEngine copied_jit;
        ShaderSetup destination;
        destination.uniforms = {};
        add.Set(destination); Compare(copied_jit, destination, 0);
        destination.uniform_queue.Push(0x12345678);
        destination.uniforms.b[3] = true;
        destination.uniforms.i[2] = {1, 2, 3, 4};
        destination.uniforms_dirty = false;
        ShaderSetup divergent = destination;
        Need(divergent.cached_shader == nullptr, "copy retained foreign compiled owner");
        Need(divergent.GetCodeRevision() == 1, "copy constructor imported source revision");
        Need(divergent.GetProgramCode() == destination.GetProgramCode() &&
                 divergent.GetSwizzleData() == destination.GetSwizzleData() &&
                 divergent.GetProgramCodeHash() == destination.GetProgramCodeHash() &&
                 divergent.GetSwizzleDataHash() == destination.GetSwizzleDataHash() &&
                 divergent.GetBiggestProgramSize() == destination.GetBiggestProgramSize() &&
                 divergent.GetBiggestSwizzleSize() == destination.GetBiggestSwizzleSize(),
             "copy lost shader source or hash metadata");
        Need(divergent.entry_point == destination.entry_point &&
                 divergent.uniforms.f == destination.uniforms.f &&
                 divergent.uniforms.b == destination.uniforms.b &&
                 divergent.uniforms.i == destination.uniforms.i &&
                 divergent.uniform_queue.buffer == destination.uniform_queue.buffer &&
                 divergent.uniform_queue.index == destination.uniform_queue.index &&
                 divergent.uniforms_dirty == destination.uniforms_dirty,
             "copy lost guest state");
        Compare(copied_jit, divergent, 0);
        // CodexAstraLocal: One dirty-word update makes the fresh copy's local
        // epoch match the older destination's without updating its cached code.
        divergent.UpdateProgramCode(0, multiply.code[0]);
        Need(destination.GetCodeRevision() == divergent.GetCodeRevision(),
             "copy test needs convergent local revisions");
        const auto previous_revision = destination.GetCodeRevision();
        destination = divergent;
        Need(destination.cached_shader == nullptr, "assignment retained foreign compiled owner");
        Need(destination.GetCodeRevision() == previous_revision + 1,
             "assignment imported source revision");
        Compare(copied_jit, destination, 0);
        const auto self_revision = destination.GetCodeRevision();
        const auto* self_owner = destination.cached_shader;
        destination = destination;
        Need(destination.GetCodeRevision() == self_revision && destination.cached_shader == self_owner,
             "self assignment changed identity");
        Compare(copied_jit, destination, 0);
    }
    // CodexAstraLocal: Object-address reuse alone cannot validate a stale binding.
    // New construction resets cached_shader even when its local revision repeats.
    alignas(ShaderSetup) std::array<std::byte, sizeof(ShaderSetup)> storage;
    auto* reused = new (storage.data()) ShaderSetup;
    add.Set(*reused); Compare(jit, *reused, 0); const auto revision = reused->GetCodeRevision();
    reused->~ShaderSetup(); reused = new (storage.data()) ShaderSetup;
    multiply.Set(*reused); Need(reused->GetCodeRevision() == revision, "address-reuse test revision");
    Compare(jit, *reused, 0); reused->~ShaderSetup();
    std::cout << "PASS shader_identity_checks=" << checks << '\n';
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}

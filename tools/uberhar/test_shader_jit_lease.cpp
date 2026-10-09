// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Execute real leased JIT programs after setup/cache retirement,
// with deliberately colliding source hashes and independently owned uniforms.
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>
#include <nihstro/shader_bytecode.h>
#include "common/logging/log.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit.h"

// CodexAstraLocal: Diagnostic services alone are adapted; both interpreter and
// generated code operate on the actual production setup/unit/uniform layouts.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}

// CodexAstraLocal: A friend declaration is injected only into the test header.
// Observing a weak reference to the actual cache owner catches a missing lease
// before executing freed code; it neither extends ownership nor changes selection.
namespace Pica::Shader {
struct LeaseTestAccess {
    static std::weak_ptr<const void> Owner(const JitEngine& engine, const ShaderSetup& setup) {
        for (const auto& binding : engine.bindings)
            if (binding.setup == &setup)
                return binding.entry;
        throw std::runtime_error("missing prepared owner observer");
    }
};
}

using namespace Pica;
using O = nihstro::OpCode::Id;
using S = nihstro::SourceRegister;
using D = nihstro::DestRegister;
std::atomic<unsigned> checks{}, executions{};
void Need(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}

// CodexAstraLocal: Zero reported prefix lengths force genuine identical cache
// hash inputs while full generated programs/descriptors remain distinguishable.
struct Program {
    ProgramCode code{};
    SwizzleData swizzles{};
    Program(O operation = O::ADD, bool rotate = false) {
        nihstro::Instruction instruction{};
        instruction.opcode = O::END;
        code.fill(instruction.hex);
        instruction.opcode = operation;
        instruction.common.dest = D::MakeOutput(0);
        instruction.common.src1 = S::MakeFloat(0);
        instruction.common.src2 = S::MakeInput(0);
        code[0] = instruction.hex;
        instruction.opcode = O::MOV;
        instruction.common.src1 = S::MakeInput(1);
        code[4] = instruction.hex;
        nihstro::SwizzlePattern swizzle{};
        for (u32 lane = 0; lane < 4; ++lane) {
            swizzle.SetDestComponentEnabled(lane, true);
            swizzle.SetSelectorSrc1(lane, static_cast<nihstro::SwizzlePattern::Selector>((lane + rotate) % 4));
            swizzle.SetSelectorSrc2(lane, static_cast<nihstro::SwizzlePattern::Selector>(lane));
        }
        swizzles[0] = swizzle.hex;
    }
    void Set(ShaderSetup& setup) const {
        setup.UpdateProgramCode(code, 0);
        setup.UpdateSwizzleData(swizzles, 0);
        setup.uniforms = {};
        for (u32 lane = 0; lane < 4; ++lane)
            setup.uniforms.f[0][lane] = f24::FromFloat32(float(3 + lane));
    }
};

// CodexAstraLocal: Compare both raw output banks and all explicit architectural
// state for small exact arithmetic, including lanes/registers untouched by code.
void Compare(const ShaderRunLease& lease, const Program& program, const Uniforms& uniforms,
             u32 entry, u32 seed) {
    Need(bool(lease), "missing valid lease");
    Need(lease.EntryPoint() == entry, "lease entry identity changed");
    Need(lease.Program().size() == program.code.size() &&
             std::equal(lease.Program().begin(), lease.Program().end(), program.code.begin()) &&
             lease.Swizzles().size() == program.swizzles.size() &&
             std::equal(lease.Swizzles().begin(), lease.Swizzles().end(), program.swizzles.begin()),
         "lease source changed after upload");
    const auto bound = lease.Bind(uniforms);
    Need(bool(bound) && bound.uniforms == &uniforms, "bound context did not use owned uniforms");
    ShaderSetup expected_setup;
    program.Set(expected_setup);
    expected_setup.uniforms = uniforms;
    Shader::InterpreterEngine interpreter;
    interpreter.SetupBatch(expected_setup, entry);
    for (bool bank : {false, true}) {
        ShaderUnit expected, actual;
        expected.output_bank = actual.output_bank = bank;
        for (u32 reg = 0; reg < 16; ++reg) for (u32 lane = 0; lane < 4; ++lane) {
            actual.input[reg][lane] = expected.input[reg][lane] =
                f24::FromFloat32(float(1 + seed + reg * 3 + lane));
            actual.temporary[reg][lane] = expected.temporary[reg][lane] =
                f24::FromFloat32(float(100 + reg * 4 + lane));
            for (u32 out_bank = 0; out_bank < 2; ++out_bank)
                actual.output[out_bank][reg][lane] = expected.output[out_bank][reg][lane] =
                    f24::FromFloat32(float(1000 + out_bank * 80 + reg * 4 + lane));
        }
        interpreter.Run(expected_setup, expected);
        bound.Run(actual);
        ++executions;
        Need(std::memcmp(actual.output.data(), expected.output.data(), sizeof(actual.output)) == 0,
             "leased generated output differs");
        Need(std::memcmp(actual.input.data(), expected.input.data(), sizeof(actual.input)) == 0 &&
                 std::memcmp(actual.temporary.data(), expected.temporary.data(), sizeof(actual.temporary)) == 0 &&
                 std::memcmp(actual.address_registers, expected.address_registers, sizeof(actual.address_registers)) == 0 &&
                 std::memcmp(actual.conditional_code, expected.conditional_code, sizeof(actual.conditional_code)) == 0 &&
                 actual.output_bank == expected.output_bank,
             "leased architectural state differs");
    }
}

// CodexAstraLocal: Exercise actual load invalidation without linking an unrelated
// serialization backend. This no-change loading archive still runs the real
// ShaderSetup serialize loading branch, which must invalidate a prepared lease.
struct LoadingArchive {
    using is_loading = std::true_type;
    template<class T> LoadingArchive& operator&(T&) { return *this; }
};

void Admission() {
    Shader::JitEngine jit;
    Shader::InterpreterEngine interpreter;
    ShaderSetup setup;
    Program add, multiply{O::MUL}, rotate{O::ADD, true};
    Need(!jit.LeaseForDraw(setup), "unprepared setup admitted");
    Need(!interpreter.LeaseForDraw(setup), "unsupported engine admitted");
    add.Set(setup); jit.SetupBatch(setup, 0);
    auto lease = jit.LeaseForDraw(setup);
    Need(bool(lease), "prepared setup refused");
    const auto owner = lease.OwnerIdentity();
    setup.uniforms.f[0].x = f24::FromFloat32(19);
    Need(jit.LeaseForDraw(setup).OwnerIdentity() == owner, "uniform update changed owner");
    Compare(lease, add, setup.uniforms, 0, 1);
    multiply.Set(setup);
    Need(!jit.LeaseForDraw(setup), "stale source revision admitted");
    jit.SetupBatch(setup, 0);
    auto multiplied = jit.LeaseForDraw(setup);
    Need(multiplied.OwnerIdentity() != owner, "colliding compiled owner aliased");
    Compare(lease, add, setup.uniforms, 0, 2);
    Compare(multiplied, multiply, setup.uniforms, 0, 2);
    rotate.Set(setup);
    Need(!jit.LeaseForDraw(setup), "stale descriptor revision admitted");
    jit.SetupBatch(setup, 0);
    Compare(jit.LeaseForDraw(setup), rotate, setup.uniforms, 0, 3);
    ShaderSetup copied = setup;
    Need(!jit.LeaseForDraw(copied), "unprepared copy admitted");
    setup = copied;
    Need(!jit.LeaseForDraw(setup), "unprepared assignment admitted");
    jit.SetupBatch(setup, 0);
    LoadingArchive archive;
    boost::serialization::access::serialize(archive, setup, 0);
    Need(!jit.LeaseForDraw(setup), "restored setup admitted without preparation");
    jit.SetupBatch(setup, 0);
    setup.entry_point = MAX_PROGRAM_CODE_LENGTH;
    Need(!jit.LeaseForDraw(setup), "out-of-range lease entry admitted");
}

void BindingsAndReuse() {
    Shader::JitEngine jit;
    Program add, multiply{O::MUL}, rotate{O::ADD, true};
    ShaderSetup a, b, c;
    add.Set(a); multiply.Set(b); rotate.Set(c);
    Need(a.GetProgramCodeHash() == b.GetProgramCodeHash() &&
             a.GetSwizzleDataHash() == c.GetSwizzleDataHash(), "fixture hash collision missing");
    jit.SetupBatch(a, 0); auto first = jit.LeaseForDraw(a);
    jit.SetupBatch(a, 4); auto entry = jit.LeaseForDraw(a);
    Need(first.OwnerIdentity() == entry.OwnerIdentity(), "entry change recompiled source");
    Compare(first, add, a.uniforms, 0, 4);
    Compare(entry, add, a.uniforms, 4, 4);
    jit.SetupBatch(b, 0); jit.SetupBatch(c, 0);
    Need(!jit.LeaseForDraw(a), "evicted prepared binding unexpectedly admitted");
    jit.SetupBatch(a, 0);
    Need(jit.LeaseForDraw(a).OwnerIdentity() == first.OwnerIdentity(), "exact source owner not reused");
    Compare(first, add, a.uniforms, 0, 5);
    // CodexAstraLocal: A newly constructed setup can reuse address and local epoch
    // but has no prepared cached_shader; stale two-binding identity must not pass.
    alignas(ShaderSetup) std::array<std::byte, sizeof(ShaderSetup)> storage;
    auto* reused = new (storage.data()) ShaderSetup;
    add.Set(*reused); jit.SetupBatch(*reused, 0);
    const auto revision = reused->GetCodeRevision();
    reused->~ShaderSetup(); reused = new (storage.data()) ShaderSetup;
    multiply.Set(*reused);
    Need(reused->GetCodeRevision() == revision, "address reuse epoch mismatch");
    Need(!jit.LeaseForDraw(*reused), "unprepared reused address admitted");
    jit.SetupBatch(*reused, 0);
    Compare(jit.LeaseForDraw(*reused), multiply, reused->uniforms, 0, 6);
    reused->~ShaderSetup();
}

void Lifetime() {
    Program add;
    ShaderRunLease lease;
    Uniforms owned{};
    std::weak_ptr<const void> actual_owner;
    {
        auto jit = std::make_unique<Shader::JitEngine>();
        auto setup = std::make_unique<ShaderSetup>();
        add.Set(*setup); jit->SetupBatch(*setup, 0);
        owned = setup->uniforms;
        lease = jit->LeaseForDraw(*setup);
        actual_owner = Shader::LeaseTestAccess::Owner(*jit, *setup);
        setup.reset(); jit.reset();
    }
    Need(!actual_owner.expired(), "lease did not retain compiled owner");
    Compare(lease, add, owned, 0, 7);
    auto copy = lease;
    auto moved = std::move(lease);
    Need(!lease && !lease.Bind(owned) && lease.Program().empty() && lease.Swizzles().empty() &&
             lease.OwnerIdentity() == nullptr, "moved-from lease retained borrowed access");
    Need(copy.OwnerIdentity() == moved.OwnerIdentity(), "copied lease changed owner");
    Compare(copy, add, owned, 0, 8);
    // CodexAstraLocal: Separate packet uniforms and ShaderUnits are used by each
    // thread, while only immutable generated code/source is shared after teardown.
    std::array<std::exception_ptr, 4> errors{};
    std::vector<std::thread> workers;
    for (u32 thread = 0; thread < errors.size(); ++thread)
        workers.emplace_back([&, thread, retained = moved] {
            try {
                auto uniforms = owned;
                for (u32 lane = 0; lane < 4; ++lane)
                    uniforms.f[0][lane] = f24::FromFloat32(float(20 + thread * 7 + lane));
                for (u32 value = 0; value < 32; ++value)
                    Compare(retained, add, uniforms, 0, value + thread * 32);
            } catch (...) { errors[thread] = std::current_exception(); }
        });
    for (auto& worker : workers) worker.join();
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
    copy = {};
    Need(!actual_owner.expired(), "remaining lease lost owner");
    moved = {};
    Need(actual_owner.expired(), "final lease release retained compiled owner");
}

int main(int argc, char** argv) try {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    if (selected == "all" || selected == "admission") Admission();
    if (selected == "all" || selected == "bindings") BindingsAndReuse();
    if (selected == "all" || selected == "lifetime") Lifetime();
    Need(checks > 0, "unknown fixture case");
    std::cout << "PASS lease_checks=" << checks << " executions=" << executions << '\n';
} catch (const std::exception& error) {
    std::cout << "FAIL: " << error.what() << '\n';
    return 1;
}

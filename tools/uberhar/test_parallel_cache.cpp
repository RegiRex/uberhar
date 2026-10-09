// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Execute the extracted production proof cache against fresh
// certificates after real scalar/range/full uploads and save-state restoration.
#include <any>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/uberhar_parallel_vertex.h"
#include "video_core/pica/uberhar_vertex_parallel_batch.h"

// CodexAstraLocal: Only the optional worker name and logging sinks are adapted;
// production ShaderSetup mutations, serialization and certificate code execute.
namespace Common {
void SetCurrentThreadName(const char*) {}
namespace Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}
}
namespace Pica {
#include "parallel_cache.inc"
}

namespace {
using namespace Pica;
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
unsigned checks{};
void Need(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}

// CodexAstraLocal: This in-memory archive copies each actual serialized field,
// so restoration exercises the production loading branch without file IO or a
// duplicate implementation of ShaderSetup's serialized field list.
template <bool Loading> struct Archive {
    using is_loading = std::bool_constant<Loading>;
    std::vector<std::any>& fields;
    std::size_t cursor{};
    template <typename T> Archive& operator&(T& value) {
        if constexpr (Loading) value = std::any_cast<T>(fields.at(cursor++));
        else fields.emplace_back(value);
        return *this;
    }
};

u32 Flow(O opcode, unsigned target = 0) {
    nihstro::Instruction instruction{};
    instruction.opcode = opcode;
    instruction.flow_control.dest_offset = target;
    instruction.flow_control.bool_uniform_id = 0;
    return instruction.hex;
}
u32 Arithmetic(O opcode, D dest, S source) {
    nihstro::Instruction instruction{};
    instruction.opcode = opcode;
    instruction.common.dest = dest;
    instruction.common.src1 = source;
    instruction.common.src2 = S::MakeInput(0);
    instruction.common.operand_desc_id = 0;
    return instruction.hex;
}
void ConsumeHashes(ShaderSetup& setup) {
    setup.GetProgramCodeHash();
    setup.GetSwizzleDataHash();
}
void Compare(ParallelVertexState& cache, ShaderSetup& setup, u32 outputs) {
    ConsumeHashes(setup);
    const auto actual = cache.Get(setup, outputs);
    const auto expected = AnalyzeParallelVertex(setup.GetProgramCode(), setup.GetSwizzleData(),
        setup.entry_point, ParallelVertexBooleanUniforms(setup.uniforms), outputs);
    Need(actual.status == expected.status && actual.pc == expected.pc &&
         actual.reg == expected.reg && actual.lanes == expected.lanes,
         "cached certificate differs from current source/state");
}
}

int main() {
    ShaderSetup setup;
    auto cache = std::make_unique<ParallelVertexState>();
    ProgramCode safe{};
    safe.fill(Flow(O::END));
    safe[0] = Arithmetic(O::MOV, D::MakeOutput(0), S::MakeInput(0));
    ProgramCode carry = safe;
    carry[0] = Arithmetic(O::ADD, D::MakeTemporary(0), S::MakeTemporary(0));
    carry[1] = Arithmetic(O::MOV, D::MakeOutput(0), S::MakeTemporary(0));
    SwizzleData swizzles{};
    nihstro::SwizzlePattern identity{};
    for (unsigned lane = 0; lane < 4; ++lane) {
        identity.SetDestComponentEnabled(lane, true);
        identity.SetSelectorSrc1(lane, static_cast<nihstro::SwizzlePattern::Selector>(lane));
        identity.SetSelectorSrc2(lane, static_cast<nihstro::SwizzlePattern::Selector>(lane));
    }
    swizzles[0] = identity.hex;
    setup.UpdateProgramCode(safe);
    setup.UpdateSwizzleData(swizzles);
    Compare(*cache, setup, 1);
    Need(cache->Get(setup, 1).Supported(), "safe source not admitted");

    // CodexAstraLocal: Exercise reused dirty periods and each supported upload
    // form; a source change must never inherit the earlier safe certificate.
    for (unsigned variant = 0; variant < 3; ++variant) {
        setup.UpdateProgramCode(safe);
        Compare(*cache, setup, 1);
        if (variant == 0) {
            setup.UpdateProgramCode(0, carry[0]);
            setup.UpdateProgramCode(1, carry[1]);
        } else if (variant == 1) setup.UpdateProgramCodeRange(0, carry.data(), 3);
        else setup.UpdateProgramCode(carry);
        Compare(*cache, setup, 1);
        Need(!cache->Get(setup, 1).Supported(), "source edit retained safe proof");
    }

    // CodexAstraLocal: Force a shortlist collision by assigning the old entry
    // the new hashes. Exact source bytes, rather than hash equality, must decide.
    setup.UpdateProgramCode(safe);
    Compare(*cache, setup, 1);
    auto* previous = cache->current;
    setup.UpdateProgramCode(carry);
    previous->program_hash = setup.GetProgramCodeHash();
    previous->swizzle_hash = setup.GetSwizzleDataHash();
    Compare(*cache, setup, 1);
    Need(!cache->Get(setup, 1).Supported(), "hash collision authorized carry");

    // CodexAstraLocal: Branch uniforms and alternate entries share code bytes
    // but can change safety. Mask and descriptor variations exercise output and
    // lane-dependent admission against a separately recomputed certificate.
    ProgramCode conditional = carry;
    conditional[0] = Flow(O::JMPU, 4);
    conditional[1] = carry[0];
    conditional[2] = carry[1];
    setup.UpdateProgramCode(conditional);
    for (unsigned round = 0; round < 96; ++round) {
        setup.entry_point = round % 3 == 0 ? 4 : 0;
        setup.WriteUniformBoolReg(round & 1);
        const u32 mask = 1U << (round % 16);
        Compare(*cache, setup, mask);
        const u32 desc = identity.hex ^ ((round & 7) << 4);
        if (round % 3 == 0) setup.UpdateSwizzleData(0, desc);
        else if (round % 3 == 1) setup.UpdateSwizzleDataRange(0, &desc, 1);
        else { swizzles[0] = desc; setup.UpdateSwizzleData(swizzles); }
        Compare(*cache, setup, mask);
    }

    // CodexAstraLocal: Output0 is always written while output1 is conditionally
    // carried. A certificate for the first output mask cannot authorize both.
    ProgramCode partial = safe;
    nihstro::Instruction compare{};
    compare.opcode = O::CMP;
    compare.common.src1 = S::MakeInput(0);
    compare.common.src2 = S::MakeInput(1);
    compare.common.compare_op.x = nihstro::Instruction::Common::CompareOpType::LessThan;
    compare.common.compare_op.y = nihstro::Instruction::Common::CompareOpType::GreaterEqual;
    partial[1] = compare.hex;
    nihstro::Instruction branch{};
    branch.opcode = O::IFC;
    branch.flow_control.dest_offset = 4;
    branch.flow_control.num_instructions = 0;
    branch.flow_control.op = nihstro::Instruction::FlowControlType::JustX;
    branch.flow_control.refx = 1;
    partial[2] = branch.hex;
    partial[3] = Arithmetic(O::MOV, D::MakeOutput(1), S::MakeInput(1));
    setup.entry_point = 0;
    setup.UpdateProgramCode(partial);
    setup.UpdateSwizzleData(0, identity.hex);
    Compare(*cache, setup, 1);
    Need(cache->Get(setup, 1).Supported(), "unselected carry rejected");
    Compare(*cache, setup, 3);
    Need(!cache->Get(setup, 3).Supported(), "new output mask retained old proof");

    // CodexAstraLocal: Load a previously saved unsafe source after caching a
    // safe one. The host revision is deliberately not archived and must advance.
    setup.entry_point = 0;
    setup.UpdateProgramCode(carry);
    setup.UpdateSwizzleData(0, identity.hex);
    ConsumeHashes(setup);
    std::vector<std::any> fields;
    Archive<false> save{fields};
    boost::serialization::access::serialize(save, setup, 0);
    setup.UpdateProgramCode(safe);
    Compare(*cache, setup, 1);
    const auto revision = setup.GetCodeRevision();
    Archive<true> load{fields};
    boost::serialization::access::serialize(load, setup, 0);
    Need(setup.GetCodeRevision() != revision, "restore retained host revision");
    Compare(*cache, setup, 1);
    Need(!cache->Get(setup, 1).Supported(), "restore retained safe proof");

    // CodexAstraLocal: Guest-state assignment cannot import another setup's
    // local revision and accidentally validate this owner's earlier proof.
    ShaderSetup copied;
    copied.UpdateProgramCode(carry);
    copied.UpdateSwizzleData(0, identity.hex);
    ConsumeHashes(copied);
    setup.UpdateProgramCode(safe);
    Compare(*cache, setup, 1);
    const auto before_copy = setup.GetCodeRevision();
    setup = copied;
    Need(setup.GetCodeRevision() != before_copy, "assignment retained host revision");
    Compare(*cache, setup, 1);
    Need(!cache->Get(setup, 1).Supported(), "assignment retained safe proof");

    // CodexAstraLocal: More than eight distinct programs forces bounded cache
    // eviction; revisiting the original must still reconstruct the right proof.
    for (unsigned round = 0; round < 40; ++round) {
        auto varied = round & 1 ? carry : safe;
        varied[80 + round] = Flow(O::NOP);
        setup.UpdateProgramCode(varied);
        Compare(*cache, setup, 1);
        Need(cache->Get(setup, 1).Supported() == ((round & 1) == 0), "evicted proof stale");
    }
    std::cout << "parallel cache PASS: " << checks << " checks; edits, keys, collisions, restore, eviction\n";
}

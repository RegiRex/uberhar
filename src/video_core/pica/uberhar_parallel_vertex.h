// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <map>
#include <span>
#include <vector>
#include <nihstro/shader_bytecode.h>
#include "common/common_types.h"
#include "video_core/pica/shader_setup.h"

namespace Pica {

// CodexAstraLocal: This certificate only permits splitting one no-GS draw's
// shader invocations into grains with identical initial ShaderUnit state. The
// caller preserves live input mapping, immutable code/uniforms, FP environment,
// FIFO miss order and ordered output assembly. It does not certify GPU arithmetic,
// fix a backend ABI, authorize GS, or preserve unused final state across draws.
enum class ParallelVertexStatus : u8 {
    Independent,
    InvalidContract,
    UnsupportedInstruction,
    InvalidDescriptor,
    InvalidOperand,
    InvalidScope,
    AmbiguousClosure,
    StackLimit,
    WorkLimit,
    CyclicFlow,
    TemporaryCarry,
    AddressCarry,
    ConditionCarry,
    OutputCarry,
    MissingEnd,
    // CodexAstraLocal: Size the caller's bounded rejection counters without a
    // separate numeric copy of this certificate result vocabulary.
    Count,
};

struct ParallelVertexCertificate {
    ParallelVertexStatus status{ParallelVertexStatus::InvalidContract};
    u32 pc{};
    u32 nodes{};
    u32 reg{};
    u8 lanes{};

    bool Supported() const {
        return status == ParallelVertexStatus::Independent;
    }
};

// CodexAstraLocal: Uniform flow is constant only within this draw. This complete
// Boolean snapshot must participate in a certificate cache key, together with
// code/swizzles, entry and output mask; changing uniforms requires revalidation.
inline u16 ParallelVertexBooleanUniforms(const Uniforms& uniforms) {
    u16 result{};
    for (u32 i = 0; i < uniforms.b.size(); ++i) {
        result |= static_cast<u16>(uniforms.b[i]) << i;
    }
    return result;
}

namespace ParallelVertexDetail {
using Op = nihstro::OpCode;
using Lanes = std::array<u8, 16>;
inline constexpr u32 MaxNodes = 8192;
inline constexpr u32 MaxCalls = 3;
inline constexpr u32 MaxIfs = 8;
inline constexpr u32 MaxRegions = 256;

// CodexAstraLocal: Separate possible writes from definite writes. Lanes never
// written by any reachable invocation retain the identical draw-initial value;
// every mutable consumed lane must instead be defined in this invocation.
struct Definitions {
    Lanes temporary{};
    Lanes output{};
    u8 addresses{};
    u8 conditions{};
};
struct Access {
    Definitions reads{};
    Definitions writes{};
};

inline void Union(Definitions& to, const Definitions& from) {
    for (u32 reg = 0; reg < 16; ++reg) {
        to.temporary[reg] |= from.temporary[reg];
        to.output[reg] |= from.output[reg];
    }
    to.addresses |= from.addresses;
    to.conditions |= from.conditions;
}
inline void Intersect(Definitions& to, const Definitions& from) {
    for (u32 reg = 0; reg < 16; ++reg) {
        to.temporary[reg] &= from.temporary[reg];
        to.output[reg] &= from.output[reg];
    }
    to.addresses &= from.addresses;
    to.conditions &= from.conditions;
}

// CodexAstraLocal: Exact return/IF contexts allow reused or overlapping subroutine
// ranges without treating guest PC alone as a join. Limits reject rather than
// emulate stack overflow; LOOP/EMIT and ambiguous cross-scope transfers stay serial.
struct Call {
    u32 begin{}, end{}, return_pc{};
    auto operator<=>(const Call&) const = default;
};
struct If {
    u32 instruction{}, otherwise{}, end{}, call_depth{};
    auto operator<=>(const If&) const = default;
};
struct Context {
    u32 pc{}, calls{}, ifs{};
    std::array<Call, MaxCalls> call{};
    std::array<If, MaxIfs> branch{};
    auto operator<=>(const Context&) const = default;
};
struct Node {
    Context context{};
    Access access{};
    std::array<u32, 2> next{};
    u32 successors{}, incoming{};
    bool terminal{};
};
struct Region {
    u32 instruction{}, otherwise{}, end{};
};

// CodexAstraLocal: Check reads before writes, with exact swizzles and relative
// uniform addressing. Conservatively require every loaded operand lane, including
// discarded SIMD arithmetic, so grain resets cannot change sticky FP flags via
// a masked-away carry value. This is stricter than output-only dependence; inputs
// remain read-only and must be supplied by the caller for each invocation.
inline ParallelVertexStatus Decode(nihstro::Instruction instruction,
                                   std::span<const u32> swizzles, Access& access) {
    const auto opcode = instruction.opcode.Value();
    const auto effective = opcode.EffectiveOpCode();
    const auto info = opcode.GetInfo();
    const bool mad = info.type == Op::Type::MultiplyAdd;
    const bool inverted = (info.subtype & Op::Info::SrcInversed) != 0;
    const u32 descriptor = mad ? u32(instruction.mad.operand_desc_id)
                               : u32(instruction.common.operand_desc_id);
    if (descriptor >= swizzles.size()) {
        return ParallelVertexStatus::InvalidDescriptor;
    }
    const nihstro::SwizzlePattern swizzle{swizzles[descriptor]};
    u8 enabled{};
    for (u32 lane = 0; lane < 4; ++lane) {
        enabled |= swizzle.DestComponentEnabled(lane) ? 1U << lane : 0;
    }
    std::array<nihstro::SourceRegister, 3> sources{};
    std::array<u32, 3> relative{};
    u32 source_count{};
    u32 destination{};
    if (mad) {
        if (effective != Op::Id::MAD && effective != Op::Id::MADI) {
            return ParallelVertexStatus::UnsupportedInstruction;
        }
        sources = {instruction.mad.GetSrc1(inverted), instruction.mad.GetSrc2(inverted),
                   instruction.mad.GetSrc3(inverted)};
        relative[inverted ? 2 : 1] = instruction.mad.address_register_index;
        source_count = 3;
        destination = instruction.mad.dest.Value();
    } else {
        sources[0] = instruction.common.GetSrc1(inverted);
        sources[1] = instruction.common.GetSrc2(inverted);
        relative[inverted ? 1 : 0] = instruction.common.address_register_index;
        destination = instruction.common.dest.Value();
        switch (effective) {
        case Op::Id::MOV: case Op::Id::FLR:
        case Op::Id::RCP: case Op::Id::RSQ: case Op::Id::EX2: case Op::Id::LG2:
        case Op::Id::MOVA:
            source_count = 1;
            break;
        case Op::Id::ADD: case Op::Id::MUL: case Op::Id::MIN: case Op::Id::MAX:
        case Op::Id::SGE: case Op::Id::SGEI: case Op::Id::SLT: case Op::Id::SLTI:
        case Op::Id::DP3:
        case Op::Id::DP4:
        case Op::Id::DPH: case Op::Id::DPHI:
            source_count = 2;
            break;
        case Op::Id::CMP:
            if (u32(instruction.common.compare_op.x.Value()) > 5 ||
                u32(instruction.common.compare_op.y.Value()) > 5) {
                return ParallelVertexStatus::InvalidOperand;
            }
            source_count = 2;
            break;
        default:
            return ParallelVertexStatus::UnsupportedInstruction;
        }
    }
    for (u32 source = 0; source < source_count; ++source) {
        const u8 consumed = 15;
        u8 lanes{};
        for (u32 lane = 0; lane < 4; ++lane) {
            if (consumed & (1U << lane)) {
                const auto selector = source == 0 ? swizzle.GetSelectorSrc1(lane)
                    : source == 1 ? swizzle.GetSelectorSrc2(lane) : swizzle.GetSelectorSrc3(lane);
                lanes |= 1U << static_cast<u32>(selector);
            }
        }
        const u32 reg = sources[source].GetIndex();
        switch (sources[source].GetRegisterType()) {
        case nihstro::RegisterType::Input:
            if (reg >= 16) return ParallelVertexStatus::InvalidOperand;
            break;
        case nihstro::RegisterType::Temporary:
            if (reg >= 16) return ParallelVertexStatus::InvalidOperand;
            access.reads.temporary[reg] |= lanes;
            break;
        case nihstro::RegisterType::FloatUniform:
            if (reg >= 96 || relative[source] > 3) return ParallelVertexStatus::InvalidOperand;
            if (relative[source]) access.reads.addresses |= 1U << (relative[source] - 1);
            break;
        default:
            return ParallelVertexStatus::InvalidOperand;
        }
    }
    if (effective == Op::Id::MOVA) {
        access.writes.addresses = enabled & 3;
    } else if (effective == Op::Id::CMP) {
        access.writes.conditions = 3;
    } else if (destination < 16) {
        access.writes.output[destination] = enabled;
    } else if (destination < 32) {
        access.writes.temporary[destination - 16] = enabled;
    } else {
        return ParallelVertexStatus::InvalidOperand;
    }
    return ParallelVertexStatus::Independent;
}
} // namespace ParallelVertexDetail

// CodexAstraLocal: Build a bounded acyclic graph for this exact Boolean-uniform
// snapshot, then intersect definitions at all joins. Unsupported flow retains
// serial execution. Temporary containers exist only during certificate creation;
// callers cache the small result, not per-vertex graphs or guest payloads.
inline ParallelVertexCertificate AnalyzeParallelVertex(std::span<const u32> program,
                                                       std::span<const u32> swizzles,
                                                       u32 entry, u16 uniform_bools,
                                                       u32 output_register_mask) {
    using namespace ParallelVertexDetail;
    ParallelVertexCertificate result;
    if (program.empty() || program.size() > MAX_PROGRAM_CODE_LENGTH ||
        swizzles.empty() || swizzles.size() > MAX_SWIZZLE_DATA_LENGTH ||
        entry >= program.size() || entry >= MAX_PROGRAM_CODE_LENGTH - 1 ||
        (output_register_mask & ~0xffffU)) {
        return result;
    }
    const auto fail = [&](ParallelVertexStatus status, u32 pc, u32 reg = 0, u8 lanes = 0) {
        result.status = status; result.pc = pc; result.reg = reg; result.lanes = lanes;
        return result;
    };
    // CodexAstraLocal: JIT IF bodies contain implicit branches even when reached
    // by another label. Require the matching active true-IF frame, preventing an
    // interpreter-only certificate for jumps/calls into those compiled bodies.
    std::vector<Region> regions;
    for (u32 pc = 0; pc < program.size(); ++pc) {
        const nihstro::Instruction instruction{program[pc]};
        const auto op = instruction.opcode.Value().EffectiveOpCode();
        if (op != Op::Id::IFC && op != Op::Id::IFU) continue;
        const u32 otherwise = instruction.flow_control.dest_offset;
        const u32 end = otherwise + instruction.flow_control.num_instructions;
        if (otherwise <= pc || end >= MAX_PROGRAM_CODE_LENGTH - 1 || end > program.size()) {
            return fail(ParallelVertexStatus::InvalidScope, pc);
        }
        if (regions.size() == MaxRegions) return fail(ParallelVertexStatus::WorkLimit, pc);
        regions.push_back({pc, otherwise, end});
    }
    std::vector<Node> nodes;
    std::map<Context, u32> lookup;
    nodes.reserve(std::min<std::size_t>(program.size() * 2, MaxNodes));
    auto add_node = [&](const Context& context, u32& index) {
        if (context.pc >= program.size() || context.pc >= MAX_PROGRAM_CODE_LENGTH - 1) {
            result = fail(ParallelVertexStatus::MissingEnd, context.pc); return false;
        }
        if (context.calls && (context.pc < context.call[context.calls - 1].begin ||
                              context.pc >= context.call[context.calls - 1].end)) {
            result = fail(ParallelVertexStatus::InvalidScope, context.pc); return false;
        }
        for (u32 i = 0; i < context.ifs; ++i) {
            const auto& frame = context.branch[i];
            if (frame.call_depth > context.calls ||
                (frame.call_depth == context.calls &&
                 (context.pc <= frame.instruction || context.pc >= frame.otherwise))) {
                result = fail(ParallelVertexStatus::InvalidScope, context.pc); return false;
            }
        }
        for (const auto& region : regions) {
            if (context.pc <= region.instruction || context.pc >= region.otherwise) continue;
            bool active{};
            for (u32 i = 0; i < context.ifs; ++i) {
                active |= context.branch[i].instruction == region.instruction &&
                          context.branch[i].call_depth == context.calls;
            }
            if (!active) {
                result = fail(ParallelVertexStatus::InvalidScope, context.pc); return false;
            }
        }
        if (const auto found = lookup.find(context); found != lookup.end()) {
            index = found->second; return true;
        }
        if (nodes.size() == MaxNodes) {
            result = fail(ParallelVertexStatus::WorkLimit, context.pc); return false;
        }
        index = static_cast<u32>(nodes.size());
        lookup.emplace(context, index);
        nodes.push_back({.context = context});
        result.nodes = static_cast<u32>(nodes.size());
        return true;
    };
    u32 first{};
    if (!add_node(Context{.pc = entry}, first)) return result;
    Definitions possible;
    for (u32 at = 0; at < nodes.size(); ++at) {
        const auto context = nodes[at].context;
        const u32 pc = context.pc;
        const nihstro::Instruction instruction{program[pc]};
        const auto opcode = instruction.opcode.Value();
        const auto op = opcode.EffectiveOpCode();
        const auto type = opcode.GetInfo().type;
        if (op == Op::Id::END) {
            if (context.calls || context.ifs) return fail(ParallelVertexStatus::InvalidScope, pc);
            nodes[at].terminal = true;
            continue;
        }
        const bool call = op == Op::Id::CALL || op == Op::Id::CALLU || op == Op::Id::CALLC;
        const bool branch = op == Op::Id::IFU || op == Op::Id::IFC;
        const bool jump = op == Op::Id::JMPU || op == Op::Id::JMPC;
        const bool condition = op == Op::Id::IFC || op == Op::Id::JMPC || op == Op::Id::CALLC;
        if (type == Op::Type::Arithmetic || type == Op::Type::MultiplyAdd) {
            const auto status = Decode(instruction, swizzles, nodes[at].access);
            if (status != ParallelVertexStatus::Independent) return fail(status, pc);
        } else if (!call && !branch && !jump && op != Op::Id::NOP) {
            return fail(ParallelVertexStatus::UnsupportedInstruction, pc);
        }
        if (condition) {
            using Condition = nihstro::Instruction::FlowControlType::Op;
            const auto code = instruction.flow_control.op.Value();
            nodes[at].access.reads.conditions = code == Condition::JustX ? 1
                                               : code == Condition::JustY ? 2 : 3;
        }
        Union(possible, nodes[at].access.writes);
        const u32 target = instruction.flow_control.dest_offset;
        const u32 length = instruction.flow_control.num_instructions;
        bool uniform_taken = true;
        if (op == Op::Id::CALLU || op == Op::Id::IFU || op == Op::Id::JMPU) {
            uniform_taken = (uniform_bools & (1U << instruction.flow_control.bool_uniform_id)) != 0;
            if (op == Op::Id::JMPU && (length & 1U)) uniform_taken = !uniform_taken;
        }
        const u32 alternatives = condition ? 2 : 1;
        for (u32 alternative = 0; alternative < alternatives; ++alternative) {
            const bool taken = condition ? alternative != 0 : uniform_taken;
            Context next = context;
            next.pc = pc + 1;
            const bool closes_call = context.calls && context.call[context.calls - 1].end == pc + 1;
            const bool closes_if = context.ifs && context.branch[context.ifs - 1].otherwise == pc + 1;
            // CodexAstraLocal: Interpreter postinstruction closure can override
            // a taken transfer, whereas JIT labels bypass its return/IF epilogue.
            // Reject those mixed semantics instead of choosing one backend.
            if ((branch || ((call || jump) && taken)) && (closes_call || closes_if)) {
                return fail(ParallelVertexStatus::AmbiguousClosure, pc);
            }
            if (call && taken) {
                if (!length || target >= MAX_PROGRAM_CODE_LENGTH - 1 ||
                    target + length > program.size() || target + length >= MAX_PROGRAM_CODE_LENGTH - 1 ||
                    (target <= pc && pc < target + length)) {
                    return fail(ParallelVertexStatus::InvalidScope, pc);
                }
                if (next.calls == MaxCalls) return fail(ParallelVertexStatus::StackLimit, pc);
                next.call[next.calls++] = {target, target + length, pc + 1};
                next.pc = target;
            }
            if (branch) {
                if (target <= pc || target + length > program.size() ||
                    target + length >= MAX_PROGRAM_CODE_LENGTH - 1) {
                    return fail(ParallelVertexStatus::InvalidScope, pc);
                }
                if (taken) {
                    if (next.ifs == MaxIfs) return fail(ParallelVertexStatus::StackLimit, pc);
                    next.branch[next.ifs++] = {pc, target, target + length, context.calls};
                } else {
                    next.pc = target;
                }
            }
            if (jump && taken) next.pc = target;
            u32 boundary = pc + 1;
            bool returned{};
            while (next.calls && next.call[next.calls - 1].end == boundary) {
                boundary = next.call[next.calls - 1].return_pc;
                next.call[--next.calls] = {};
                next.pc = boundary;
                returned = true;
            }
            if (closes_if) {
                if (returned) return fail(ParallelVertexStatus::AmbiguousClosure, pc);
                next.pc = next.branch[next.ifs - 1].end;
                next.branch[--next.ifs] = {};
            }
            u32 successor{};
            if (!add_node(next, successor)) return result;
            bool duplicate{};
            for (u32 n = 0; n < nodes[at].successors; ++n) duplicate |= nodes[at].next[n] == successor;
            if (!duplicate) {
                nodes[at].next[nodes[at].successors++] = successor;
                ++nodes[successor].incoming;
            }
        }
    }
    // CodexAstraLocal: Complete all predecessor joins before testing reads.
    // A backward jump is acceptable only if this exact context graph is acyclic;
    // no guessed iteration limit or truncated loop becomes an independence proof.
    std::vector<u32> order;
    order.reserve(nodes.size());
    for (u32 i = 0; i < nodes.size(); ++i) if (!nodes[i].incoming) order.push_back(i);
    for (u32 i = 0; i < order.size(); ++i) {
        for (u32 n = 0; n < nodes[order[i]].successors; ++n) {
            const u32 successor = nodes[order[i]].next[n];
            if (!--nodes[successor].incoming) order.push_back(successor);
        }
    }
    if (order.size() != nodes.size()) return fail(ParallelVertexStatus::CyclicFlow, entry);
    std::vector<Definitions> definitions(nodes.size());
    std::vector<bool> arrived(nodes.size());
    arrived[first] = true;
    bool terminal{};
    for (const u32 index : order) {
        const auto& node = nodes[index];
        auto state = definitions[index];
        for (u32 reg = 0; reg < 16; ++reg) {
            const u8 missing = node.access.reads.temporary[reg] & possible.temporary[reg] & ~state.temporary[reg];
            if (missing) return fail(ParallelVertexStatus::TemporaryCarry, node.context.pc, reg, missing);
        }
        const u8 address = node.access.reads.addresses & possible.addresses & ~state.addresses;
        if (address) return fail(ParallelVertexStatus::AddressCarry, node.context.pc, 0, address);
        const u8 condition = node.access.reads.conditions & possible.conditions & ~state.conditions;
        if (condition) return fail(ParallelVertexStatus::ConditionCarry, node.context.pc, 0, condition);
        Union(state, node.access.writes);
        if (node.terminal) {
            terminal = true;
            for (u32 reg = 0; reg < 16; ++reg) {
                const u8 missing = possible.output[reg] & ~state.output[reg];
                if ((output_register_mask & (1U << reg)) && missing) {
                    return fail(ParallelVertexStatus::OutputCarry, node.context.pc, reg, missing);
                }
            }
        }
        for (u32 n = 0; n < node.successors; ++n) {
            const u32 successor = node.next[n];
            if (!arrived[successor]) definitions[successor] = state;
            else Intersect(definitions[successor], state);
            arrived[successor] = true;
        }
    }
    if (!terminal) return fail(ParallelVertexStatus::MissingEnd, entry);
    result.status = ParallelVertexStatus::Independent;
    return result;
}
} // namespace Pica

// CodexAstraLocal: Synthetic ISA cases and raw input seeds for the observable
// certificate gate. The observable table below supplies its distinct admission
// contract; no retained title program, hash or payload is embedded here.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include <nihstro/shader_bytecode.h>

namespace CarryChallenge {
using Op = nihstro::OpCode::Id;
using Src = nihstro::SourceRegister;
using Dst = nihstro::DestRegister;
using Swizzle = nihstro::SwizzlePattern;
enum class Expect { Accept, Reject };

// CodexAstraLocal: Keep exact source and an explicit refusal expectation beside
// every case; adapters may map their own status names without weakening accept.
struct Case {
    std::string name;
    Pica::ProgramCode code{};
    Pica::SwizzleData swizzles{};
    u32 entry{}, descriptors{};
    u16 bools{}, output_mask{1};
    Expect expect{Expect::Accept};
    bool safe_to_execute{true};
};

// CodexAstraLocal: Encode actual ISA bitfields, with lane masks in XYZW bit0..3
// order and explicit descriptor selectors. A failed bound is a fixture failure.
struct Builder {
    Case value;
    u32 pc{};
    explicit Builder(std::string name) {
        value.name = std::move(name);
        nihstro::Instruction end{};
        end.opcode = Op::END;
        value.code.fill(end.hex);
    }
    void At(u32 next) { pc = next; }
    void Arithmetic(Op op, Dst dst, u8 mask, Src a, Src b = Src::MakeInput(1),
                    Src c = Src::MakeInput(2),
                    std::array<u8, 4> sa = {0, 1, 2, 3},
                    std::array<u8, 4> sb = {0, 1, 2, 3},
                    std::array<u8, 4> sc = {0, 1, 2, 3},
                    u8 negate = 0, u32 relative = 0) {
        Swizzle sw{};
        for (u32 lane = 0; lane < 4; ++lane) {
            sw.SetDestComponentEnabled(lane, (mask & (1U << lane)) != 0);
            sw.SetSelectorSrc1(lane, static_cast<Swizzle::Selector>(sa[lane]));
            sw.SetSelectorSrc2(lane, static_cast<Swizzle::Selector>(sb[lane]));
            sw.SetSelectorSrc3(lane, static_cast<Swizzle::Selector>(sc[lane]));
        }
        sw.negate_src1 = (negate & 1) != 0;
        sw.negate_src2 = (negate & 2) != 0;
        sw.negate_src3 = (negate & 4) != 0;
        u32 descriptor{};
        while (descriptor < value.descriptors && value.swizzles[descriptor] != sw.hex)
            ++descriptor;
        if (descriptor == value.descriptors) value.swizzles[value.descriptors++] = sw.hex;
        nihstro::Instruction ins{};
        ins.opcode = op;
        const auto info = nihstro::OpCode(op).GetInfo();
        const bool inverted = (info.subtype & nihstro::OpCode::Info::SrcInversed) != 0;
        if (info.type == nihstro::OpCode::Type::MultiplyAdd) {
            if (descriptor >= 32) throw std::runtime_error("MAD descriptor bound");
            ins.mad.dest = dst;
            ins.mad.src1 = a;
            ins.mad.operand_desc_id = descriptor;
            ins.mad.address_register_index = relative;
            if (inverted) { ins.mad.src2i = b; ins.mad.src3i = c; }
            else { ins.mad.src2 = b; ins.mad.src3 = c; }
        } else {
            ins.common.dest = dst;
            ins.common.operand_desc_id = descriptor;
            ins.common.address_register_index = relative;
            if (inverted) { ins.common.src1i = a; ins.common.src2i = b; }
            else { ins.common.src1 = a; ins.common.src2 = b; }
            if (op == Op::CMP) {
                ins.common.compare_op.x = nihstro::Instruction::Common::CompareOpType::LessThan;
                ins.common.compare_op.y = nihstro::Instruction::Common::CompareOpType::GreaterEqual;
            }
        }
        value.code.at(pc++) = ins.hex;
    }
    void Flow(Op op, u32 target = 0, u32 length = 0, u32 boolean = 0) {
        nihstro::Instruction ins{};
        ins.opcode = op;
        ins.flow_control.dest_offset = target;
        ins.flow_control.num_instructions = length;
        ins.flow_control.bool_uniform_id = boolean;
        if (op == Op::IFC || op == Op::CALLC || op == Op::JMPC) {
            ins.flow_control.op = nihstro::Instruction::FlowControlType::JustX;
            ins.flow_control.refx = 1;
        }
        value.code.at(pc++) = ins.hex;
    }
    void Output() { Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeInput(0)); }
    void Refuse(bool executable = true) {
        value.expect = Expect::Reject;
        value.safe_to_execute = executable;
    }
    Case Finish() { Flow(Op::END); return std::move(value); }
};

// CodexAstraLocal: Seed cases exercise recurrence, status-only and overwritten
// state. The observable table below replaces historical state-only refusals with
// the final output/control expectations; unsafe flow remains non-executable.
inline std::vector<Case> BuildCases() {
    std::vector<Case> cases;
    const auto r0 = Src::MakeTemporary(0);
    const auto r1 = Src::MakeTemporary(1);
    const auto v0 = Src::MakeInput(0);
    const auto v1 = Src::MakeInput(1);
    for (auto op : {Op::ADD, Op::MUL, Op::MAD}) {
        for (u32 mask = 0; mask < 16; ++mask) {
            Builder b("masked_" + std::to_string(static_cast<u32>(op)) + "_" + std::to_string(mask));
            b.Output();
            b.Arithmetic(op, Dst::MakeTemporary(0), mask, r0, v1, Src::MakeInput(2));
            cases.push_back(b.Finish());
        }
    }
    {
        Builder b("swizzled_alias_recurrence"); b.Output();
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, r0, v1, Src::MakeInput(2), {3, 0, 1, 2});
        cases.push_back(b.Finish());
    }
    {
        Builder b("dependent_mov_then_recurrence"); b.Output();
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(1), 15, r0);
        b.Arithmetic(Op::MUL, Dst::MakeTemporary(0), 15, r1, v1);
        cases.push_back(b.Finish());
    }
    {
        Builder b("discarded_status_then_independent_reset"); b.Output();
        b.Arithmetic(Op::MAD, Dst::MakeTemporary(2), 0, r0, v1, Src::MakeInput(2));
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 15, Src::MakeInput(3));
        cases.push_back(b.Finish());
    }
    {
        Builder b("independent_negated_operand"); b.Output();
        b.Arithmetic(Op::MUL, Dst::MakeTemporary(0), 15, r0, v1, Src::MakeInput(2),
                     {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, 2);
        cases.push_back(b.Finish());
    }
    for (u32 lane = 0; lane < 4; ++lane) {
        Builder b("dp3_source_lane_" + std::to_string(lane)); b.Output();
        b.Arithmetic(Op::DP3, Dst::MakeTemporary(1), 15, r0, v1);
        b.Arithmetic(Op::MUL, Dst::MakeTemporary(0), 1U << lane, r0, v1);
        if (lane != 3) b.Refuse();
        cases.push_back(b.Finish());
    }
    for (u32 out : {0U, 1U}) {
        Builder b("carried_output_" + std::to_string(out)); b.Output();
        b.Arithmetic(Op::MOV, Dst::MakeOutput(out), 1, r0);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 1, r0, v1);
        b.Refuse();
        cases.push_back(b.Finish());
    }
    {
        Builder b("carried_final_condition"); b.Output();
        b.Arithmetic(Op::CMP, Dst::MakeTemporary(0), 3, r0, v1);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 3, r0, v1);
        b.Refuse(); cases.push_back(b.Finish());
    }
    {
        Builder b("carried_final_address"); b.Output();
        b.Arithmetic(Op::MOVA, Dst::MakeTemporary(0), 3, r0);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 3, r0, v1);
        b.Refuse(); cases.push_back(b.Finish());
    }
    {
        Builder b("carried_relative_uniform"); b.Output();
        b.Arithmetic(Op::MOVA, Dst::MakeTemporary(0), 1, r0);
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(1), 15, Src::MakeFloat(0), v1,
                     Src::MakeInput(2), {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, 0, 1);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 1, r0, v1);
        b.Refuse(); cases.push_back(b.Finish());
    }
    for (auto op : {Op::DP4, Op::RCP, Op::RSQ, Op::FLR, Op::MAX}) {
        Builder b("unsupported_carried_" + std::to_string(static_cast<u32>(op))); b.Output();
        b.Arithmetic(op, Dst::MakeTemporary(0), 15, r0, v1);
        b.Refuse(); cases.push_back(b.Finish());
    }
    {
        Builder b("negated_carried_operand"); b.Output();
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, r0, v1, Src::MakeInput(2),
                     {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, 1);
        b.Refuse(); cases.push_back(b.Finish());
    }
    for (bool incompatible : {false, true}) {
        Builder b(incompatible ? "same_pc_incompatible_contexts" : "same_pc_two_occurrences");
        b.Output(); b.Flow(Op::CALL, 20, 1);
        if (incompatible) b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 15, v0);
        b.Flow(Op::CALL, 20, 1); b.Flow(Op::END);
        b.At(20); b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, r0, v1);
        if (incompatible) b.Refuse();
        cases.push_back(b.Finish());
    }
    {
        Builder b("independent_conditional_event_presence"); b.Output();
        b.Arithmetic(Op::CMP, Dst::MakeTemporary(0), 3, v0, v1);
        b.Flow(Op::IFC, 5, 1);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, r0, v1);
        b.Flow(Op::NOP);
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 15, Src::MakeInput(2));
        cases.push_back(b.Finish());
    }
    {
        Builder b("carried_branch_condition"); b.Output();
        b.Flow(Op::IFC, 4, 1);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, r0, v1);
        b.Flow(Op::NOP);
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 15, v0);
        b.Arithmetic(Op::CMP, Dst::MakeTemporary(0), 3, v0, v1);
        b.Refuse(); cases.push_back(b.Finish());
    }
    for (u32 count : {16U, 17U}) {
        Builder b("tracked_lane_bound_" + std::to_string(count)); b.Output();
        for (u32 index = 0; index < count; ++index) {
            const u32 reg = index / 4, lane = index % 4;
            b.Arithmetic(Op::ADD, Dst::MakeTemporary(reg), 1U << lane,
                         Src::MakeTemporary(reg), v1);
        }
        if (count > 16) b.Refuse();
        cases.push_back(b.Finish());
    }
    for (u32 count : {64U, 65U}) {
        Builder b("event_bound_" + std::to_string(count)); b.Output();
        for (u32 index = 0; index < count; ++index)
            b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 1, r0, v1);
        if (count > 64) b.Refuse();
        cases.push_back(b.Finish());
    }
    {
        Builder b("loop_refused"); b.Output(); b.Flow(Op::LOOP, 3, 0);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 1, r0, v1); b.Flow(Op::NOP);
        b.Refuse(false); cases.push_back(b.Finish());
    }
    {
        Builder b("recursive_call_refused"); b.Output(); b.Flow(Op::CALL, 0, 3);
        b.Refuse(false); cases.push_back(b.Finish());
    }
    if (cases.size() > 96) throw std::runtime_error("finite corpus case cap");
    return cases;
}

// CodexAstraLocal: Seed only raw IEEE bits without host arithmetic. The rotated
// set puts sNaN/qNaN/zeros/subnormals in enabled lanes across seeds and invocations;
// the separate input updater also supplies finite changing values.
inline constexpr std::array<u32, 16> Special = {
    0x00000000, 0x80000000, 0x3fc00000, 0xbf000000,
    0x7f800000, 0xff800000, 0x7fc12345, 0xffc54321,
    0x7f800001, 0xff812345, 0x00000001, 0x807fffff,
    0x00800000, 0x7f7fffff, 0x3f400000, 0xc0200000};
inline void Raw(Pica::f24& out, u32 bits) { std::memcpy(&out, &bits, sizeof(bits)); }
inline void Seed(Pica::ShaderUnit& unit, u32 seed, bool bank) {
    unit.output_bank = bank;
    for (u32 reg = 0; reg < 16; ++reg) for (u32 lane = 0; lane < 4; ++lane) {
        const u32 bits = Special[(seed + reg * 3 + lane) % Special.size()];
        Raw(unit.temporary[reg][lane], bits);
        Raw(unit.input[reg][lane], bits ^ 0x80000000U);
        for (u32 output_bank = 0; output_bank < 2; ++output_bank)
            Raw(unit.output[output_bank][reg][lane], bits ^ (output_bank ? 0x80000000U : 0));
    }
    for (u32 n = 0; n < 3; ++n) unit.address_registers[n] = s32(seed % 3) - 1;
    unit.conditional_code[0] = (seed & 1) != 0;
    unit.conditional_code[1] = (seed & 2) != 0;
}
inline void SetInput(Pica::ShaderUnit& unit, u32 vertex, bool special) {
    for (u32 reg = 0; reg < 16; ++reg) for (u32 lane = 0; lane < 4; ++lane) {
        const u32 bits = special ? Special[(vertex * 3 + reg * 5 + lane) % Special.size()]
                                 : std::bit_cast<u32>(float(1 + ((vertex * 3 + reg + lane) % 13)) / 8);
        Raw(unit.input[reg][lane], bits);
    }
}

// CodexAstraLocal: Compare all semantic register bytes in both banks, avoiding
// compiler padding and the owner-only emitter pointer. FP flags are separate.
using RawState = std::array<u32, 262>;
inline RawState Capture(const Pica::ShaderUnit& unit) {
    RawState bytes{};
    u32 next{};
    for (const auto* bank : {&unit.input, &unit.temporary, &unit.output[0], &unit.output[1]})
        for (const auto& reg : *bank) for (u32 lane = 0; lane < 4; ++lane)
            std::memcpy(&bytes[next++], &reg[lane], 4);
    for (s32 address : unit.address_registers) bytes[next++] = std::bit_cast<u32>(address);
    bytes[next++] = unit.conditional_code[0];
    bytes[next++] = unit.conditional_code[1];
    bytes[next++] = unit.output_bank;
    if (next != bytes.size()) throw std::runtime_error("raw-state field count");
    return bytes;
}

} // namespace CarryChallenge

// CodexAstraLocal: Relabel only state/status-only refusals for this separate
// output/control contract, then add live reductions, aliases and joins.
namespace ObservableChallenge {
using namespace CarryChallenge;

inline std::vector<Case> BuildCases() {
    auto cases = CarryChallenge::BuildCases();
    // CodexAstraLocal: These five remain outside the output/control contract.
    // Other old refusals concern dead storage/status or replay-record bounds.
    constexpr std::array<std::string_view, 5> refused{
        "carried_output_0", "carried_relative_uniform", "carried_branch_condition",
        "loop_refused", "recursive_call_refused"};
    for (auto& test : cases) {
        const bool rejected = std::find(refused.begin(), refused.end(), test.name) != refused.end();
        test.expect = rejected ? Expect::Reject : Expect::Accept;
    }
    const auto r0 = Src::MakeTemporary(0);
    const auto v0 = Src::MakeInput(0);
    const auto v1 = Src::MakeInput(1);
    const auto add = [&](Builder& b, bool accepted) {
        if (!accepted) b.Refuse();
        cases.push_back(b.Finish());
    };

    // CodexAstraLocal: Source taints must be snapshotted before any lane write;
    // writing X first must not erase the old X dependency of swapped Y.
    for (u32 lane : {0U, 1U}) {
        Builder b("observable_alias_swap_lane_" + std::to_string(lane)); b.Output();
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 2, v0);
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 3, r0, v1, Src::MakeInput(2), {1,0,2,3});
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 1U << lane, r0);
        add(b, lane == 0);
    }
    {
        Builder b("observable_swizzle_future_carry"); b.Output();
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 1, r0, v1, Src::MakeInput(2), {3,0,1,2});
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 8, r0, v1);
        add(b, false);
    }

    // CodexAstraLocal: The actual reductions distinguish discarded W from a
    // live W product, including DPH's replacement of only source1.W with one.
    for (auto op : {Op::DP3, Op::DP4}) {
        Builder b("observable_dot_" + std::to_string(static_cast<u32>(op)));
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 7, v0);
        b.Arithmetic(op, Dst::MakeOutput(0), 15, r0, v1);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 8, r0, v1);
        add(b, op == Op::DP3);
    }
    for (bool second : {false, true}) {
        Builder b(second ? "observable_dph_source2_W" : "observable_dph_source1_W");
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 7, v0);
        b.Arithmetic(Op::DPH, Dst::MakeOutput(0), 15, second ? v1 : r0, second ? r0 : v1);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 8, r0, v1);
        add(b, !second);
    }

    // CodexAstraLocal: Scalar instructions broadcast selected X, even though
    // their swizzle load/negation may also touch irrelevant vector components.
    for (auto op : {Op::RCP, Op::RSQ, Op::EX2, Op::LG2}) for (bool x : {false, true}) {
        Builder b("observable_scalar_" + std::to_string(static_cast<u32>(op)) + (x ? "_X" : "_YZW"));
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), x ? 14 : 1, v0);
        b.Arithmetic(op, Dst::MakeOutput(0), 15, r0);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), x ? 1 : 14, r0, v1);
        add(b, !x);
    }

    // CodexAstraLocal: Every reachable alternative must define the selected
    // value; an independent condition does not make a one-sided definition safe.
    for (bool both : {false, true}) {
        Builder b(both ? "observable_join_both_defined" : "observable_join_one_defined"); b.Output();
        b.Arithmetic(Op::CMP, Dst::MakeTemporary(1), 3, v0, v1);
        b.Flow(Op::IFC, 5, 1);
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 1, v0);
        b.Flow(Op::NOP);
        if (both) b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 1, v1);
        else b.Flow(Op::NOP);
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 1, r0);
        add(b, both);
    }
    {
        auto test = *std::find_if(cases.begin(), cases.end(), [](const auto& item) {
            return item.name == "carried_output_1";
        });
        test.name = "observable_changed_output_mask";
        test.output_mask = 3;
        test.expect = Expect::Reject;
        cases.push_back(std::move(test));
    }

    // CodexAstraLocal: Tainted but unread condition/address destinations may
    // differ; the exact later selector determines whether control is observable.
    for (bool y : {false, true}) {
        Builder b(y ? "observable_condition_Y_used" : "observable_condition_Y_dead"); b.Output();
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 1, v0);
        b.Arithmetic(Op::CMP, Dst::MakeTemporary(1), 3, r0, v1);
        const auto branch = b.pc;
        b.Flow(Op::IFC, 6, 1);
        nihstro::Instruction ins{b.value.code[branch]};
        ins.flow_control.op = y ? nihstro::Instruction::FlowControlType::JustY
                                : nihstro::Instruction::FlowControlType::JustX;
        ins.flow_control.refy = 1;
        b.value.code[branch] = ins.hex;
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(1), 15, v0, v1);
        b.Flow(Op::NOP);
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(1), 15, v0);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 2, r0, v1);
        add(b, !y);
    }
    for (u32 address : {1U, 2U}) {
        Builder b("observable_address_" + std::to_string(address)); b.Output();
        b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 1, v0);
        b.Arithmetic(Op::MOVA, Dst::MakeTemporary(1), 3, r0);
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeFloat(0), v1,
                     Src::MakeInput(2), {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, 0, address);
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 2, r0, v1);
        add(b, address == 1);
    }

    // CodexAstraLocal: A late independent write still changes the next call's
    // input state. Only an early overwrite kills that inter-invocation dependence.
    for (bool early : {false, true}) {
        Builder b(early ? "observable_early_reset" : "observable_late_reset"); b.Output();
        if (early) b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 15, v1);
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, r0);
        if (!early) b.Arithmetic(Op::MOV, Dst::MakeTemporary(0), 15, v1);
        add(b, early);
    }
    {
        Builder b("observable_never_written_temp");
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeTemporary(15));
        add(b, true);
    }
    for (bool uniform : {false, true}) {
        Builder b(uniform ? "observable_uniform_taken" : "observable_uniform_not_taken"); b.Output();
        b.value.bools = uniform;
        b.Flow(Op::IFU, 4, 1);
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, r0);
        b.Flow(Op::NOP);
        b.Output();
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, r0, v1);
        add(b, !uniform);
    }
    {
        Builder b("observable_emit_refused"); b.Output(); b.Flow(Op::EMIT);
        b.Refuse(false);
        cases.push_back(b.Finish());
    }
    if (cases.size() > 112) throw std::runtime_error("observable corpus case cap");
    return cases;
}
} // namespace ObservableChallenge

// CodexAstraLocal: Guard removal must change actual selected output, rather
// than merely disagreeing with an expected admission label.
namespace ObservableSensitive {
using namespace CarryChallenge;
inline std::vector<Case> BuildCases() {
    std::vector<Case> cases;
    {
        // CodexAstraLocal: The next invocation selects genuinely different
        // input vectors using this invocation's final CMP. A fresh grain's
        // condition therefore cannot replace the serial carried condition.
        Builder b("observable_sensitive_carried_branch"); b.Output();
        b.Flow(Op::IFC, 4, 1);
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeInput(1));
        b.Flow(Op::NOP);
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeInput(2));
        b.Arithmetic(Op::CMP, Dst::MakeTemporary(0), 3, Src::MakeInput(0), Src::MakeInput(1));
        b.Refuse();
        cases.push_back(b.Finish());
    }
    {
        // CodexAstraLocal: The fixture must supply distinct immutable uniform
        // vectors (the reviewed A64 fixture already does). Current output uses
        // the preceding invocation's address before live input replaces it.
        Builder b("observable_sensitive_carried_address");
        b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeFloat(0),
                     Src::MakeInput(1), Src::MakeInput(2),
                     {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, 0, 1);
        b.Arithmetic(Op::MOVA, Dst::MakeTemporary(0), 1, Src::MakeInput(0));
        b.Refuse();
        cases.push_back(b.Finish());
    }
    return cases;
}
} // namespace ObservableSensitive

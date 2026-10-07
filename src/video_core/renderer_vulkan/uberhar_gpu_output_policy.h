// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <bit>
#include <memory>
#include <vector>
#include <nihstro/shader_bytecode.h>
#include "video_core/pica/regs_rasterizer.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_setup.h"

namespace Vulkan::ReadyVertexPolicy {

// CodexAstraUlt: Only physical W has the reproduced never-written initial-value
// disagreement: CPU output registers start at zero, generated GPU outputs at
// (0,0,0,1). Compose packed outputs and last-writer semantic mapping; unmapped
// semantics and padding slots are not reads of a physical output register.
// CodexAstraLocal: Audit the recovered guard against ShaderUnit::WriteOutput,
// OutputVertex and GetSemanticMaps. All use ascending packed outputs followed by
// component order, so only the final source of each consumed semantic matters.
inline u16 ConsumedOutputW(const Pica::ShaderRegs& shader,
                          const Pica::RasterizerRegs& rasterizer) {
    const u32 count = rasterizer.vs_output_total;
    // CodexAstraLocal: Value() extracts the 16 physical output bits; reserved
    // bits in the backing register must not name nonexistent output registers.
    u32 mask = shader.output_mask.Value();
    if (count > std::size(rasterizer.vs_output_attributes) ||
        std::popcount(mask) < static_cast<int>(count))
        return 0; // CodexAstraLocal: Incomplete transport remains outside this narrow guard.

    std::array<u8, 24> sources;
    sources.fill(64);
    for (u32 attribute = 0; attribute < count; ++attribute) {
        const u32 reg = std::countr_zero(mask);
        mask &= mask - 1;
        const auto mapping = rasterizer.vs_output_attributes[attribute];
        const std::array<u32, 4> semantics{
            mapping.map_x, mapping.map_y, mapping.map_z, mapping.map_w};
        for (u32 component = 0; component < 4; ++component) {
            if (semantics[component] < sources.size())
                sources[semantics[component]] = static_cast<u8>(reg * 4 + component);
        }
    }
    u16 consumed = 0;
    for (u32 semantic = 0; semantic < sources.size(); ++semantic) {
        if (semantic == 17 || semantic == 21)
            continue; // CodexAstraLocal: OutputVertex padding is not read by generated EmitVtx.
        const u32 source = sources[semantic];
        if (source < 64 && source % 4 == 3)
            consumed |= static_cast<u16>(1U << (source / 4));
    }
    return consumed;
}

// CodexAstraUlt: This is a conservative union of POSSIBLE writes across ALL
// words, including unreachable code and both sides of branches. Absence proves
// the lane stays zero on CPU. Presence does not prove per-invocation writes or
// general parity: conditional output/temp/address/condition carry remains open.
// CodexAstraLocal: Match nihstro's separate common/MAD destination layouts and
// reversed destination-mask order. CMP/MOVA have no output destination. Unknown
// words cancel this absence proof, including unknown words after END; no control
// flow, entry-point, uniform or per-vertex reachability claim is made here.
inline u16 PossibleOutputWWrites(const Pica::ProgramCode& program,
                                const Pica::SwizzleData& swizzles) {
    using nihstro::OpCode;
    u16 written = 0;
    for (const u32 word : program) {
        const nihstro::Instruction instruction{word};
        const auto opcode = instruction.opcode.Value();
        const auto info = opcode.GetInfo();
        u32 destination, descriptor;
        if (info.type == OpCode::Type::Arithmetic && (info.subtype & OpCode::Info::Dest)) {
            destination = instruction.common.dest.Value();
            descriptor = instruction.common.operand_desc_id;
        } else if (info.type == OpCode::Type::MultiplyAdd) {
            destination = instruction.mad.dest.Value();
            descriptor = instruction.mad.operand_desc_id;
        } else if (info.type == OpCode::Type::Unknown) {
            return 0xffff; // CodexAstraLocal: Unknown words cannot establish absence of writes.
        } else {
            continue; // CodexAstraLocal: Includes CMP/MOVA, which do not write output registers.
        }
        if (destination < 16 &&
            nihstro::SwizzlePattern{swizzles[descriptor]}.DestComponentEnabled(3))
            written |= static_cast<u16>(1U << destination);
    }
    return written;
}

// CodexAstraUlt: Bound immutable memoization to the optional program budget.
// Hashes are lookup hints only: every hit compares complete program/swizzle
// words, so collisions and in-place ShaderSetup changes cannot reuse a verdict.
// FIFO replacement never changes admission; it only causes another bounded scan.
// Entries allocate lazily (32 KiB each), exclusively when Combo checks a shader.
// CodexAstraLocal: Each hit still compares 32 KiB plus scans at most Capacity
// hashes. This cost and CPU fallback coverage need device measurement; the memo
// is bounded storage, not evidence of improved throughput or overall GPU memory.
template <std::size_t Capacity = 128>
class OutputWriteMemo {
    static_assert(Capacity > 0);
public:
    u16 Get(const Pica::ProgramCode& program, const Pica::SwizzleData& swizzles,
            u64 program_hash, u64 swizzle_hash) {
        for (const auto& entry : entries) {
            if (entry->program_hash == program_hash && entry->swizzle_hash == swizzle_hash &&
                entry->program == program && entry->swizzles == swizzles) {
                ++hits;
                return entry->written;
            }
        }
        ++scans;
        auto entry = std::make_unique<Entry>();
        entry->program_hash = program_hash;
        entry->swizzle_hash = swizzle_hash;
        entry->program = program;
        entry->swizzles = swizzles;
        entry->written = PossibleOutputWWrites(program, swizzles);
        const u16 written = entry->written;
        if (entries.size() < Capacity) {
            entries.push_back(std::move(entry));
        } else {
            entries[next] = std::move(entry);
            next = (next + 1) % Capacity;
        }
        return written;
    }
    std::size_t Size() const { return entries.size(); }
    u64 Hits() const { return hits; }
    u64 Scans() const { return scans; }

private:
    struct Entry {
        Pica::ProgramCode program;
        Pica::SwizzleData swizzles;
        u64 program_hash{}, swizzle_hash{};
        u16 written{};
    };
    std::vector<std::unique_ptr<Entry>> entries;
    std::size_t next{};
    u64 hits{}, scans{};
};

} // CodexAstraLocal: namespace Vulkan::ReadyVertexPolicy

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include <array>
#include <bit>
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/uberhar_vertex_input.h"

namespace SwRenderer {
// CodexAstraLocal TEST-ONLY LOG: Retain at most16 demanded nonfinite outputs
// and their actual scalar reads after the first capsule. Explicit semantic bits
// exclude padding and preserve NaN payloads. This proves matching values only,
// never unique assembler identity or the original guest writer. Remove after
// the demanded Software terminal fault has a causal reproduction.
class SoftwareSourceHistory {
public:
    static constexpr u32 Capacity = 16;
    static constexpr u32 InvocationLimit = 4096;
    using Semantics = std::array<u32, 22>;
    struct Record {
        Semantics output{};
        Pica::NativeInputReadTrace input{};
        u64 event{}, draw{};
        u32 index{}, vertex{};
        bool actual_reads{};
    };
    struct Match {
        const Record* latest{};
        u32 count{};
    };
    static Semantics Bits(const Pica::OutputVertex& output) {
        Semantics result{};
        u32 index{};
        const auto append = [&](Pica::f24 value) {
            result[index++] = std::bit_cast<u32>(value.ToFloat32());
        };
        for (u32 lane = 0; lane < 4; ++lane) append(output.pos[lane]);
        for (u32 lane = 0; lane < 4; ++lane) append(output.quat[lane]);
        for (u32 lane = 0; lane < 4; ++lane) append(output.color[lane]);
        for (u32 lane = 0; lane < 2; ++lane) append(output.tc0[lane]);
        for (u32 lane = 0; lane < 2; ++lane) append(output.tc1[lane]);
        append(output.tc0_w);
        for (u32 lane = 0; lane < 3; ++lane) append(output.view[lane]);
        for (u32 lane = 0; lane < 2; ++lane) append(output.tc2[lane]);
        return result;
    }
    void Add(const Pica::OutputVertex& output, const Pica::NativeInputReadTrace* input,
             u64 draw, u32 index, u32 vertex) {
        auto& record = records[events % Capacity];
        record.output = Bits(output);
        record.event = ++events;
        record.draw = draw;
        record.index = index;
        record.vertex = vertex;
        record.actual_reads = input && input->count && input->count <= input->attributes.size();
        if (record.actual_reads) record.input = *input;
    }
    Match Find(const Pica::OutputVertex& output) const {
        const auto bits = Bits(output);
        Match result{};
        for (const auto& record : records) {
            if (!record.event || record.output != bits) continue;
            ++result.count;
            if (!result.latest || record.event > result.latest->event) result.latest = &record;
        }
        return result;
    }
    u64 Events() const { return events; }
private:
    std::array<Record, Capacity> records{};
    u64 events{};
};
} // namespace SwRenderer

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include "video_core/pica/regs_rasterizer.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/pica/uberhar_vertex_cache.h"

namespace Pica {

// AstraEH: Prepare the two register mappings once per no-GS draw. Compose the packed
// VS output mask with rasterizer semantics so a miss need not create/copy a 256-byte
// AttributeBuffer. Unmapped semantics remain one, duplicate destinations use the
// last write, and overflow semantics 24..31 remain outside OutputVertex.
class NativeVertexPlan {
public:
    NativeVertexPlan(const ShaderRegs& shader, const RasterizerRegs& rasterizer,
                     bool fast_conversion = false) {
        input_count = shader.max_input_attribute_index + 1;
        for (u32 attr = 0; attr < input_count; ++attr)
            input_registers[attr] = static_cast<u8>(shader.GetRegisterForAttribute(attr));

        const u32 attributes = rasterizer.vs_output_total & 7;
        u32 mask = shader.output_mask;
        supported = std::popcount(mask) >= static_cast<int>(attributes);
        if (!supported)
            return; // Incomplete output transport retains the established recovery path.

        std::array<u8, 24> sources;
        sources.fill(64);
        for (u32 attr = 0; attr < attributes; ++attr) {
            const u32 reg = std::countr_zero(mask);
            mask &= mask - 1;
            const auto map = rasterizer.vs_output_attributes[attr];
            const std::array<u32, 4> destinations{map.map_x, map.map_y, map.map_z, map.map_w};
            for (u32 comp = 0; comp < 4; ++comp) {
                if (destinations[comp] < sources.size())
                    sources[destinations[comp]] = static_cast<u8>(reg * 4 + comp);
            }
        }
        // CodexAstraLocal: Resolve aliases first, then recognize exact semantic
        // fields. The opt-in direct recipe writes all 96 bytes, including both
        // padding words, without a temporary fill/overwrite/copy buffer.
        if (fast_conversion) {
            fast_recipe = true;
            for (u32 field = 0; field < FieldOffsets.size(); ++field) {
                const u32 dest = FieldOffsets[field], length = FieldLengths[field];
                const u8 source = sources[dest];
                field_sources[field] = source;
                for (u32 lane = 0; lane < length; ++lane) {
                    const u32 expected = source == 64 ? 64 : source + lane;
                    if (expected > 64 || (source != 64 && expected == 64) ||
                        sources[dest + lane] != expected)
                        fast_recipe = false;
                }
            }
            if (fast_recipe)
                return;
        }
        // CodexAstraLocal: Group only four final contiguous components from one
        // output register. Resolve duplicate semantics above before grouping;
        // unmapped, partial and irregular destinations retain scalar transport.
        for (u32 dest = 0; dest < sources.size();) {
            const u8 source = sources[dest];
            if (dest + 3 < sources.size() && source < 64 && source % 4 == 0 &&
                sources[dest + 1] == source + 1 && sources[dest + 2] == source + 2 &&
                sources[dest + 3] == source + 3) {
                quads[quad_count++] = {static_cast<u8>(dest), source};
                dest += 4;
            } else {
                if (source != 64)
                    copies[copy_count++] = {static_cast<u8>(dest), source};
                ++dest;
            }
        }
    }

    bool Supported() const {
        return supported;
    }

    bool FastRecipe() const { return fast_recipe; }

    // AstraEH: Preserve ascending-attribute overwrite order and untouched registers.
    void LoadInput(ShaderUnit& unit, const AttributeBuffer& input) const {
        for (u32 attr = 0; attr < input_count; ++attr)
            unit.input[input_registers[attr]] = input[attr];
    }

    OutputVertex Convert(const ShaderUnit& unit) const {
        // CodexAstraLocal: Whole-register copying requires packed scalar order
        // and trivial object representation; a future layout change must fail build.
        using Register = Common::Vec4<f24>;
        static_assert(std::is_trivially_copyable_v<f24> &&
                      std::is_trivially_copyable_v<Register> &&
                      std::is_standard_layout_v<Register> &&
                      sizeof(Register) == 4 * sizeof(f24) &&
                      offsetof(Register, x) == 0 && offsetof(Register, y) == sizeof(f24) &&
                      offsetof(Register, z) == 2 * sizeof(f24) &&
                      offsetof(Register, w) == 3 * sizeof(f24));
        OutputVertex vertex;
        if (fast_recipe) {
            CopyFields(vertex, unit, std::make_index_sequence<FieldOffsets.size()>{});
        } else {
            std::array<f24, 24> slots;
            slots.fill(f24::One());
            // CodexAstraLocal: Copy complete register object bytes, preserving f24
            // storage bits and both output banks. Color conversion remains below.
            for (u32 i = 0; i < quad_count; ++i) {
                const auto [dest, source] = quads[i];
                std::memcpy(&slots[dest], &unit.output[unit.output_bank][source / 4],
                            4 * sizeof(f24));
            }
            for (u32 i = 0; i < copy_count; ++i) {
                const auto [dest, source] = copies[i];
                slots[dest] = unit.output[unit.output_bank][source / 4][source % 4];
            }
            std::memcpy(&vertex, slots.data(), sizeof(vertex));
        }
        // AstraEH: Match inherited pre-interpolation abs/saturation, including NaN -> 1.
        for (u32 i = 0; i < 4; ++i) {
            const float c = std::fabs(vertex.color[i].ToFloat32());
            vertex.color[i] = f24::FromFloat32(c < 1.0f ? c : 1.0f);
        }
        return vertex;
    }

private:
    // CodexAstraLocal: Fixed-size copies expose scalar/vector moves to the
    // compiler. Copying representation bytes preserves exceptional floats and
    // permits contiguous lanes across register boundaries without aliasing.
    static constexpr std::array<u8, 10> FieldOffsets{0, 4, 8, 12, 14, 16, 17, 18, 21, 22};
    static constexpr std::array<u8, 10> FieldLengths{4, 4, 4, 2, 2, 1, 1, 3, 1, 2};
    template <std::size_t Field>
    void CopyField(OutputVertex& vertex, const ShaderUnit& unit) const {
        constexpr std::array<f24, 4> ones{f24::One(), f24::One(), f24::One(), f24::One()};
        const u8 source = field_sources[Field];
        const auto* bytes = source == 64 ? reinterpret_cast<const u8*>(ones.data()) :
            reinterpret_cast<const u8*>(unit.output[unit.output_bank].data()) + source * sizeof(f24);
        std::memcpy(reinterpret_cast<u8*>(&vertex) + FieldOffsets[Field] * sizeof(f24),
                    bytes, FieldLengths[Field] * sizeof(f24));
    }
    template <std::size_t... Field>
    void CopyFields(OutputVertex& vertex, const ShaderUnit& unit,
                    std::index_sequence<Field...>) const {
        static_assert(std::is_standard_layout_v<OutputVertex> &&
            offsetof(OutputVertex, pos) == 0 && offsetof(OutputVertex, quat) == 4 * sizeof(f24) &&
            offsetof(OutputVertex, color) == 8 * sizeof(f24) &&
            offsetof(OutputVertex, tc0) == 12 * sizeof(f24) &&
            offsetof(OutputVertex, tc1) == 14 * sizeof(f24) &&
            offsetof(OutputVertex, tc0_w) == 16 * sizeof(f24) &&
            offsetof(OutputVertex, view) == 18 * sizeof(f24) &&
            offsetof(OutputVertex, tc2) == 22 * sizeof(f24) && sizeof(OutputVertex) == 24 * sizeof(f24));
        (CopyField<Field>(vertex, unit), ...);
    }
    std::array<u8, 10> field_sources{};
    bool fast_recipe{};
    struct Copy {
        u8 destination, source;
    };
    std::array<u8, 16> input_registers{};
    std::array<Copy, 24> copies{};
    // CodexAstraLocal: Store only final grouped mappings; scalar copies
    // retain every destination that could not safely form a full register.
    std::array<Copy, 6> quads{};
    u32 input_count{}, copy_count{}, quad_count{};
    bool supported{};
};

// AstraEH: Use the existing draw timestamp to admit one sample per 50 ms. Never
// catch up after a pause or exhaust a lifetime quota before later laps/scenes.
class NativeVertexSampleBudget {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Period = std::chrono::milliseconds{50};

    // CodexAstraLocal: Deferred admission leaves each due ordinary diagnostic
    // draw on its original synchronous path without consuming the sample here.
    bool Due(Clock::time_point now, u32 vertices) const {
        return vertices != 0 && now >= next;
    }

    bool Admit(Clock::time_point now, u32 vertices) {
        if (!Due(now, vertices))
            return false;
        next = now + Period;
        return true;
    }

private:
    Clock::time_point next{};
};

// AstraEH: Sparse CPU samples are raw, non-extrapolated durations, not GPU timings.
// One selected input partitions a sampled draw; full draw totals expose setup cost.
struct NativeVertexSamples {
    using Clock = std::chrono::steady_clock;
    u64 misses{}, hits{}, input_ns{}, shader_ns{}, output_ns{}, submit_ns{};
    u64 batches{}, batch_inputs{}, batch_invocations{}, setup_ns{}, vertex_ns{}, draw_ns{},
        draw_max_ns{};

    static u64 Nanoseconds(Clock::time_point begin, Clock::time_point end) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    }
};

struct NativeVertexCounts {
    u64 invocations{}, hits{};
};

// AstraEH: Cache the final 96-byte vertex, preserving the original 64-slot FIFO.
// A hit bypasses both VS transport and semantic conversion, but still submits every
// vertex in order to the persistent primitive assembler. No across-draw caching,
// heap allocation, geometry-shader substitution or partial-primitive reset occurs.
template <bool Profile, typename VertexAt, typename Shade, typename Submit>
NativeVertexCounts RunNativeVertexBatch(u32 count, bool indexed, VertexAt&& vertex_at,
                                        Shade&& shade, Submit&& submit,
                                        NativeVertexSamples& samples, u32 sample_index = 0) {
    VertexCacheIndex index;
    std::array<OutputVertex, VertexCacheIndex::Capacity> cache;
    NativeVertexCounts counts;
    for (u32 i = 0; i < count; ++i) {
        const u32 vertex = vertex_at(i);
        const int slot = indexed ? index.Find(static_cast<u16>(vertex)) : -1;
        OutputVertex result;
        const OutputVertex* output;
        if (slot >= 0) {
            output = &cache[slot];
            ++counts.hits;
            if constexpr (Profile) {
                if (i == sample_index)
                    ++samples.hits;
            }
        } else {
            if constexpr (Profile) {
                if (i == sample_index) {
                    result = shade.template operator()<true>(vertex, i);
                    ++samples.misses;
                } else {
                    result = shade.template operator()<false>(vertex, i);
                }
            } else {
                result = shade.template operator()<false>(vertex, i);
            }
            ++counts.invocations;
            if (indexed) {
                auto& cached = cache[index.Insert(static_cast<u16>(vertex))];
                cached = result;
                output = &cached;
            } else {
                output = &result;
            }
        }
        if constexpr (Profile) {
            if (i == sample_index) {
                const auto start = NativeVertexSamples::Clock::now();
                submit(*output);
                samples.submit_ns +=
                    NativeVertexSamples::Nanoseconds(start, NativeVertexSamples::Clock::now());
                continue;
            }
        }
        submit(*output);
    }
    return counts;
}
} // namespace Pica

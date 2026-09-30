// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Differential input transport against the inherited two-step algorithm.
// The reference intentionally retains per-attribute resolution, format dispatch,
// attribute-buffer materialization and the production ShaderUnit::LoadInput.
#include <bit>
#include <chrono>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/uberhar_vertex_input.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
}
using namespace Pica;
using Format = PipelineRegs::VertexAttributeFormat;
using Result = NativeVertexInputPlan::Result;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    static constexpr PAddr Base = 0x14000000;
    std::array<NativeInputAttribute, 16> attributes{};
    std::vector<u8> memory = std::vector<u8>(1 << 18);
    AttributeBuffer defaults{};
    ShaderRegs shader{};
    u64 mappings{};
    std::span<const u8> Map(PAddr address) {
        ++mappings;
        if (address < Base || address - Base > memory.size()) return {};
        return {memory.data() + (address - Base), memory.size() - (address - Base)};
    }
    Result Prepare(NativeVertexInputPlan& plan, u32 available, u64 last, PAddr base = Base) {
        return plan.Prepare(shader, available, base, last,
            [&](u32 i) { return attributes[i]; },
            [&](PAddr address) { return Map(address); });
    }
};
template <typename T>
void ReferenceAttribute(const u8* data, u32 elements, Common::Vec4<f24>& value) {
    for (u32 component = 0; component < elements; ++component) {
        T source;
        std::memcpy(&source, data + component * sizeof(T), sizeof(T));
        value[component] = f24::FromFloat32(source);
    }
}
// AstraEH: Independent scalar reference; memcpy gives the old typed-read value
// without making the test itself undefined on unaligned guest buffer addresses.
void Reference(Fixture& fixture, ShaderUnit& unit, u32 available, u32 vertex) {
    AttributeBuffer input{};
    for (u32 i = 0; i < available; ++i) {
        const auto& desc = fixture.attributes[i];
        if (desc.is_default) {
            input[i] = fixture.defaults[i];
            continue;
        }
        const auto source = fixture.Map(Fixture::Base + desc.offset + desc.stride * vertex);
        Check(source.size() >= desc.elements * PipelineRegs::GetFormatBytes(desc.format),
              "Reference input out of range");
        switch (desc.format) {
        case Format::BYTE: ReferenceAttribute<s8>(source.data(), desc.elements, input[i]); break;
        case Format::UBYTE: ReferenceAttribute<u8>(source.data(), desc.elements, input[i]); break;
        case Format::SHORT: ReferenceAttribute<s16>(source.data(), desc.elements, input[i]); break;
        case Format::FLOAT: ReferenceAttribute<f32>(source.data(), desc.elements, input[i]); break;
        }
        for (u32 c = desc.elements; c < 4; ++c)
            input[i][c] = c == 3 ? f24::One() : f24::Zero();
    }
    unit.LoadInput(fixture.shader, input);
}
void Same(const ShaderUnit& a, const ShaderUnit& b) {
    Check(std::memcmp(a.input.data(), b.input.data(), sizeof(a.input)) == 0,
          "Fused inputs differ bitwise, including untouched/aliased registers");
}
u64 Differential() {
    Fixture f;
    std::mt19937 rng{0xA57A17};
    u64 comparisons = 0;
    for (auto& byte : f.memory) byte = static_cast<u8>(rng());
    constexpr std::array<u32, 12> edges{0,0x80000000,0x7f800000,0xff800000,0x7fc01234,
        1,0x80000001,0x3f000000,0xbf000000,0x3fc00000,0xbfc00000,0x3f800000};
    for (u32 pattern = 0; pattern < 4096; ++pattern) {
        const u32 available = 1 + pattern % 16;
        const u32 consumed = 1 + rng() % available;
        f.shader.max_input_attribute_index.Assign(consumed - 1);
        f.shader.input_attribute_to_register_map_low = rng();
        f.shader.input_attribute_to_register_map_high = rng();
        for (u32 attr = 0; attr < 16; ++attr) {
            auto& a = f.attributes[attr];
            a = {attr * 8192 + pattern % 4, static_cast<u32>(rng() % 29),
                 1 + (pattern + attr) % 4, static_cast<Format>((pattern / 4 + attr) % 4),
                 (pattern + attr) % 7 == 0};
            for (u32 c = 0; c < 4; ++c)
                f.defaults[attr][c] = f24::FromFloat32(std::bit_cast<float>(edges[(attr+c+pattern)%edges.size()]));
            if (a.format == Format::FLOAT && pattern % 3 == 0) {
                for (u32 c = 0; c < 4; ++c) {
                    const u32 bits = edges[(attr+c+pattern)%edges.size()];
                    std::memcpy(f.memory.data()+a.offset+c*4, &bits, 4);
                }
            }
        }
        NativeVertexInputPlan plan;
        Check(f.Prepare(plan, available, 255) == Result::Ready, "Valid layout rejected");
        ShaderUnit actual, expected;
        actual.input = expected.input = f.defaults;
        for (u32 vertex : {0U, 1U, 7U, 63U, 64U, 127U, 254U, 255U}) {
            Reference(f, expected, available, vertex);
            const auto before = f.mappings;
            plan.Load(actual, f.defaults, vertex);
            Check(f.mappings == before, "Fused load performed memory mapping");
            Same(actual, expected);
            ++comparisons;
        }
        // Data and defaults stay live; no persistent contents caching is introduced.
        f.memory[0] ^= 0xff;
        f.defaults[0][0] = f24::FromFloat32(13.0f);
        Reference(f, expected, available, 0);
        plan.Load(actual, f.defaults, 0);
        Same(actual, expected);
        ++comparisons;
    }
    return comparisons;
}
void Boundaries() {
    Fixture f;
    f.shader.max_input_attribute_index.Assign(0);
    f.attributes[0] = {0, 1, 1, Format::UBYTE, false};
    NativeVertexInputPlan plan;
    Check(f.Prepare(plan,1,65535) == Result::Ready, "16-bit maximum rejected");
    ShaderUnit a, b;
    for (u32 v : {0U,255U,256U,65534U,65535U}) {
        Reference(f,b,1,v); plan.Load(a,f.defaults,v); Same(a,b);
    }
    // Non-indexed addresses above 16 bits must not be truncated.
    Check(f.Prepare(plan,1,70000) == Result::Ready, "Large non-indexed offset rejected");
    f.memory[70000]=73; f.memory[4464]=94;
    Reference(f,b,1,70000); plan.Load(a,f.defaults,70000); Same(a,b);
    f.attributes[0] = {static_cast<u32>(f.memory.size()-4),0,4,Format::UBYTE,false};
    Check(f.Prepare(plan,1,65535) == Result::Ready, "Zero stride/exact boundary rejected");
    f.attributes[0].offset++;
    Check(f.Prepare(plan,1,0) == Result::ShortMapping && !plan.Ready(), "Short mapping admitted");
    f.attributes[0] = {0xfffffff0,0,4,Format::FLOAT,false};
    Check(f.Prepare(plan,1,0) == Result::AddressWrap, "Base+offset wrap admitted");
    f.attributes[0] = {0,255,4,Format::FLOAT,false};
    Check(f.Prepare(plan,1,0xffffffff) == Result::AddressWrap, "Range wrap admitted");
    Check(f.Prepare(plan,1,u64{1}<<32) == Result::AddressWrap, "Vertex wrap admitted");
    f.attributes[0] = {0,0,0,Format::BYTE,false};
    Check(f.Prepare(plan,1,0) == Result::Unconfigured, "Retention case admitted");
    f.attributes[0].elements=5;
    Check(f.Prepare(plan,1,0) == Result::Unconfigured, "Oversized attribute admitted");
    f.attributes[0].elements=1; f.attributes[0].format=static_cast<Format>(4);
    Check(f.Prepare(plan,1,0) == Result::Unconfigured, "Unknown format admitted");
    Check(f.Prepare(plan,0,0) == Result::MissingAttribute, "Missing shader input admitted");
    // Partial failure cannot leave a reusable prior plan or touch input registers.
    f.attributes[0] = {0,1,1,Format::UBYTE,false};
    f.attributes[1] = {0xfffffff0,0,4,Format::FLOAT,false};
    f.shader.max_input_attribute_index.Assign(1);
    const auto before=a.input;
    Check(f.Prepare(plan,2,255) == Result::AddressWrap && !plan.Ready(), "Partial failure reused plan");
    Check(std::memcmp(before.data(),a.input.data(),sizeof(before))==0,"Preparation wrote shader inputs");
    // An all-default layout performs no mappings, including attributes 12..15.
    f.shader.max_input_attribute_index.Assign(15);
    for (auto& d : f.attributes) d.is_default=true;
    auto maps=f.mappings;
    Check(f.Prepare(plan,16,65535) == Result::Ready && f.mappings==maps,"Default layout mapped memory");
    Reference(f,b,16,0); plan.Load(a,f.defaults,65535); Same(a,b);
    // Next draw resolves a new backing allocation and register map.
    f.memory=std::vector<u8>(1<<18,41); f.attributes[0]={7,3,2,Format::UBYTE,false};
    f.shader.max_input_attribute_index.Assign(0); f.shader.input_attribute_to_register_map_low=7;
    Check(f.Prepare(plan,1,255)==Result::Ready,"Remapped next draw rejected");
    Reference(f,b,1,255); plan.Load(a,f.defaults,255); Same(a,b);
}
int main() {
    const auto comparisons=Differential();
    Boundaries();
    std::printf("PASS: %llu fused-input bitwise comparisons; defaults, live data, aliases, "
                "unaligned formats, 8/16-bit and large indices, overflow/range/retention fallbacks\n",
                static_cast<unsigned long long>(comparisons));
}

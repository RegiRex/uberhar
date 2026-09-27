// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Compare prepared output with the inherited ShaderUnit/OutputVertex code,
// including every output mask, exceptional float bits and real primitive assembly.
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_vertex_output.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

using namespace Pica;
using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;
void Check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}

// AstraEH: Exercise signed zero, infinity, NaN, subnormals and unsaturated colors.
f24 Value(u32 key) {
    constexpr std::array<u32, 12> edges{0,          0x80000000, 0x7f800000, 0xff800000,
                                        0x7fc01234, 1,          0x80000001, 0x3f000000,
                                        0xbf000000, 0x3fc00000, 0xbfc00000, 0x3f800000};
    return f24::FromFloat32(std::bit_cast<float>(edges[key % edges.size()]));
}

u64 CheckPlans() {
    std::mt19937 rng{0xA57AE};
    ShaderUnit unit, reference_input;
    AttributeBuffer input, packed;
    for (u32 reg = 0; reg < 16; ++reg) {
        for (u32 comp = 0; comp < 4; ++comp) {
            input[reg][comp] = Value(reg * 4 + comp);
            for (u32 bank = 0; bank < 2; ++bank)
                unit.output[bank][reg][comp] = Value(reg * 7 + comp + bank);
        }
    }
    u64 cases = 0;
    for (u32 mask = 0; mask < 65536; ++mask) {
        ShaderRegs shader{};
        shader.output_mask.Assign(mask);
        shader.max_input_attribute_index.Assign(mask & 15);
        shader.input_attribute_to_register_map_low = rng();
        shader.input_attribute_to_register_map_high = rng();
        RasterizerRegs rasterizer{};
        for (u32 attributes = 0; attributes < 8; ++attributes) {
            rasterizer.vs_output_total.Assign(attributes);
            for (auto& map : rasterizer.vs_output_attributes) {
                // Every semantic encoding, duplicate targets and overflow destinations.
                map.map_x.Assign(static_cast<Semantic>(rng() & 31));
                map.map_y.Assign(static_cast<Semantic>(rng() & 31));
                map.map_z.Assign(static_cast<Semantic>(rng() & 31));
                map.map_w.Assign(static_cast<Semantic>(rng() & 31));
            }
            const NativeVertexPlan plan{shader, rasterizer};
            Check(plan.Supported() == (std::popcount(mask) >= static_cast<int>(attributes)),
                  "Incomplete output transport was admitted");
            if (!plan.Supported())
                continue;
            unit.input = input;
            reference_input.input = input;
            reference_input.LoadInput(shader, input);
            plan.LoadInput(unit, input);
            Check(std::memcmp(unit.input.data(), reference_input.input.data(), sizeof(input)) == 0,
                  "Input mapping order/untouched registers differ");
            for (bool bank : {false, true}) {
                unit.output_bank = bank;
                unit.WriteOutput(shader, packed);
                const OutputVertex reference{rasterizer, packed};
                const auto actual = plan.Convert(unit);
                Check(std::memcmp(&reference, &actual, sizeof(actual)) == 0,
                      "Prepared output differs bitwise from inherited conversion");
                ++cases;
            }
        }
    }
    return cases;
}

struct BatchResult {
    std::vector<OutputVertex> vertices;
    std::vector<OutputVertex> triangles;
    NativeVertexCounts counts;
};

// AstraEH: Independent linear FIFO reference uses the old attribute payload and
// conversion on every hit; synthetic VS output changes on every actual invocation.
void FillShader(ShaderUnit& unit, u32 vertex, u64 invocation) {
    unit.output_bank = (invocation & 1) != 0;
    for (u32 reg = 0; reg < 16; ++reg) {
        for (u32 comp = 0; comp < 4; ++comp) {
            unit.output[unit.output_bank][reg][comp] = f24::FromFloat32(
                static_cast<float>((vertex + reg * 7 + comp + invocation) % 513) / 256.0f - 1.0f);
        }
    }
}

u64 CheckBatches() {
    using Topology = PipelineRegs::TriangleTopology;
    u64 cases = 0;
    for (auto topology : {Topology::List, Topology::Strip, Topology::Fan, Topology::Shader}) {
        for (bool indexed : {false, true}) {
            for (bool profile : {false, true}) {
                for (u32 pattern = 0; pattern < 3; ++pattern) {
                    PrimitiveAssembler reference_assembler{topology}, actual_assembler{topology};
                    // Preserve partial primitives across these batch boundaries; change maps
                    // and shader outputs between batches so stale cached state is detectable.
                    for (u32 count : {0U, 1U, 2U, 3U, 63U, 64U, 65U, 4096U}) {
                        ShaderRegs shader{};
                        shader.output_mask.Assign(count & 1 ? 0xaaaa : 0x5555);
                        RasterizerRegs rasterizer{};
                        rasterizer.vs_output_total.Assign(7);
                        for (u32 a = 0; a < 7; ++a) {
                            auto& map = rasterizer.vs_output_attributes[a];
                            map.map_x.Assign(static_cast<Semantic>((a * 4 + count) & 31));
                            map.map_y.Assign(static_cast<Semantic>((a * 4 + 1 + count) & 31));
                            map.map_z.Assign(static_cast<Semantic>((a * 4 + 2 + count) & 31));
                            map.map_w.Assign(static_cast<Semantic>((a * 4 + 3 + count) & 31));
                        }
                        NativeVertexPlan plan{shader, rasterizer};
                        const auto vertex_at = [&](u32 i) -> u32 {
                            if (!indexed)
                                return i + 65536; // Non-indexed offsets must not truncate to u16.
                            if (pattern == 0)
                                return i % 13;
                            if (pattern == 1)
                                return (i * 129) & 65535; // Bucket collisions and FIFO eviction.
                            return i % 5 == 0 ? 65535 : ((i / 3) % 67);
                        };
                        BatchResult reference, actual;
                        ShaderUnit unit;
                        const auto make_sink = [](BatchResult& result) {
                            return [&result](const OutputVertex& a, const OutputVertex& b,
                                             const OutputVertex& c) {
                                result.triangles.insert(result.triangles.end(), {a, b, c});
                            };
                        };
                        const PrimitiveAssembler::TriangleHandler reference_sink =
                            make_sink(reference);
                        const PrimitiveAssembler::TriangleHandler actual_sink = make_sink(actual);
                        if (topology == Topology::Shader) {
                            reference_assembler.SetWinding();
                            actual_assembler.SetWinding();
                        }
                        std::array<u16, 64> keys{};
                        std::array<AttributeBuffer, 64> cache{};
                        u32 size = 0, cursor = 0;
                        AttributeBuffer packed{};
                        for (u32 i = 0; i < count; ++i) {
                            const u32 vertex = vertex_at(i);
                            int hit = -1;
                            if (indexed) {
                                for (u32 slot = 0; slot < size; ++slot) {
                                    if (keys[slot] == vertex) {
                                        hit = static_cast<int>(slot);
                                        break;
                                    }
                                }
                            }
                            if (hit >= 0) {
                                packed = cache[hit];
                                ++reference.counts.hits;
                            } else {
                                FillShader(unit, vertex, ++reference.counts.invocations);
                                unit.WriteOutput(shader, packed);
                                if (indexed) {
                                    keys[cursor] = static_cast<u16>(vertex);
                                    cache[cursor] = packed;
                                    cursor = (cursor + 1) % 64;
                                    size = std::min(size + 1, 64U);
                                }
                            }
                            const OutputVertex output{rasterizer, packed};
                            reference.vertices.push_back(output);
                            reference_assembler.SubmitVertex(output, reference_sink);
                        }
                        u64 shader_runs = 0, sampled_shaders = 0;
                        NativeVertexSamples samples;
                        const auto shade = [&]<bool Sample>(u32 vertex, u32) {
                            if constexpr (Sample)
                                ++sampled_shaders;
                            FillShader(unit, vertex, ++shader_runs);
                            return plan.Convert(unit);
                        };
                        const auto submit = [&](const OutputVertex& output) {
                            actual.vertices.push_back(output);
                            actual_assembler.SubmitVertex(output, actual_sink);
                        };
                        const u32 sample_index = count / 2;
                        if (profile)
                            actual.counts = RunNativeVertexBatch<true>(
                                count, indexed, vertex_at, shade, submit, samples, sample_index);
                        else
                            actual.counts = RunNativeVertexBatch<false>(count, indexed, vertex_at,
                                                                        shade, submit, samples);
                        Check(actual.counts.invocations == reference.counts.invocations &&
                                  actual.counts.hits == reference.counts.hits &&
                                  shader_runs == reference.counts.invocations,
                              "Native route changed FIFO shader execution order/counts");
                        const auto equal = [](const auto& a, const auto& b) {
                            return a.size() == b.size() &&
                                   (a.empty() || std::memcmp(a.data(), b.data(),
                                                             a.size() * sizeof(OutputVertex)) == 0);
                        };
                        Check(equal(actual.vertices, reference.vertices),
                              "Submitted vertices differ");
                        Check(equal(actual.triangles, reference.triangles),
                              "Triangle order/winding/tail differs");
                        Check(sampled_shaders == samples.misses &&
                                  samples.misses + samples.hits == (profile && count ? 1U : 0U),
                              "Sparse sample admission/counts differ");
                        ++cases;
                    }
                }
            }
        }
    }
    return cases;
}

int main() {
    // AstraEH: Time-based sampling must cover late gameplay, reject empty draws and
    // avoid catch-up bursts after a pause, without reading a clock in this policy.
    NativeVertexSampleBudget budget;
    using Clock = NativeVertexSampleBudget::Clock;
    using namespace std::chrono_literals;
    const auto epoch = Clock::time_point{} + 1s;
    Check(!budget.Admit(epoch, 0), "Empty draw consumed sample budget");
    Check(budget.Admit(epoch, 1), "First nonempty draw was not sampled");
    Check(!budget.Admit(epoch, 1), "Repeated timestamp admitted another sample");
    Check(!budget.Admit(epoch + 49ms, 1), "Sample admitted before period");
    Check(budget.Admit(epoch + 50ms, 1), "Period boundary did not admit sample");
    Check(budget.Admit(epoch + 1h, 1), "Pause prevented resumed sampling");
    Check(!budget.Admit(epoch + 1h, 1), "Pause caused a catch-up burst");
    for (u32 sample = 1; sample <= 10000; ++sample) {
        const auto now = epoch + 1h + 50ms * sample;
        Check(budget.Admit(now, 100), "Lifetime quota starved late samples");
        Check(!budget.Admit(now + 1ms, 100), "Dense draws escaped time budget");
    }
    const auto plans = CheckPlans();
    const auto batches = CheckBatches();
    std::printf("PASS: %llu bitwise production vertex conversions; %llu FIFO/assembly batches; "
                "all 65536 masks, both banks, input mapping and sustained sample admission\n",
                static_cast<unsigned long long>(plans), static_cast<unsigned long long>(batches));
}

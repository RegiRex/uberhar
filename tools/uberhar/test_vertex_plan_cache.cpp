// Copyright 2026 Uberhar contributors; GPLv2 or later; see license.txt.
// AstraPro: Test cached plans against freshly constructed plans and inherited
// output transport. No vertex payload or constants may become part of this cache.
#include <cstdio>
#include <random>
#include <stdexcept>
#include "common/logging/log.h"
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
#include "video_core/pica/uberhar_vertex_plan_cache.h"

Pica::f24 Value(u32 key) {
    constexpr std::array<u32, 12> bits{0, 0x80000000, 0x7f800000, 0xff800000,
        0x7fc01234, 1, 0x80000001, 0x3f000000, 0xbf000000, 0x3fc00000, 0xbfc00000, 0x3f800000};
    return Pica::f24::FromFloat32(std::bit_cast<float>(bits[key % bits.size()]));
}

int main() {
    std::mt19937 rng{0x013CA5E};
    Pica::NativeVertexPlanCache cache;
    Pica::ShaderRegs shader{};
    Pica::RasterizerRegs raster{};
    Pica::ShaderUnit actual, expected;
    Pica::AttributeBuffer input, packed;
    unsigned checks = 0;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        // Alternate changed plans and exact repeats, including invalid output
        // masks, aliased input registers, inactive maps and restored old shapes.
        if (iteration % 3 != 1) {
            shader.output_mask.Assign(rng() & 65535);
            shader.max_input_attribute_index.Assign(rng() & 15);
            shader.input_attribute_to_register_map_low = rng();
            shader.input_attribute_to_register_map_high = rng();
            raster.vs_output_total.Assign(rng() & 7);
            for (auto& m : raster.vs_output_attributes)
                m.raw = rng();
        }
        const Pica::NativeVertexPlan fresh{shader, raster};
        const auto& reused = cache.Get(shader, raster);
        Check(fresh.Supported() == reused.Supported(), "cached support mismatch");
        ++checks;
        for (unsigned reg = 0; reg < 16; ++reg) {
            for (unsigned c = 0; c < 4; ++c) {
                input[reg][c] = Value(rng());
                actual.input[reg][c] = expected.input[reg][c] = Value(rng());
                for (unsigned bank = 0; bank < 2; ++bank)
                    actual.output[bank][reg][c] = Value(rng());
            }
        }
        actual.output_bank = (iteration & 1) != 0;
        reused.LoadInput(actual, input);
        fresh.LoadInput(expected, input);
        Check(std::memcmp(actual.input.data(), expected.input.data(), sizeof(input)) == 0,
              "cached input changed alias order or untouched registers");
        ++checks;
        if (fresh.Supported()) {
            const auto a = reused.Convert(actual);
            const auto b = fresh.Convert(actual);
            actual.WriteOutput(shader, packed);
            const Pica::OutputVertex reference{raster, packed};
            Check(std::memcmp(&a, &b, sizeof(a)) == 0, "cached conversion differs");
            Check(std::memcmp(&a, &reference, sizeof(a)) == 0, "inherited conversion differs");
            checks += 2;
        }
        const auto builds = cache.Builds();
        shader.main_offset.Assign((shader.main_offset.Value() + 1) & 65535);
        cache.Get(shader, raster);
        Check(cache.Builds() == builds, "unrelated program entry changed a pure mapping");
        ++checks;
        // Every active output word must invalidate, even if it changes an
        // unrecognized semantic. Equality must not depend on a lossy hash.
        for (unsigned i = 0; i < raster.vs_output_total; ++i) {
            const auto before = cache.Builds();
            raster.vs_output_attributes[i].raw ^= 1;
            cache.Get(shader, raster);
            Check(cache.Builds() == before + 1, "active map edit not observed");
            raster.vs_output_attributes[i].raw ^= 1;
            cache.Get(shader, raster);
            ++checks;
        }
        if (iteration % 17 == 0) {
            const auto before = cache.Builds();
            cache.Reset(); cache.Get(shader, raster);
            Check(cache.Builds() == before + 1, "explicit reset did not rebuild");
            ++checks;
        }
    }
    std::printf("Native plan cache: %u checks; hits=%llu builds=%llu; exact live payloads preserved\n",
                checks, (unsigned long long)cache.Hits(), (unsigned long long)cache.Builds());
}

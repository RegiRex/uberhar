// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Differential check against the pre-0.1.5 path, including complete
// input mutations, forced slot collisions, title/profile resets and saturated census.
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/uberhar_tev_preparation.h"

namespace Common::Log {
void Stop() {
}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) {
        throw std::runtime_error(fmt::vformat(format, args));
    }
}
} // namespace Common::Log

namespace {
using namespace Vulkan;
using namespace Pica::Shader;
namespace GLSL = Pica::Shader::Generator::GLSL;
using Key = Common::HashableStruct<StaticPipelineInfo>;

void Require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

// AstraEH: Independent copy of the released 0.1.4 calculations, using the old
// PipelineInfo construction/order rather than the new preparation helper.
TevPreparation OldPath(const FSConfig& raw, const PipelineInfo& info, const Profile& profile,
                       bool dynamic) {
    const auto family = GLSL::MakeDynamicTevFamilyConfig(raw, profile);
    const auto raw_hash = raw.Hash();
    const auto family_hash = family.Hash();
    PipelineInfo fallback_info = info;
    fallback_info.state.shader_ids[1] = raw_hash;
    const auto raw_pipeline =
        fallback_info.state.ExecutionHash(dynamic, fallback_info.state.shader_ids);
    fallback_info.state.shader_ids[1] = family_hash;
    const auto candidate_pipeline =
        fallback_info.state.ExecutionHash(dynamic, fallback_info.state.shader_ids);
    auto previous = family;
    previous.lighting = raw.lighting;
    const auto alpha13 =
        GLSL::MakeDynamicTevFamilyConfig(raw, profile, GLSL::LightingFamilyKey::Alpha13);
    const auto alpha14 =
        GLSL::MakeDynamicTevFamilyConfig(raw, profile, GLSL::LightingFamilyKey::Alpha14);
    return {family,
            {raw_hash, family_hash, raw_pipeline, candidate_pipeline, info.state.shader_ids[0],
             info.state.shader_ids[2], Common::ComputeStructHash64(info.state.vertex_layout),
             Common::ComputeStructHash64(info.state.attachments),
             Common::ComputeStructHash64(info.state.blending),
             Common::ComputeStructHash64(info.state.rasterization),
             Common::ComputeStructHash64(info.state.depth_stencil), previous.Hash(),
             Common::ComputeStructHash64(family.lighting),
             Common::ComputeStructHash64(family.proctex), alpha13.Hash(), alpha14.Hash()},
            raw.lighting.enable ? 1U << raw.lighting.src_num.Value() : 0U};
}

struct Census {
    std::array<std::unordered_set<u64>, 16> keys;
    u32 light_counts{};
    bool capped{};
    void Observe(const TevPreparation& prepared) {
        light_counts |= prepared.light_counts;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].size() < 2048) {
                keys[i].insert(prepared.candidates[i]);
            } else if (!keys[i].contains(prepared.candidates[i])) {
                capped = true;
            }
        }
    }
    bool operator==(const Census&) const = default;
};

struct Input {
    FSConfig raw;
    PipelineInfo pipeline;
};

std::vector<Input> Corpus(std::size_t count) {
    std::vector<Input> inputs;
    for (std::size_t i = 0; i < count; ++i) {
        Pica::RegsInternal regs{};
        regs.lighting.disable.Assign(i % 3 == 0);
        regs.lighting.max_light_index.Assign(i % 8);
        regs.framebuffer.output_merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
        FSConfig config{regs};
        config.texture.texture0_type.Assign(
            static_cast<Pica::TexturingRegs::TextureConfig::TextureType>(i % 6));
        config.lighting.lights[0].raw ^= static_cast<u32>(i);
        PipelineInfo info{};
        info.state.shader_ids = {i % 11, i * 17, i % 5};
        info.state.blending.blend_enable = i % 2;
        info.state.blending.color_write_mask = i % 16;
        info.state.rasterization.value = i % 8;
        info.state.depth_stencil.value = i * 13;
        info.state.vertex_layout.binding_count = 1;
        info.state.vertex_layout.attribute_count = 1;
        info.state.vertex_layout.bindings[0].value = i * 31;
        info.state.vertex_layout.attributes[0].value = i * 29;
        inputs.push_back({config, info});
    }
    return inputs;
}

u64 checks{};
template <std::size_t N>
void Check(TevPreparationCache<N>& cache, const Input& input, const Profile& profile, bool dynamic,
           Census& expected, Census& actual) {
    const auto old = OldPath(input.raw, input.pipeline, profile, dynamic);
    const auto current = cache.Get(input.raw, input.pipeline);
    Require(old.family == current.prepared.family &&
                old.candidates == current.prepared.candidates &&
                old.light_counts == current.prepared.light_counts,
            "Cached preparation differs from released path");
    expected.Observe(old);
    if (!current.reused) {
        actual.Observe(current.prepared);
    }
    ++checks;
}

void Verify() {
    auto inputs = Corpus(3000);
    Profile profile{};
    profile.is_vulkan = true;
    TevPreparationCache<> cache;
    static_assert(sizeof(cache) < 512 * 1024);
    Census expected, actual;
    for (bool dynamic : {false, true}) {
        for (u32 flags = 0; flags < 16; ++flags) {
            profile.has_custom_border_color = flags & 1;
            profile.has_logic_op = (flags >> 1) & 1;
            profile.has_blend_minmax_factor = (flags >> 2) & 1;
            profile.enable_accurate_mul = (flags >> 3) & 1;
            cache.Configure(profile, dynamic);
            for (std::size_t i = 0; i < 256; ++i) {
                const auto& input = inputs[(i * 13) % 128];
                Check(cache, input, profile, dynamic, expected, actual);
                Check(cache, input, profile, dynamic, expected, actual);
            }
        }
    }
    Require(cache.Stats().invalidations == 31, "Profile/feature change did not invalidate");
    const auto saved = cache.Stats();
    cache.Configure(profile, true);
    Require(cache.Stats().invalidations == saved.invalidations, "Unchanged profile invalidated");
    // AstraEH: Even profile fields outside today's canonicalizer invalidate safely.
    ++profile.vk_format_traits[15].native_format;
    cache.Configure(profile, true);
    Require(cache.Stats().invalidations == 32, "Format profile update retained preparation");
    // AstraEH: Flip every input byte, including fields omitted by execution hashing;
    // canonical aliases must still preserve independent diagnostic dimensions.
    const auto base = inputs[7];
    for (std::size_t i = 0; i < sizeof(FSConfig) + sizeof(StaticPipelineInfo); ++i) {
        auto changed = base;
        auto* bytes = i < sizeof(FSConfig)
                          ? reinterpret_cast<unsigned char*>(&changed.raw)
                          : reinterpret_cast<unsigned char*>(&changed.pipeline.state);
        bytes[i < sizeof(FSConfig) ? i : i - sizeof(FSConfig)] ^= 1;
        Check(cache, base, profile, true, expected, actual);
        const auto misses = cache.Stats().misses;
        Check(cache, changed, profile, true, expected, actual);
        Require(cache.Stats().misses > misses, "Different complete input reused");
    }
    // AstraEH: A one-slot cache forces A/B/A collisions regardless of hash values.
    TevPreparationCache<1> collision;
    collision.Configure(profile, true);
    for (u32 i = 0; i < 100; ++i) {
        Check(collision, inputs[i % 2], profile, true, expected, actual);
    }
    Require(collision.Stats().hits == 0 && collision.Stats().evictions == 99,
            "Slot collision reused an incompatible entry");
    for (const auto& input : inputs) {
        Check(cache, input, profile, true, expected, actual);
        Check(cache, input, profile, true, expected, actual);
    }
    Require(expected == actual && actual.capped, "Census sets/cap/light coverage changed");
    cache.Reset();
    expected = {};
    actual = {};
    Require(cache.Stats().requests == 0, "Title reset retained counters");
    Check(cache, inputs.back(), profile, true, expected, actual);
    Require(cache.Stats().misses == 1 && expected == actual, "Title reset hid first observation");
    // AstraEH: Dynamic viewport state is consumed later, never part of pure preparation.
    auto dynamic_only = inputs.back();
    dynamic_only.pipeline.dynamic_info.blend_color ^= 1;
    Check(cache, dynamic_only, profile, true, expected, actual);
    Require(cache.Stats().hits == 1, "Dynamic draw state fragmented pure preparation");
    std::cout << checks
              << " differential preparations; exact census/caps, collisions, profile/title resets; "
              << sizeof(cache) << " bytes fixed storage\n";
}

// AstraEH: Synthetic host cost only, no game-speed promise or timing pass/fail gate.
// Run both paths on the identical stream and verify checksums/census after timing.
void Benchmark() {
    for (const std::size_t scenario : {40U, 192U, 1024U, 4096U}) {
        const std::size_t count = scenario == 4096 ? 1024 : scenario;
        const std::size_t repeats = scenario == 4096 ? 1 : 4;
        const auto inputs = Corpus(count);
        Profile profile{};
        profile.is_vulkan = true;
        TevPreparationCache<> cache;
        cache.Configure(profile, false);
        Census old_census, cached_census;
        constexpr u64 Iterations = 1'000'000;
        u64 old_sum{}, cached_sum{};
        const auto start = std::chrono::steady_clock::now();
        for (u64 i = 0; i < Iterations; ++i) {
            const auto& input = inputs[(i / repeats) % count];
            const auto prepared = OldPath(input.raw, input.pipeline, profile, false);
            old_census.Observe(prepared);
            old_sum += prepared.candidates[1];
        }
        const auto middle = std::chrono::steady_clock::now();
        for (u64 i = 0; i < Iterations; ++i) {
            const auto& input = inputs[(i / repeats) % count];
            const auto prepared = cache.Get(input.raw, input.pipeline);
            if (!prepared.reused) {
                cached_census.Observe(prepared.prepared);
            }
            cached_sum += prepared.prepared.candidates[1];
        }
        const auto end = std::chrono::steady_clock::now();
        Require(old_sum == cached_sum && old_census == cached_census, "Benchmark result changed");
        const double old_ns =
            std::chrono::duration<double, std::nano>(middle - start).count() / Iterations;
        const double cached_ns =
            std::chrono::duration<double, std::nano>(end - middle).count() / Iterations;
        std::cout << "benchmark states=" << count << " repeats=" << repeats
                  << " requests=" << Iterations << " old_ns=" << old_ns
                  << " cached_ns=" << cached_ns << " ratio=" << old_ns / cached_ns
                  << " hits=" << cache.Stats().hits << " misses=" << cache.Stats().misses << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    Verify();
    if (argc == 2 && std::string_view{argv[1]} == "--benchmark") {
        Benchmark();
    }
}

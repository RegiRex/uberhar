// CodexAstraUlt-2: Copyright 2026 Uberhar contributors. GPLv2 or later; see license.txt.
#include <cstdio>
#include <functional>
#include <random>
#include <stdexcept>
#include "video_core/renderer_vulkan/uberhar_precompile_recipes.h"

namespace Vulkan {
// CodexAstraUlt-2: Run actual CPU-layout construction; this shell supplies only its 88-byte
// software vertex carrier. The codec itself is included unchanged from production.
struct RasterizerVulkan {
    using HardwareVertex = std::array<float, 22>;
    VertexLayout software_layout{};
    void MakeSoftwareVertexLayout();
};
#include "precompile_software_layout.inc"
}

namespace {
using namespace Vulkan::Precompile;
void Check(bool passed, const char* text) {
    if (!passed) throw std::runtime_error(text);
}
Recipe Base() {
    Vulkan::RasterizerVulkan rasterizer;
    rasterizer.MakeSoftwareVertexLayout();
    Vulkan::StaticPipelineInfo pipeline{};
    pipeline.vertex_layout = rasterizer.software_layout;
    pipeline.attachments.color = VideoCore::PixelFormat::RGBA8;
    pipeline.attachments.depth = VideoCore::PixelFormat::Invalid;
    pipeline.blending.color_write_mask = 15;
    pipeline.blending.logic_op = Pica::FramebufferRegs::LogicOp::Copy;
    Pica::RegsInternal registers{};
    registers.lighting.disable.Assign(1);
    return {Pica::Shader::FSConfig{registers}, pipeline, 1};
}
void Rechecksum(std::vector<u8>& bytes) {
    const u64 checksum = Detail::Checksum(bytes);
    for (std::size_t i = 0; i < 8; ++i) bytes[56 + i] = static_cast<u8>(checksum >> (8 * i));
}
void WriteRecord(std::vector<u8>& bytes, const Recipe& recipe, std::size_t index = 0) {
    std::vector<u8> payload;
    Detail::Writer write{payload};
    Detail::ConfigFields(recipe.config, write);
    Detail::PipelineFields(recipe.pipeline, write);
    write(recipe.uses);
    Check(payload.size() == RecipeBytes, "explicit record size mismatch");
    std::copy(payload.begin(), payload.end(), bytes.begin() + HeaderBytes + index * RecipeBytes);
    Rechecksum(bytes);
}
}

int main() try {
    const Identity identity{0x4000000123400ULL, 123, 456};
    const auto base = Base();
    Check(IsEligible(base.config, base.pipeline), "actual CPU layout rejected");
    const auto bytes = Encode({base}, identity);
    Check(bytes.size() == HeaderBytes + RecipeBytes, "explicit wire size");
    auto decoded = Decode(bytes, identity);
    Check(decoded && decoded->size() == 1 && Equals(decoded->front(), base), "round trip");
    Check(decoded->front().pipeline.shader_ids == std::array<u64, 3>{}, "shader IDs persisted");
    const auto empty = Decode(Encode({}, identity), identity);
    Check(empty && empty->empty(), "empty valid cache");
    Check(!Decode(bytes, {identity.title + 1, identity.compiler, identity.profile}), "wrong title");
    Check(!Decode(bytes, {identity.title, identity.compiler + 1, identity.profile}), "wrong compiler");
    Check(!Decode(bytes, {identity.title, identity.compiler, identity.profile + 1}), "wrong profile");
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        Check(!Decode(std::span{bytes}.first(n), identity), "truncation accepted");
        auto corrupt = bytes;
        corrupt[n] ^= 1;
        Check(!Decode(corrupt, identity), "single-byte corruption accepted");
    }
    auto trailing = bytes;
    trailing.push_back(0);
    Check(!Decode(trailing, identity), "trailing bytes accepted");

    // CodexAstraUlt-2: Populate real guest registers and execute production config/profile
    // constructors, covering eight-light layouts, blend sentinels, wrap modes and procedural LUTs.
    for (u32 i = 0; i < 128; ++i) {
        Pica::RegsInternal registers{};
        registers.lighting.disable.Assign(i & 1);
        registers.lighting.max_light_index.Assign(i % 8);
        registers.lighting.config0.config.Assign(static_cast<Pica::LightingRegs::LightingConfig>(
            i % 8 == 7 ? 8 : i % 8));
        registers.lighting.lut_scale.d0.Assign(static_cast<Pica::LightingRegs::LightingScale>(i % 8));
        registers.lighting.lut_input.d0.Assign(static_cast<Pica::LightingRegs::LightingLutInput>(i % 6));
        registers.framebuffer.output_merger.alphablend_enable.Assign((i >> 1) & 1);
        registers.framebuffer.output_merger.alpha_blending.blend_equation_rgb.Assign(
            static_cast<Pica::FramebufferRegs::BlendEquation>(i % 5));
        registers.framebuffer.output_merger.alpha_blending.factor_source_rgb.Assign(
            static_cast<Pica::FramebufferRegs::BlendFactor>(i % 15));
        registers.texturing.texture0.wrap_s.Assign(
            static_cast<Pica::TexturingRegs::TextureConfig::WrapMode>(i % 8));
        registers.texturing.main_config.texture3_enable.Assign((i >> 2) & 1);
        registers.texturing.main_config.texture3_coordinates.Assign(i % 3);
        registers.texturing.proctex_lut.width.Assign(64);
        registers.texturing.proctex_lut.lod_max.Assign(7);
        registers.texturing.proctex_lut.filter.Assign(static_cast<Pica::TexturingRegs::ProcTexFilter>(i % 6));
        Pica::Shader::Profile profile{};
        profile.is_vulkan = 1;
        profile.has_custom_border_color = (i >> 3) & 1;
        auto actual = base;
        actual.config = Pica::Shader::FSConfig{registers};
        actual.config.ApplyProfile(profile);
        Check(IsEligible(actual.config, actual.pipeline), "constructor config rejected");
        const auto roundtrip = Decode(Encode({actual}, identity), identity);
        Check(roundtrip && Equals(roundtrip->front(), actual), "constructor config round trip");
    }

    // CodexAstraUlt-2: Recomputed checksums deliberately exercise semantic validation, not
    // just corruption detection; each rejected state would reach generation or driver setup.
    std::vector<std::function<void(Recipe&)>> invalid{
        [](auto& r) { r.config.lighting.src_num.Assign(15); },
        [](auto& r) { r.config.lighting.config.Assign(static_cast<Pica::LightingRegs::LightingConfig>(7)); },
        [](auto& r) { r.config.lighting.bump_mode.Assign(static_cast<Pica::LightingRegs::LightingBumpMode>(3)); },
        [](auto& r) { r.config.lighting.lut_d0.type.Assign(static_cast<Pica::LightingRegs::LightingLutInput>(6)); },
        [](auto& r) { r.config.lighting.lut_d0.scale_bits = 0x7FC00000; },
        [](auto& r) { r.config.framebuffer.requested_logic_op = static_cast<Pica::FramebufferRegs::LogicOp>(16); },
        [](auto& r) { r.config.framebuffer.requested_rgb_blend.eq = static_cast<Pica::FramebufferRegs::BlendEquation>(5); },
        [](auto& r) { r.config.framebuffer.requested_alpha_blend.src_factor = static_cast<Pica::FramebufferRegs::BlendFactor>(15); },
        [](auto& r) { r.config.texture.texture0_type.Assign(static_cast<Pica::TexturingRegs::TextureConfig::TextureType>(7)); },
        [](auto& r) { r.config.texture.fog_mode.Assign(Pica::TexturingRegs::FogMode::Gas); },
        [](auto& r) { r.config.texture.tev_stages[5].sources_raw = 7; },
        [](auto& r) { r.config.texture.tev_stages[3].modifiers_raw = 6; },
        [](auto& r) { r.config.texture.tev_stages[1].ops_raw = 10; },
        [](auto& r) { r.config.texture.tev_stages[1].ops_raw = 6U << 16; },
        [](auto& r) { r.config.proctex.coord.Assign(3); },
        [](auto& r) { r.config.proctex.lut_width = -1; },
        [](auto& r) { r.config.proctex.lut_offset3 = 256; },
        [](auto& r) { r.config.proctex.enable.Assign(1); },
        [](auto& r) { r.config.proctex.enable.Assign(1); r.config.proctex.lut_width = 64; r.config.proctex.lut_offset0 = 240; },
        [](auto& r) { r.config.proctex.lut_filter.Assign(static_cast<Pica::TexturingRegs::ProcTexFilter>(6)); },
        [](auto& r) { r.pipeline.attachments.color = static_cast<VideoCore::PixelFormat>(15); },
        [](auto& r) { r.pipeline.attachments.depth = static_cast<VideoCore::PixelFormat>(15); },
        [](auto& r) { r.pipeline.vertex_layout.binding_count = 255; },
        [](auto& r) { r.pipeline.vertex_layout.attribute_count = 255; },
        [](auto& r) { r.pipeline.vertex_layout.bindings[0].byte_count.Assign(84); },
        [](auto& r) { r.pipeline.vertex_layout.attributes[7].offset.Assign(2047); },
        [](auto& r) { r.pipeline.blending.color_blend_eq.Assign(static_cast<Pica::FramebufferRegs::BlendEquation>(7)); },
        [](auto& r) { r.pipeline.blending.src_alpha_blend_factor.Assign(static_cast<Pica::FramebufferRegs::BlendFactor>(15)); },
        [](auto& r) { r.pipeline.blending.logic_op = static_cast<Pica::FramebufferRegs::LogicOp>(16); },
        [](auto& r) { r.pipeline.blending.color_write_mask = 16; },
        [](auto& r) { r.pipeline.rasterization.topology.Assign(Pica::PipelineRegs::TriangleTopology::Strip); },
        [](auto& r) { r.pipeline.depth_stencil.value = 1U << 31; },
        [](auto& r) { r.uses = 0; },
    };
    for (const auto& mutate : invalid) {
        auto bad = base;
        mutate(bad);
        Check(Encode({bad}, identity).empty(), "invalid recipe encoded");
        auto forged = bytes;
        WriteRecord(forged, bad);
        Check(!Decode(forged, identity), "checksummed invalid state decoded");
    }
    for (u32 shader : {0U, 2U}) {
        auto gpu = base;
        gpu.pipeline.shader_ids[shader] = 1;
        Check(!IsEligible(gpu.config, gpu.pipeline) && Encode({gpu}, identity).empty(), "GPU route accepted");
    }
    auto host_independent = base;
    host_independent.pipeline.shader_ids[1] = 0xFEEDFACE;
    host_independent.pipeline.vertex_layout.bindings.back().value = 0xFFFF;
    host_independent.pipeline.vertex_layout.attributes.back().value = 0xFFFFFFFF;
    Check(Encode({host_independent}, identity) == bytes, "host identity or unused layout serialized");

    // CodexAstraUlt-2: Exercise production encoding's exact deduplication, static-state
    // distinctions, priority and saturated usage accumulation through persisted round trips.
    decoded = Decode(Encode({base, host_independent}, identity), identity);
    Check(decoded && decoded->size() == 1 && decoded->front().uses == 2,
          "encoding deduplication");
    auto distinct = base;
    distinct.pipeline.depth_stencil.value = 2;
    decoded = Decode(Encode({base, distinct}, identity), identity);
    Check(decoded && decoded->size() == 2 && !Equals((*decoded)[0], (*decoded)[1]),
          "exact inactive static state merged");
    auto nearly_saturated = base;
    nearly_saturated.uses = std::numeric_limits<u64>::max() - 1;
    auto increment = host_independent;
    increment.uses = 4;
    decoded = Decode(Encode({nearly_saturated, increment}, identity), identity);
    Check(decoded && decoded->size() == 1 &&
          decoded->front().uses == std::numeric_limits<u64>::max(), "usage overflow");
    auto saturated = base;
    saturated.uses = std::numeric_limits<u64>::max();
    decoded = Decode(Encode({saturated, base}, identity), identity);
    Check(decoded && decoded->size() == 1 && decoded->front().uses == saturated.uses,
          "saturated usage changed");
    distinct.uses = 10;
    decoded = Decode(Encode({base, distinct, host_independent}, identity), identity);
    Check(decoded && decoded->size() == 2 && decoded->front().uses == 10 &&
          decoded->back().uses == 2, "deduplication priority");
    auto duplicate = Encode({base, distinct}, identity);
    WriteRecord(duplicate, base, 0);
    WriteRecord(duplicate, base, 1);
    Check(!Decode(duplicate, identity), "duplicate wire records accepted");

    // CodexAstraUlt-2: Exhaust hard bounds and exercise random bytes with valid envelopes.
    std::vector<Recipe> maximum;
    maximum.reserve(MaxRecipes + 1);
    for (u32 i = 0; i < MaxRecipes; ++i) {
        auto recipe = base;
        recipe.pipeline.depth_stencil.value = i;
        maximum.push_back(recipe);
    }
    const auto full = Encode(maximum, identity);
    const auto full_decoded = Decode(full, identity);
    Check(full.size() == MaxEncodedBytes && full_decoded && full_decoded->size() == MaxRecipes, "maximum cache");
    auto extra = base;
    extra.pipeline.depth_stencil.value = MaxRecipes;
    maximum.push_back(extra);
    Check(Encode(maximum, identity).empty(), "entry bound exceeded");
    auto excessive = bytes;
    excessive[12] = 1; excessive[13] = 32;
    Rechecksum(excessive);
    Check(!Decode(excessive, identity), "forged count accepted");
    std::mt19937 random{0x818};
    for (unsigned i = 0; i < 2000; ++i) {
        auto mutated = bytes;
        for (unsigned j = 0; j < 1 + random() % 12; ++j)
            mutated[HeaderBytes + random() % RecipeBytes] ^= static_cast<u8>(1 + random() % 255);
        Rechecksum(mutated);
        if (const auto result = Decode(mutated, identity))
            Check(result->size() == 1 && IsEligible(result->front().config, result->front().pipeline), "invalid fuzz acceptance");
    }
    std::printf("PASS: semantic recipe round trip, identities, corruption, %zu forged invalid states, dedup, priority, 8192 bound and 2000 fuzz cases\n", invalid.size());
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}

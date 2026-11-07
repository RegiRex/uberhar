// CodexAstraUlt-2: Copyright 2026 Uberhar contributors. GPLv2 or later; see license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/shader/generator/pica_fs_config.h"

namespace Vulkan::Precompile {
// CodexAstraUlt-2: Transfer semantic CPU fragment state only; runtime shader/module identities
// are reconstructed by the owning cache. The checksum detects damage, not malicious authorship.
inline constexpr std::size_t MaxRecipes = 8192;
inline constexpr u32 Schema = 1;
inline constexpr u64 Magic = 0x4550494345524855ULL;
inline constexpr std::size_t HeaderBytes = 64;
inline constexpr std::size_t ConfigBytes = 272;
inline constexpr std::size_t PipelineBytes = 117;
inline constexpr std::size_t RecipeBytes = ConfigBytes + PipelineBytes + sizeof(u64);
inline constexpr std::size_t MaxEncodedBytes = HeaderBytes + MaxRecipes * RecipeBytes;
struct Identity {
    u64 title{}, compiler{}, profile{};
};
struct Recipe {
    Pica::Shader::FSConfig config;
    StaticPipelineInfo pipeline;
    u64 uses{};
};

// CodexAstraUlt-2: Validate every generator-controlled index and enum before constructing
// source or Vulkan state, even when a damaged file carries a recomputed valid checksum.
inline bool IsValidConfig(const Pica::Shader::FSConfig& config) {
    const auto& fb = config.framebuffer;
    const auto valid_blend = [](const Pica::Shader::BlendConfig& blend) {
        const u32 eq = static_cast<u32>(blend.eq);
        return (eq <= 4 || eq == std::numeric_limits<u32>::max()) &&
               static_cast<u32>(blend.src_factor) <= 14 &&
               static_cast<u32>(blend.dst_factor) <= 14;
    };
    if ((fb.raw & ~0xFFFU) || fb.scissor_test_mode.Value() ==
            static_cast<Pica::RasterizerRegs::ScissorMode>(2) ||
        static_cast<u32>(fb.requested_logic_op) > 15 ||
        !valid_blend(fb.requested_rgb_blend) || !valid_blend(fb.requested_alpha_blend))
        return false;
    const auto& texture = config.texture;
    const u32 fog = static_cast<u32>(texture.fog_mode.Value());
    if ((texture.raw & ~0x1FFFFU) || static_cast<u32>(texture.texture0_type.Value()) > 5 ||
        (fog != 0 && fog != 5))
        return false;
    for (const auto& border : texture.texture_border_color)
        if (border.raw & ~3U) return false;
    for (const auto& wrap : texture.requested_wrap)
        if (static_cast<u32>(wrap.s) > 7 || static_cast<u32>(wrap.t) > 7) return false;
    for (const auto& stage : texture.tev_stages) {
        for (u32 shift : {0U, 4U, 8U, 16U, 20U, 24U}) {
            const u32 source = (stage.sources_raw >> shift) & 15U;
            if (source > 6 && source < 13) return false;
        }
        for (u32 shift : {0U, 4U, 8U}) {
            const u32 modifier = (stage.modifiers_raw >> shift) & 15U;
            if (modifier > 5 && modifier != 8 && modifier != 9 && modifier != 12 && modifier != 13)
                return false;
        }
        const u32 color_op = stage.ops_raw & 15U;
        const u32 alpha_op = (stage.ops_raw >> 16) & 15U;
        if (color_op > 9 || alpha_op > 9 ||
            (color_op != 7 && (alpha_op == 6 || alpha_op == 7)))
            return false;
    }
    const auto& lighting = config.lighting;
    const u32 lighting_config = static_cast<u32>(lighting.config.Value());
    if ((lighting.raw & ~0xFFFFFFU) || lighting.src_num.Value() > 8 ||
        static_cast<u32>(lighting.bump_mode.Value()) > 2 ||
        (lighting_config > 6 && lighting_config != 8))
        return false;
    for (const auto& light : lighting.lights)
        if (light.raw & ~0x3FFU) return false;
    for (const auto* lut : {&lighting.lut_d0, &lighting.lut_d1, &lighting.lut_sp,
                            &lighting.lut_fr, &lighting.lut_rr, &lighting.lut_rg, &lighting.lut_rb}) {
        if ((lut->raw & ~31U) || static_cast<u32>(lut->type.Value()) > 5) return false;
        const float scale = lut->GetScale();
        if (scale != 0.0f && scale != 0.25f && scale != 0.5f && scale != 1.0f &&
            scale != 2.0f && scale != 4.0f && scale != 8.0f)
            return false;
    }
    const auto& proc = config.proctex;
    if ((proc.raw & ~0x3FFFFFFU) || proc.coord.Value() >= 3 ||
        static_cast<u32>(proc.u_clamp.Value()) > 4 ||
        static_cast<u32>(proc.v_clamp.Value()) > 4 ||
        static_cast<u32>(proc.color_combiner.Value()) > 9 ||
        static_cast<u32>(proc.alpha_combiner.Value()) > 9 ||
        static_cast<u32>(proc.lut_filter.Value()) > 5 ||
        static_cast<u32>(proc.u_shift.Value()) > 2 ||
        static_cast<u32>(proc.v_shift.Value()) > 2 ||
        proc.lut_width < 0 || proc.lut_width > 255 || proc.lod_min > 15 || proc.lod_max > 15)
        return false;
    for (s32 offset : {proc.lut_offset0, proc.lut_offset1, proc.lut_offset2, proc.lut_offset3})
        if (offset < 0 || offset > 255) return false;
    if (!proc.enable) return true;
    if (proc.lut_width == 0 || proc.lod_min > std::min<u16>(7, proc.lod_max)) return false;
    // CodexAstraUlt-2: Each procedural mip range must remain within its 256-entry LUT.
    const std::array offsets{proc.lut_offset0, proc.lut_offset1, proc.lut_offset2, proc.lut_offset3};
    for (u32 level = 0; level < offsets.size(); ++level)
        if (offsets[level] + (proc.lut_width >> level) > 256) return false;
    return true;
}

// CodexAstraUlt-2: Match MakeSoftwareVertexLayout exactly; unknown interfaces remain normal
// runtime misses. Inactive array tails are omitted semantically and reset during decoding.
inline bool IsEligible(const Pica::Shader::FSConfig& config, const StaticPipelineInfo& state) {
    if (!IsValidConfig(config) || state.shader_ids[0] || state.shader_ids[2]) return false;
    const auto& layout = state.vertex_layout;
    constexpr std::array<u32, 8> sizes{4, 4, 2, 2, 2, 1, 4, 3};
    if (layout.binding_count != 1 || layout.attribute_count != sizes.size() ||
        layout.bindings[0].binding.Value() != 0 || layout.bindings[0].fixed.Value() ||
        layout.bindings[0].byte_count.Value() != 88)
        return false;
    u32 offset = 0;
    for (u32 i = 0; i < sizes.size(); ++i) {
        const auto& attribute = layout.attributes[i];
        if (attribute.binding.Value() || attribute.location.Value() != i ||
            attribute.type.Value() != Pica::PipelineRegs::VertexAttributeFormat::FLOAT ||
            attribute.size.Value() != sizes[i] || attribute.offset.Value() != offset ||
            (attribute.value & ~0x1FFFFFFU))
            return false;
        offset += sizes[i] * sizeof(float);
    }
    const auto& blend = state.blending;
    const auto color = state.attachments.color;
    const auto depth = state.attachments.depth;
    const bool color_valid = static_cast<u32>(color) <= 4 || color == VideoCore::PixelFormat::Invalid;
    const bool depth_valid = depth == VideoCore::PixelFormat::Invalid ||
        depth == VideoCore::PixelFormat::D16 || depth == VideoCore::PixelFormat::D24 ||
        depth == VideoCore::PixelFormat::D24S8;
    return color_valid && depth_valid &&
        !(color == VideoCore::PixelFormat::Invalid && depth == VideoCore::PixelFormat::Invalid) &&
        blend.blend_enable <= 1 && blend.color_write_mask <= 15 &&
        static_cast<u32>(blend.logic_op) <= 15 && !(blend.value & ~0x3FFFFFU) &&
        static_cast<u32>(blend.src_color_blend_factor.Value()) <= 14 &&
        static_cast<u32>(blend.dst_color_blend_factor.Value()) <= 14 &&
        static_cast<u32>(blend.src_alpha_blend_factor.Value()) <= 14 &&
        static_cast<u32>(blend.dst_alpha_blend_factor.Value()) <= 14 &&
        static_cast<u32>(blend.color_blend_eq.Value()) <= 4 &&
        static_cast<u32>(blend.alpha_blend_eq.Value()) <= 4 &&
        state.rasterization.topology.Value() == Pica::PipelineRegs::TriangleTopology::List &&
        !(state.rasterization.value & ~0x73U) && !(state.depth_stencil.value & ~0x3FFFFU);
}

namespace Detail {
// CodexAstraUlt-2: Explicit little-endian scalar traversal avoids structure padding, host
// handles, endian assumptions and uninitialized inactive layout tails in persistent bytes.
template <class Config, class IO>
void ConfigFields(Config& config, IO& io) {
    auto& fb = config.framebuffer;
    io(fb.raw);
    for (auto* blend : {&fb.requested_rgb_blend, &fb.requested_alpha_blend}) {
        io(blend->eq); io(blend->src_factor); io(blend->dst_factor);
    }
    io(fb.requested_logic_op);
    auto& texture = config.texture;
    io(texture.raw);
    for (auto& border : texture.texture_border_color) io(border.raw);
    for (auto& stage : texture.tev_stages) {
        io(stage.sources_raw); io(stage.modifiers_raw); io(stage.ops_raw); io(stage.scales_raw);
    }
    for (auto& wrap : texture.requested_wrap) { io(wrap.s); io(wrap.t); }
    auto& lighting = config.lighting;
    io(lighting.raw);
    for (auto* lut : {&lighting.lut_d0, &lighting.lut_d1, &lighting.lut_sp, &lighting.lut_fr,
                      &lighting.lut_rr, &lighting.lut_rg, &lighting.lut_rb}) {
        io(lut->raw); io(lut->scale_bits);
    }
    for (auto& light : lighting.lights) io(light.raw);
    auto& proc = config.proctex;
    io(proc.raw); io(proc.lut_width); io(proc.lut_offset0); io(proc.lut_offset1);
    io(proc.lut_offset2); io(proc.lut_offset3); io(proc.lod_min); io(proc.lod_max);
}
template <class State, class IO>
void PipelineFields(State& state, IO& io) {
    auto& blend = state.blending;
    io(blend.blend_enable); io(blend.color_write_mask); io(blend.logic_op); io(blend.value);
    io(state.attachments.color); io(state.attachments.depth);
    auto& layout = state.vertex_layout;
    io(layout.binding_count); io(layout.attribute_count);
    for (auto& binding : layout.bindings) io(binding.value);
    for (auto& attribute : layout.attributes) io(attribute.value);
    io(state.rasterization.value); io(state.depth_stencil.value);
}
inline StaticPipelineInfo CanonicalPipeline(const StaticPipelineInfo& state) {
    auto result = state;
    result.shader_ids = {};
    for (std::size_t i = result.vertex_layout.binding_count; i < MAX_VERTEX_BINDINGS; ++i)
        result.vertex_layout.bindings[i].value = 0;
    for (std::size_t i = result.vertex_layout.attribute_count; i < MAX_VERTEX_ATTRIBUTES; ++i)
        result.vertex_layout.attributes[i].value = 0;
    return result;
}
struct Writer {
    std::vector<u8>& bytes;
    template <class T> void operator()(const T& value) {
        const u64 bits = static_cast<u64>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes.push_back(static_cast<u8>(bits >> (8 * i)));
    }
};
struct Reader {
    std::span<const u8> bytes;
    std::size_t cursor{};
    bool good{true};
    template <class T> void operator()(T& value) {
        if (sizeof(T) > bytes.size() - cursor) { good = false; return; }
        u64 bits{};
        for (std::size_t i = 0; i < sizeof(T); ++i) bits |= u64{bytes[cursor++]} << (8 * i);
        value = static_cast<T>(bits);
    }
};
struct Hasher {
    u64 value = 14695981039346656037ULL;
    template <class T> void operator()(const T& field) {
        const u64 bits = static_cast<u64>(field);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value ^= static_cast<u8>(bits >> (8 * i));
            value *= 1099511628211ULL;
        }
    }
};
inline u64 Checksum(std::span<const u8> bytes) {
    Hasher hash;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (i < 56 || i >= HeaderBytes) hash(bytes[i]);
    return hash.value;
}
inline Pica::Shader::FSConfig EmptyConfig() {
    static_assert(std::is_trivially_copyable_v<Pica::Shader::FSConfig>);
    static_assert(std::has_unique_object_representations_v<Pica::Shader::FSConfig>);
    static_assert(sizeof(Pica::Shader::FSConfig) == ConfigBytes);
    return std::bit_cast<Pica::Shader::FSConfig>(std::array<u8, ConfigBytes>{});
}
inline u64 AddUses(u64 a, u64 b) {
    return a + std::min(b, std::numeric_limits<u64>::max() - a);
}
} // CodexAstraUlt-2: namespace Detail

// CodexAstraUlt-2: Hashes choose a deduplication bucket; exact semantic comparison resolves
// every collision. FS identity stays byte-exact and all consumed static fields stay distinct.
inline u64 Hash(const Pica::Shader::FSConfig& config, const StaticPipelineInfo& pipeline) {
    Detail::Hasher hash;
    Detail::ConfigFields(config, hash);
    auto canonical = Detail::CanonicalPipeline(pipeline);
    Detail::PipelineFields(canonical, hash);
    return hash.value;
}
inline bool Equals(const Recipe& a, const Recipe& b) {
    if (!(a.config == b.config)) return false;
    const auto& x = a.pipeline;
    const auto& y = b.pipeline;
    if (x.blending.blend_enable != y.blending.blend_enable ||
        x.blending.color_write_mask != y.blending.color_write_mask ||
        x.blending.logic_op != y.blending.logic_op || x.blending.value != y.blending.value ||
        x.attachments.color != y.attachments.color || x.attachments.depth != y.attachments.depth ||
        x.rasterization.value != y.rasterization.value || x.depth_stencil.value != y.depth_stencil.value ||
        x.vertex_layout.binding_count != y.vertex_layout.binding_count ||
        x.vertex_layout.attribute_count != y.vertex_layout.attribute_count)
        return false;
    for (u32 i = 0; i < std::min<u32>(x.vertex_layout.binding_count, MAX_VERTEX_BINDINGS); ++i)
        if (x.vertex_layout.bindings[i].value != y.vertex_layout.bindings[i].value) return false;
    for (u32 i = 0; i < std::min<u32>(x.vertex_layout.attribute_count, MAX_VERTEX_ATTRIBUTES); ++i)
        if (x.vertex_layout.attributes[i].value != y.vertex_layout.attributes[i].value) return false;
    return true;
}

inline std::vector<u8> Encode(const std::vector<Recipe>& recipes, Identity identity) {
    if (recipes.size() > MaxRecipes) return {};
    std::vector<Recipe> unique;
    unique.reserve(recipes.size());
    std::unordered_map<u64, std::vector<std::size_t>> buckets;
    for (const auto& recipe : recipes) {
        if (!recipe.uses || !IsEligible(recipe.config, recipe.pipeline)) return {};
        auto& bucket = buckets[Hash(recipe.config, recipe.pipeline)];
        const auto found = std::find_if(bucket.begin(), bucket.end(), [&](std::size_t i) {
            return Equals(unique[i], recipe);
        });
        if (found != bucket.end()) {
            unique[*found].uses = Detail::AddUses(unique[*found].uses, recipe.uses);
        } else {
            bucket.push_back(unique.size());
            unique.push_back({recipe.config, Detail::CanonicalPipeline(recipe.pipeline), recipe.uses});
        }
    }
    std::stable_sort(unique.begin(), unique.end(), [](const Recipe& a, const Recipe& b) {
        return a.uses > b.uses;
    });
    std::vector<u8> bytes;
    bytes.reserve(HeaderBytes + unique.size() * RecipeBytes);
    Detail::Writer write{bytes};
    write(Magic); write(Schema); write(static_cast<u32>(unique.size()));
    write(identity.title); write(identity.compiler); write(identity.profile);
    write(Pica::Shader::FSConfig::StructHash()); write(static_cast<u32>(RecipeBytes));
    write(u32{}); write(u64{});
    for (auto& recipe : unique) {
        Detail::ConfigFields(recipe.config, write);
        Detail::PipelineFields(recipe.pipeline, write);
        write(recipe.uses);
    }
    if (bytes.size() != HeaderBytes + unique.size() * RecipeBytes) return {};
    const u64 checksum = Detail::Checksum(bytes);
    for (std::size_t i = 0; i < sizeof(checksum); ++i) bytes[56 + i] = static_cast<u8>(checksum >> (8 * i));
    return bytes;
}

// CodexAstraUlt-2: Check framing and bounded allocation before parsing any entry. Reject the
// complete file on semantic errors or duplicates; required rendering retains its normal fallback.
inline std::optional<std::vector<Recipe>> Decode(std::span<const u8> bytes, Identity identity) {
    if (bytes.size() < HeaderBytes || bytes.size() > MaxEncodedBytes) return std::nullopt;
    Detail::Reader read{bytes};
    u64 magic{}, title{}, compiler{}, profile{}, layout{}, checksum{};
    u32 schema{}, count{}, record_bytes{}, reserved{};
    read(magic); read(schema); read(count); read(title); read(compiler); read(profile);
    read(layout); read(record_bytes); read(reserved); read(checksum);
    if (magic != Magic || schema != Schema || count > MaxRecipes || title != identity.title ||
        compiler != identity.compiler || profile != identity.profile ||
        layout != Pica::Shader::FSConfig::StructHash() || record_bytes != RecipeBytes || reserved ||
        bytes.size() != HeaderBytes + std::size_t{count} * RecipeBytes ||
        checksum != Detail::Checksum(bytes))
        return std::nullopt;
    std::vector<Recipe> recipes;
    recipes.reserve(count);
    std::unordered_map<u64, std::vector<std::size_t>> buckets;
    for (u32 i = 0; i < count; ++i) {
        Recipe recipe{Detail::EmptyConfig(), {}, 0};
        Detail::ConfigFields(recipe.config, read);
        Detail::PipelineFields(recipe.pipeline, read);
        read(recipe.uses);
        if (!read.good || !recipe.uses || !IsEligible(recipe.config, recipe.pipeline)) return std::nullopt;
        const auto canonical = Detail::CanonicalPipeline(recipe.pipeline);
        for (u32 j = recipe.pipeline.vertex_layout.binding_count; j < MAX_VERTEX_BINDINGS; ++j)
            if (recipe.pipeline.vertex_layout.bindings[j].value) return std::nullopt;
        for (u32 j = recipe.pipeline.vertex_layout.attribute_count; j < MAX_VERTEX_ATTRIBUTES; ++j)
            if (recipe.pipeline.vertex_layout.attributes[j].value) return std::nullopt;
        auto& bucket = buckets[Hash(recipe.config, canonical)];
        for (const std::size_t previous : bucket)
            if (Equals(recipes[previous], recipe)) return std::nullopt;
        bucket.push_back(recipes.size());
        recipes.push_back(recipe);
    }
    return read.good && read.cursor == bytes.size() ? std::optional{std::move(recipes)} : std::nullopt;
}
} // CodexAstraUlt-2: namespace Vulkan::Precompile

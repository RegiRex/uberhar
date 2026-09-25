// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/shader/generator/pica_fs_config.h"

namespace Pica::Shader::Generator::GLSL {

/// AstraEH: Conservative support gate for the initial Vulkan TEV experiment.
bool SupportsDynamicTev(const FSConfig& config, const UserConfig& user);

/// AstraEH: Canonicalize only shader-irrelevant fallback state for a fixed device profile.
/// Sampler and fixed-function pipeline state must still be supplied separately by the caller.
FSConfig MakeDynamicTevFamilyConfig(const FSConfig& config, const Profile& profile);

// AstraEH: Version 3 of the private Vulkan fallback ABI. Stay below Vulkan's
// 128-byte minimum push-constant limit; specialized/disk shader layouts are unchanged.
struct DynamicTevState {
    std::array<TevStageConfigRaw, 6> stages;
    u32 buffer_mask;
    u32 framebuffer; // alpha function [0:2], scissor [3:4], W buffering [5].
    u32 texture;     // border axes [0:5], coord2 [6], fog [7], flip [8], tex0 type [10:12].
    // AstraEH: Seven LUT controls use one byte each: input [0:2], unsigned [3],
    // scale [4:6]. Order: D0, D1, spotlight, Fresnel, red, green, blue.
    u32 lighting_luts_lo{};
    u32 lighting_luts_hi{};
    // AstraEH: Eight three-bit source selectors, then the original slot-indexed
    // two-sided flags. Keep inherited LUT indexing distinct from diffuse indexing.
    u32 lighting_sources{};
};
static_assert(sizeof(DynamicTevState) == 120);
static_assert(offsetof(DynamicTevState, buffer_mask) == 96);
static_assert(offsetof(DynamicTevState, framebuffer) == 100);
static_assert(offsetof(DynamicTevState, texture) == 104);
static_assert(offsetof(DynamicTevState, lighting_luts_lo) == 108);
static_assert(offsetof(DynamicTevState, lighting_luts_hi) == 112);
static_assert(offsetof(DynamicTevState, lighting_sources) == 116);

/// AstraEH: Capture effective runtime state before family canonicalization removes it.
DynamicTevState MakeDynamicTevState(const FSConfig& config, const Profile& profile);

class FragmentModule {
public:
    // AstraEH: Existing callers stay specialized; Vulkan fallback callers opt into dynamic TEV.
    explicit FragmentModule(const FSConfig& config, const UserConfig& user, const Profile& profile,
                            bool dynamic_tev = false);
    ~FragmentModule();

    /// Emits GLSL source corresponding to the provided pica fragment configuration
    std::string Generate();

private:
    /// Undos the host perspective transformation and applies the PICA one
    void WriteDepth();

    /// Emits code to emulate the scissor rectangle
    void WriteScissor();

    /// Writes the code to emulate fragment lighting
    void WriteLighting();

    /// Writes the code to emulate fog
    void WriteFog();

    /// Writes the code to emulate gas rendering
    void WriteGas();

    /// Writes the code to emulate shadow-map rendering
    void WriteShadow();

    /// Writes the code to emulate logic ops in the fragment shader
    void WriteLogicOp();

    /// Writes the code to emulate PICA min/max blending factors
    void WriteBlending();

    /// Returns the specified TEV stage source component(s)
    std::string GetSource(Pica::TexturingRegs::TevStageConfig::Source source, u32 tev_index);

    /// Writes the color components to use for the specified TEV stage color modifier
    void AppendColorModifier(Pica::TexturingRegs::TevStageConfig::ColorModifier modifier,
                             Pica::TexturingRegs::TevStageConfig::Source source, u32 tev_index);

    /// Writes the alpha component to use for the specified TEV stage alpha modifier
    void AppendAlphaModifier(Pica::TexturingRegs::TevStageConfig::AlphaModifier modifier,
                             Pica::TexturingRegs::TevStageConfig::Source source, u32 tev_index);

    /// Writes the combiner function for the color components for the specified TEV stage operation
    void AppendColorCombiner(Pica::TexturingRegs::TevStageConfig::Operation operation);

    /// Writes the combiner function for the alpha component for the specified TEV stage operation
    void AppendAlphaCombiner(Pica::TexturingRegs::TevStageConfig::Operation operation);

    /// Writes the if-statement condition used to evaluate alpha testing
    void WriteAlphaTestCondition(Pica::FramebufferRegs::CompareFunc func);

    /// Writes the code to emulate the specified TEV stage
    void WriteTevStage(u32 index);

    /// AstraEH: Vulkan experiment: interpret TEV registers supplied in push constants.
    void DefineDynamicTev();
    // AstraEH: Declare runtime state before sampling helpers that consume it.
    void DefineDynamicState();
    void WriteDynamicTevLoop();

    void AppendProcTexShiftOffset(std::string_view v, Pica::TexturingRegs::ProcTexShift mode,
                                  Pica::TexturingRegs::ProcTexClamp clamp_mode);

    void AppendProcTexClamp(std::string_view var, Pica::TexturingRegs::ProcTexClamp mode);

    void AppendProcTexCombineAndMap(Pica::TexturingRegs::ProcTexCombiner combiner,
                                    std::string_view offset);

    void DefineExtensions();
    void DefineInterface();
    void DefineBindingsVK();
    void DefineBindingsGL();
    void DefineHelpers();
    void DefineLightingHelpers();
    void DefineShadowHelpers();
    void DefineProcTexSampler();
    void DefineTexUnitSampler(u32 i);

private:
    FSConfig config;
    const UserConfig& user;
    const Profile& profile;
    std::string out;
    bool
        dynamic_tev{}; // AstraEH: Select runtime TEV instructions instead of baked stage constants.
    bool use_blend_fallback{};
    bool use_fragment_shader_interlock{};
    bool use_fragment_shader_barycentric{};
};

/**
 * Generates the GLSL fragment shader program source code for the current Pica state
 * @param config ShaderCacheKey object generated for the current Pica state, used for the shader
 *               configuration (NOTE: Use state in this struct only, not the Pica registers!)
 * @returns String of the shader source code
 */
std::string GenerateFragmentShader(const FSConfig& config, const UserConfig& user,
                                   const Profile& profile);

} // namespace Pica::Shader::Generator::GLSL

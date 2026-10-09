// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include "common/hash.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"

namespace Vulkan::StaticTevPolicy {
// CodexAstraLocal: Bound partial specialization globally within one title, not
// per generic family. Failed/allocation attempts are never refunded; modules
// retain stable ownership until the existing shader/pipeline drain boundary.
inline constexpr std::size_t MaxModules = 8;
inline constexpr std::size_t MaxModuleAttempts = 16;
inline constexpr std::size_t MaxPsoOwners = 8;
inline constexpr std::size_t MaxPsoAttempts = 16;
inline constexpr u32 CpuInterface = 1; // Trivial VS / original 88-byte CPU vertices.
inline constexpr u32 GeneratorPolicy = 1; // Optimized static TEV, runtime ABI7 remainder.

struct Key {
    Pica::Shader::FSConfig family;
    Pica::Shader::Profile profile;
    Pica::Shader::Generator::GLSL::StaticTevPlan plan;
    u32 interface_abi = CpuInterface;
    u32 policy = GeneratorPolicy;

    // CodexAstraLocal: Hashes only locate an entry. The full canonical family,
    // profile, exact prepared plan and CPU generator contract admit execution.
    bool operator==(const Key& other) const noexcept {
        return family == other.family && profile == other.profile && plan == other.plan &&
               interface_abi == other.interface_abi && policy == other.policy;
    }
    static u64 Hash(const Pica::Shader::FSConfig& family,
                    const Pica::Shader::Generator::GLSL::StaticTevPlan& plan) noexcept {
        return Common::HashCombine(family.Hash(), Common::ComputeStructHash64(plan),
                                   CpuInterface, GeneratorPolicy);
    }
};
} // namespace Vulkan::StaticTevPolicy

// CodexAstraLocal: Automatic bounded CPU PSO reuse keeps an exact
// observation key controls probation only, never replaces execution-owner checks.
#pragma once
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h"
#include "video_core/renderer_vulkan/uberhar_static_tev_policy.h"
namespace Vulkan {
struct CpuFragmentObservation {
    StaticPipelineInfo state;
    Pica::Shader::FSConfig fs;
    Pica::Shader::Profile profile;
    bool dynamic;
    // CodexAstraLocal: Partial probation uses the same canonical family/plan as
    // the module; runtime-only lighting/material changes cannot alias a different
    // program or create independent PSO demand for identical executable state.
    std::optional<Pica::Shader::Generator::GLSL::StaticTevPlan> static_plan{};
    bool operator==(const CpuFragmentObservation& other) const noexcept {
        return dynamic == other.dynamic && state.ExecutionEquals(other.state, dynamic) &&
            fs == other.fs && profile == other.profile && static_plan == other.static_plan;
    }
    u64 Hash() const noexcept {
        // CodexAstraLocal: Profile collisions defer probation through exact equality; this hash
        // deliberately does not read padding or copy the full profile twice.
        return Common::HashCombine(state.ExecutionHash(dynamic, {}), fs.Hash(), dynamic,
            static_plan ? Common::ComputeStructHash64(*static_plan) : 0);
    }
};
// CodexAstraLocal: Preserve the inherited full-specialization allowance while
// the additional partial tier has its own smaller attempt budget. Retirement,
// probation and admission use this same partition, including failed owners.
struct CpuFragmentPartitions {
    static constexpr std::size_t Slots = 8 + StaticTevPolicy::MaxPsoOwners,
                                 CpuAttempts = 64 + StaticTevPolicy::MaxPsoAttempts, Count = 2;
    static std::size_t Index(const CpuFragmentObservation& key) { return key.static_plan ? 1 : 0; }
    static constexpr std::size_t Owners(std::size_t partition) {
        return partition ? StaticTevPolicy::MaxPsoOwners : 8;
    }
    static constexpr std::size_t Attempts(std::size_t partition) {
        return partition ? StaticTevPolicy::MaxPsoAttempts : 64;
    }
};
using ReadyCpuBank = Vulkan::AdaptiveCpu::Cache<CpuFragmentObservation, GraphicsPipeline,
                                               CpuFragmentPartitions>;
} // namespace Vulkan

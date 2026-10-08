// CodexAstraLocal: Automatic bounded CPU PSO reuse keeps an exact
// observation key controls probation only, never replaces execution-owner checks.
#pragma once
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h"
namespace Vulkan {
struct CpuFragmentObservation {
    StaticPipelineInfo state;
    Pica::Shader::FSConfig fs;
    Pica::Shader::Profile profile;
    bool dynamic;
    bool operator==(const CpuFragmentObservation& other) const noexcept {
        return dynamic == other.dynamic && state.ExecutionEquals(other.state, dynamic) &&
            fs == other.fs && profile == other.profile;
    }
    u64 Hash() const noexcept {
        // CodexAstraLocal: Profile collisions defer probation through exact equality; this hash
        // deliberately does not read padding or copy the full profile twice.
        return Common::HashCombine(state.ExecutionHash(dynamic, {}), fs.Hash(), dynamic);
    }
};
using ReadyCpuBank = Vulkan::AdaptiveCpu::Cache<CpuFragmentObservation, GraphicsPipeline>;
} // namespace Vulkan

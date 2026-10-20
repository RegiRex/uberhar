// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <optional>
#include "video_core/shader/generator/glsl_fs_shader_gen.h"

namespace Vulkan {

// CodexAstraLocal: Reuse only the pure 128-byte fragment transport for exactly
// equal original guest configuration and complete host profile. There are no
// pipeline handles, readiness decisions, guest-memory borrows or dynamic Vulkan
// bindings here. The draw owner copies the result before queuing its consumer.
class DynamicTevPreparation {
public:
    struct Counters {
        u64 requests{}, hits{}, builds{};
        // CodexAstraLocal: Distinguish structural decoding from numeric-only
        // certificate updates; neither counter estimates avoided CPU/GPU time.
        u64 structural_builds{}, value_updates{};
    };

    const Pica::Shader::Generator::GLSL::DynamicTevState& Get(
        const Pica::Shader::FSConfig& next, const Pica::Shader::Profile& next_profile,
        const Pica::Shader::Generator::GLSL::FragmentRuntimeValues& next_values = {}) {
        ++counters.requests;
        const bool same_structure = config && *config == next && profile == next_profile;
        if (same_structure && values == next_values) {
            ++counters.hits;
            return state;
        }
        // CodexAstraLocal: Numeric uniforms do not alter structural liveness,
        // memo eligibility or packed TEV/lighting instructions. Reuse those
        // exact bytes instead of rerunning their analysis on every value change.
        if (!same_structure) {
            structure = Pica::Shader::Generator::GLSL::MakeDynamicTevState(next, next_profile);
            ++counters.structural_builds;
        } else {
            ++counters.value_updates;
        }
        // CodexAstraLocal: Begin each numeric refresh with certificate-free
        // structural bytes; unavailable/nonendpoint values cannot inherit a
        // prior draw's result. Publish the complete exact key after preparation.
        state = structure;
        Pica::Shader::Generator::GLSL::ApplyFragmentRuntime(state, next_profile, next_values);
        config = next;
        profile = next_profile;
        values = next_values;
        ++counters.builds;
        return state;
    }

    // CodexAstraLocal: Title/cache teardown resets both reusable bytes and their
    // evidence. Profile changes also miss through complete equality in Get.
    void Reset() {
        config.reset();
        counters = {};
    }
    const Counters& Stats() const { return counters; }

private:
    std::optional<Pica::Shader::FSConfig> config;
    Pica::Shader::Profile profile{};
    Pica::Shader::Generator::GLSL::FragmentRuntimeValues values{};
    // CodexAstraLocal: One additional 128-byte owner snapshot avoids structural
    // rebuilds without retaining guest memory, shader owners or extra histories.
    Pica::Shader::Generator::GLSL::DynamicTevState structure{};
    Pica::Shader::Generator::GLSL::DynamicTevState state{};
    Counters counters;
};

} // namespace Vulkan

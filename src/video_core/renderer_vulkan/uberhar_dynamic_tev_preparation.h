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
    };

    const Pica::Shader::Generator::GLSL::DynamicTevState& Get(
        const Pica::Shader::FSConfig& next, const Pica::Shader::Profile& next_profile) {
        ++counters.requests;
        if (config && *config == next && profile == next_profile) {
            ++counters.hits;
            return state;
        }
        // CodexAstraLocal: Full equality, never a hash or partial dirty-bit map,
        // proves reuse. Publish the key only after the original producer succeeds.
        state = Pica::Shader::Generator::GLSL::MakeDynamicTevState(next, next_profile);
        config = next;
        profile = next_profile;
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
    Pica::Shader::Generator::GLSL::DynamicTevState state{};
    Counters counters;
};

} // namespace Vulkan

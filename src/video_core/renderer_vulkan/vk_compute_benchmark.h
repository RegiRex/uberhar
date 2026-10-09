// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <memory>
#include "common/common_types.h"

namespace Pica::Shader {
struct Profile;
}

namespace Vulkan {
class Instance;
class Scheduler;
class RenderManager;

// CodexAstraLocal: An explicit create-only request owns synthetic scratch work.
// This object never services guest fallback, changes selector statistics or
// frees submitted resources on timeout; destruction follows the existing drain.
class ComputeBenchmark final {
public:
    static std::unique_ptr<ComputeBenchmark> Load(const Instance& instance, Scheduler& scheduler,
                                                 RenderManager& renderpass,
                                                 const Pica::Shader::Profile& profile, u64 title);
    ~ComputeBenchmark();
    // CodexAstraLocal: Shared scheduler/device failure remains terminal; only
    // benchmark-local parser, setup and evidence failures disable this request.
    void Poll(u64 title);
    void FinishAfterDrain() noexcept;

private:
    struct Impl;
    explicit ComputeBenchmark(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};
} // namespace Vulkan

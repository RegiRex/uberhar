// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <algorithm>
#include <chrono>
#include <span>
#include <vector>
#include "common/common_types.h"
#if defined(UBERHAR_HAS_SPIRV_TOOLS_OPT)
#include <spirv-tools/optimizer.hpp>
#endif

namespace Vulkan {
// CodexAstraLocal: A missing alternate has a concrete reason, not a silent
// compatibility success. Size and wall cost belong to this attempted transform.
enum class ComputeLoweringStatus : u32 { Unavailable, InputBound, Invalid, OutputBound, Unchanged, Ready };
struct ComputeLoweringReport {
    ComputeLoweringStatus status{ComputeLoweringStatus::Unavailable};
    u64 input_bytes{}, output_bytes{}, host_ns{};
};
inline const char* ComputeLoweringStatusName(ComputeLoweringStatus status) {
    switch (status) {
    case ComputeLoweringStatus::Unavailable: return "unavailable";
    case ComputeLoweringStatus::InputBound: return "input_bound";
    case ComputeLoweringStatus::Invalid: return "validation_or_pass_failure";
    case ComputeLoweringStatus::OutputBound: return "output_bound";
    case ComputeLoweringStatus::Unchanged: return "unchanged";
    case ComputeLoweringStatus::Ready: return "ready";
    }
    return "invalid_status";
}
// CodexAstraLocal: Revision2 lowers local state without exhaustive inlining.
// Inlining the captured DM module expanded it25.6x, so it is deliberately absent.
// No algebraic FP simplification, reassociation or relaxed precision is enabled.
inline std::vector<u32> LowerComputePrivateState(std::span<const u32> code,
                                                ComputeLoweringReport* report = nullptr) {
    const auto begin = std::chrono::steady_clock::now();
    const auto finish = [&](ComputeLoweringStatus status, std::size_t output_bytes) {
        if (report) *report = {status, code.size_bytes(), output_bytes,
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count())};
    };
#if defined(UBERHAR_HAS_SPIRV_TOOLS_OPT)
    constexpr std::size_t MaxWords = 4 * 1024 * 1024 / sizeof(u32);
    if (code.empty() || code.size() > MaxWords) {
        finish(ComputeLoweringStatus::InputBound, 0); return {};
    }
    spvtools::Optimizer optimizer{SPV_ENV_VULKAN_1_1};
    optimizer.SetValidateAfterAll(true);
    optimizer.RegisterPass(spvtools::CreateEliminateDeadFunctionsPass());
    optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
    optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
    optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
    optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
    optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
    std::vector<u32> lowered;
    // CodexAstraLocal: Validate input and each pass. Bound both absolute storage
    // and relative expansion before a driver sees an alternate; no retry loop.
    if (!optimizer.Run(code.data(), code.size(), &lowered) || lowered.empty()) {
        finish(ComputeLoweringStatus::Invalid, lowered.size()*sizeof(u32)); return {};
    }
    if (lowered.size() > MaxWords || lowered.size() > code.size()*2) {
        finish(ComputeLoweringStatus::OutputBound, lowered.size()*sizeof(u32)); return {};
    }
    if (lowered.size() == code.size() &&
        std::equal(lowered.begin(), lowered.end(), code.begin())) {
        finish(ComputeLoweringStatus::Unchanged, lowered.size()*sizeof(u32)); return {};
    }
    finish(ComputeLoweringStatus::Ready, lowered.size()*sizeof(u32));
    return lowered;
#else
    finish(ComputeLoweringStatus::Unavailable, 0);
    return {};
#endif
}
} // namespace Vulkan

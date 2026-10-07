// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

namespace Vulkan {

// CodexAstraUlt: Generic first-use jobs retain the requested frontend latency policy.
// Optional ready-only jobs run while a complete CPU draw remains available, so they
// can optimize their private variables/control flow before publishing a usable module.
// This policy is immutable for each renderer/cache owner; Custom keeps its override.
constexpr bool DisableShaderOptimizer(bool ready_only, bool requested_disabled) {
    return !ready_only && requested_disabled;
}

} // namespace Vulkan

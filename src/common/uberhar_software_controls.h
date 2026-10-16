// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include "common/settings.h"
namespace Settings {
// CodexAstraLocal: Restart-only, independently persisted Software comparisons.
// Bit0 exact TEV reuse, bit1 coverage spans, bit2 useful row-work admission,
// bit3 certified owned constant-fill groups. Disabled bits keep the legacy path;
// live-input checks are mandatory and have no disable switch.
inline Setting<u32, true> software_renderer_features{15, 0, 15, "software_renderer_features"};
// CodexAstraLocal TEST-ONLY LOG: Diagnostic clocks/counters are independent of
// optimizations. Remove after matched Software qualification; retain errors.
inline Setting<bool> software_renderer_diagnostics{true, "software_renderer_diagnostics"};
}

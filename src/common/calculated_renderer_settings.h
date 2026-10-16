// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include "common/calculated_features.h"
#include "common/settings.h"
namespace Settings {
// CodexAstraLocal: Separate settings avoid changing Values ABI. Renderer owners
// snapshot once at construction; changes take effect only on the next renderer.
// Retry OFF intentionally restores terminal behavior for the known driver -13.
inline Setting<u32> calculated_renderer_features{CalculatedFeatures::Default,
                                                "calculated_renderer_features"};
inline Setting<bool> calculated_renderer_diagnostics{true,"calculated_renderer_diagnostics"};
} // namespace Settings
namespace CalculatedFeatures {
struct Snapshot {
    u32 requested{Settings::calculated_renderer_features.GetValue()};
    u32 effective{Effective(requested)};
    bool diagnostics{Settings::calculated_renderer_diagnostics.GetValue()};
    // CodexAstraLocal: Include source ABI revision and diagnostic success-header
    // variant in private owner cache keys, even though these caches are not saved.
    u64 Key() const { return (u64{Revision}<<33)|(u64{diagnostics}<<32)|effective; }
    bool Has(u32 bit) const { return (effective&bit)!=0; }
};
} // namespace CalculatedFeatures

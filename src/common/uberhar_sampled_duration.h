// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <chrono>
#include <cstdint>

namespace Common::Uberhar {

// CodexAstraLocal TEST-ONLY LOG: Owner-thread wait evidence reads clocks for one
// call in 64, never waits itself, and includes exceptional exits. Remove after
// command/resource wait attribution is qualified; sums are sampled, not totals.
struct SampledDuration {
    using Clock = std::chrono::steady_clock;
    std::uint64_t calls{}, samples{}, ns{};
    struct Scope {
        SampledDuration& owner;
        Clock::time_point start{};
        Scope(SampledDuration& owner_, bool enabled) : owner{owner_} {
            if (enabled && (++owner.calls & 63U) == 1) start = Clock::now();
        }
        Scope(const Scope&) = delete;
        ~Scope() {
            if (start == Clock::time_point{}) return;
            owner.ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            ++owner.samples;
        }
    };
    Scope Measure(bool enabled = true) { return Scope{*this, enabled}; }
};

} // namespace Common::Uberhar

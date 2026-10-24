// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace Vulkan {
// CodexAstraLocal: Bound optional compiler work without permanently freezing a
// long session. Time replenishes credit; eviction and failure never refund it.
class CompileBudget {
public:
    CompileBudget(std::uint64_t capacity, std::uint64_t interval_ms)
        : capacity(capacity), interval_ms(interval_ms), credits(capacity) {}
    static std::uint64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void Refill(std::uint64_t now) {
        if (!initialized || now < last) { initialized = true; last = now; return; }
        const auto earned = (now - last) / interval_ms;
        if (!earned) return;
        const auto added = std::min(earned, capacity - credits);
        credits += added;
        replenished += added;
        last += earned * interval_ms;
    }
    bool Available() const { return credits != 0; }
    bool Spend() {
        if (!credits) { ++throttled; return false; }
        --credits;
        return true;
    }
    void Reset() { credits = capacity; initialized = false; replenished = throttled = 0; }
    std::uint64_t Credits() const { return credits; }
    std::uint64_t Replenished() const { return replenished; }
    std::uint64_t Throttled() const { return throttled; }
private:
    const std::uint64_t capacity, interval_ms;
    std::uint64_t credits{}, last{}, replenished{}, throttled{};
    bool initialized{};
};
} // namespace Vulkan

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <stdexcept>

namespace VideoCore {

// AstraEH: A typed terminal error lets the frontend stop emulation without skipping a draw
// or treating unrelated driver/runtime failures as safe to recover from.
class ShaderRecoveryError final : public std::runtime_error {
public:
    ShaderRecoveryError() : std::runtime_error{"Uberhar native recovery shader unavailable"} {}
    // CodexAstraLocal: Reuse the contained terminal renderer error for strict
    // backend rejection while preserving the historical shader-failure message.
    explicit ShaderRecoveryError(const char* message) : std::runtime_error{message} {}
    // CodexAstraLocal: Preserve a normalized driver's signed result through the
    // terminal renderer boundary; zero remains the non-driver-error default.
    ShaderRecoveryError(const char* message, int result)
        : std::runtime_error{message}, driver_result{result} {}
    int DriverResult() const noexcept { return driver_result; }
private:
    int driver_result{};
};

} // namespace VideoCore

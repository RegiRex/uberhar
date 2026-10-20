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
};

} // namespace VideoCore

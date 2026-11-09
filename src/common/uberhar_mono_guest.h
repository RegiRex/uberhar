// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

namespace Common::MonoGuest {
// CodexAstraLocal: Guest-input selection is independent of eye suppression and
// never writes the real slider. Real stereo/depth always wins over the experiment.
constexpr float Depth(bool enabled, bool supported_mode, bool mono_left, unsigned real_percent) {
    return enabled && supported_mode && mono_left && real_percent == 0
               ? 0.01f : real_percent / 100.0f;
}
} // namespace Common::MonoGuest

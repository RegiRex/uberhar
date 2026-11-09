// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <cassert>
#include "common/uberhar_mono_guest.h"
int main() {
    // CodexAstraLocal: Exhaust all user slider values and independent gates;
    // the experiment cannot overwrite real stereo/depth or non-supported modes.
    for (unsigned depth = 0; depth <= 100; ++depth)
        for (int enabled = 0; enabled < 2; ++enabled)
            for (int mode = 0; mode < 2; ++mode)
                for (int mono = 0; mono < 2; ++mono)
                    assert(Common::MonoGuest::Depth(enabled, mode, mono, depth) ==
                        (enabled && mode && mono && !depth ? 0.01f : depth / 100.0f));
}

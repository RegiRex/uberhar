// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Exercise the production cache envelope against truncation/corruption
// and source/compiler mismatches. Full shader semantics remain a separate CI gate.
#include <cstdio>
#include <stdexcept>
#include "video_core/renderer_vulkan/uberhar_spirv_cache.h"
void Check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    using namespace Vulkan::UberharSpirvCache;
    const Key key{0x0123456789abcdef, 0x1122334455667788};
    // A framed instruction stream for testing storage, not an executable shader fixture.
    const std::vector<u32> code{0x07230203, 0x10000, 0, 8, 0, (2U << 16) | 17U, 1};
    const auto encoded = Encode(key, code);
    Check(Decode(key, encoded) == code, "cache round trip failed");
    for (std::size_t n = 0; n < encoded.size(); ++n)
        Check(Decode(key, std::span(encoded).first(n)).empty(), "truncated cache accepted");
    for (std::size_t n = 0; n < encoded.size(); ++n) {
        auto changed = encoded;
        changed[n] ^= 1;
        Check(Decode(key, changed).empty(), "corrupt cache accepted");
    }
    Check(Decode({key.source + 1, key.compiler}, encoded).empty(), "source change ignored");
    Check(Decode({key.source, key.compiler + 1}, encoded).empty(), "compiler change ignored");
    auto invalid = code;
    invalid[5] &= 65535;
    Check(Encode(key, invalid).empty(), "zero-length SPIR-V instruction accepted");
    invalid = code;
    invalid[5] |= 100U << 16;
    Check(Encode(key, invalid).empty(), "overrun SPIR-V instruction accepted");
    invalid.resize(MaxCodeWords + 1);
    Check(Encode(key, invalid).empty(), "oversized SPIR-V accepted");
    auto trailing = encoded;
    trailing.push_back(0);
    Check(Decode(key, trailing).empty(), "trailing bytes accepted");
    std::puts("PASS: SPIR-V cache round trip, every-word corruption/truncation, keys and bounds");
}

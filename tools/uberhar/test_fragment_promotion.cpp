// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraPro: Test production demand/identity policy independently of GPU drivers.
#include <array>
#include <cstdio>
#include <stdexcept>
#include "video_core/renderer_vulkan/uberhar_fragment_policy.h"
#include "video_core/shader/generator/pica_fs_config.h"
using namespace Vulkan::ReadyFragmentPolicy;
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    Pica::RegsInternal regs{};
    Pica::Shader::FSConfig a{regs};
    auto b = a;
    b.texture.tev_stages[0].ops_raw ^= 1;
    Require(!(a == b), "test configs must differ");
    DemandGate<Pica::Shader::FSConfig, 4> gate;
    for (u32 i = 1; i <= WarmupDraws; ++i)
        Require(gate.Observe(0x10, a) == (i >= WarmupDraws), "premature demand admission");
    // AstraPro: Even deliberate equal hashes cannot alias different configs.
    Require(!gate.Observe(0x10, b), "hash collision incorrectly admitted");
    for (u32 i = 2; i <= WarmupDraws; ++i)
        Require(gate.Observe(0x10, b) == (i >= WarmupDraws), "collision reset failed");
    Require(!gate.Observe(0x14, a), "occupied direct-map slot did not reset");
    Require(gate.Replacements() == 2, "replacement accounting");
    // AstraPro: Counts saturate; a hot key stays admitted across long sessions.
    for (u32 i = 2; i <= WarmupDraws; ++i) gate.Observe(0x14, a);
    for (u32 i = 0; i < 1000000; ++i)
        Require(gate.Observe(0x14, a), "hot count wrapped or lost equality");
    DemandGate<Pica::Shader::FSConfig, 4> fresh;
    Require(!fresh.Observe(0x14, a), "new title retained previous demand");
    std::size_t policy_cases = 0;
    for (std::size_t resident = 0; resident <= MaxModules + 4; ++resident) {
        for (bool busy : {false, true}) {
            Require(CanAdmit(resident, busy) == (resident < MaxModules && !busy), "budget rule");
            ++policy_cases;
        }
    }
    for (bool forced : {false, true}) {
        for (bool cacheable : {false, true}) {
            Require(PreferSpecialized(forced, cacheable) == (!forced && cacheable), "route rule");
            ++policy_cases;
        }
    }
    for (bool covered : {false, true}) {
        for (bool ready_gpu : {false, true}) {
            for (bool prefer : {false, true}) {
                Require(NeedsDynamicTransport(covered, ready_gpu, prefer) ==
                        (covered && !(ready_gpu && prefer)), "transport consumer rule");
                // A failed optional promotion cannot retain the GPU-only bypass.
                Require(NeedsDynamicTransport(covered, false, prefer) == covered,
                        "CPU retry lost its generic transport");
                ++policy_cases;
            }
        }
    }
    std::printf("PASS: %zu fragment admission/transport cases, full-config hash collision/slot eviction, "
                "title reset, 1000000 saturated demand hits\n", policy_cases);
}

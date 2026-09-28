// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Verify phase boundaries, uncertain evidence and concurrent read
// accounting.
#include "core/uberhar_frame_diagnostics.h"
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include <thread>

void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main() {
    using namespace Common::UberharActivity;
    Reset(1);
    Check(!SetManualPhase(1) && !SetManualPhase(99), "User cannot claim automatic startup");
    Check(Capture().GetPhase() == Phase::Unknown, "Default must remain unconfirmed");
    SetManualPhase(2);
    auto loading = Capture();
    SetStartup(true);
    Check(Capture().GetPhase() == Phase::Startup, "Frontend loading must override manual phase");
    SetStartup(false);
    Check(Capture().GetPhase() == Phase::Loading, "Startup completion lost annotation");
    SetManualPhase(3);
    SetManualPhase(2);
    Check(Between(loading, Capture()) == Phase::Mixed,
          "Round-trip transition incorrectly attributed");
    loading = Capture();
    SetManualPhase(2);
    Check(Between(loading, Capture()) == Phase::Loading, "No-op mark changed phase generation");
    Reset(2);
    Check(Between(loading, Capture()) == Phase::Mixed && ManualPhase() == 0,
          "New game retained annotation or inherited a wait");

    std::thread reader([] {
        for (int i = 0; i < 10'000; ++i)
            NoteRead(4096);
    });
    for (int i = 0; i < 10'000; ++i)
        SetManualPhase(i % 2 ? 2 : 3);
    reader.join();
    Check(Capture().reads == 10'000 && Capture().bytes == 40'960'000, "Concurrent reads lost");
    Check(Evidence(1, 2'000'000, 0, 1'000'000'000) == "loading_candidate",
          "Read/no-presentation evidence missing");
    Check(Evidence(1, 2'000'000, 60, 1'000'000'000) == "guest_read_activity",
          "Streaming gameplay falsely classified as loading");
    Check(Evidence(0, 0, 0, 1'000'000'000) == "no_submissions",
          "Idle without reads falsely classified as loading");

    Reset(3);
    Core::UberharFrameDiagnostics frames;
    frames.Observe(1, 0, 0, 0, 100, false, Capture());
    NoteRead(16);
    frames.Observe(20'000'001, 20'000, 1, 0, 100, false, Capture());
    SetManualPhase(2);
    frames.Observe(40'000'001, 40'000, 2, 0, 100, false, Capture());
    frames.Observe(100'000'001, 60'000, 2, 0, 100, false, Capture());
    Check(frames.Phases()[0].frames == 1 && frames.Phases()[2].frames == 1 &&
              frames.Phases()[4].frames == 1,
          "Phase bands lost or concealed crossing interval");
    Check(frames.Worst()[0].phase == Phase::Loading, "Retained hitch lost its phase");
    frames.BreakInterval();
    SetManualPhase(3);
    frames.Observe(60'100'000'001, 60'000, 2, 0, 100, false, Capture());
    frames.Observe(60'120'000'001, 80'000, 3, 0, 100, false, Capture());
    u64 count = 0, wall = 0;
    for (const auto& phase : frames.Phases()) {
        count += phase.frames;
        wall += phase.wall_ns;
    }
    Check(count == frames.Total().frames && wall == frames.Total().wall_ns && wall == 120'000'000,
          "Phase totals differ from all valid frames or include pause");
    Check(frames.Total().read_requests == 1 && frames.Total().requested_bytes == 16,
          "Read deltas counted more than once");
    // AstraEH: Longer loading hitches cannot displace retained gameplay evidence.
    Core::UberharFrameDiagnostics retained;
    Reset(4);
    SetManualPhase(2);
    u64 t = 1;
    retained.Observe(t, 0, 0, 0, 100, false, Capture());
    for (int i = 0; i < 16; ++i) {
        t += 500'000'000;
        retained.Observe(t, i + 1, 0, 0, 100, false, Capture());
    }
    SetManualPhase(3);
    retained.Observe(t += 20'000'000, 20, 1, 0, 100, false, Capture());
    retained.Observe(t += 60'000'000, 21, 2, 0, 100, false, Capture());
    Check(retained.Worst()[7].interval_ns == 500'000'000 &&
              retained.PhaseWorst()[3][0].interval_ns == 60'000'000,
          "Loading eclipsed the independent gameplay hitch record");
    std::puts("PASS: explicit phases, startup override, mixed boundaries, pause "
              "exclusion, I/O and concurrency");
}

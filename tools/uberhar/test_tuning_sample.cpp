// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <cassert>
#include <cmath>
#include <iostream>
#include "common/uberhar_tuning_sample.h"

// CodexAstraLocal: Exercise the same recorder used by Android with synthetic
// host/guest clocks; this detects polluted comparisons without requiring a game.
int main() {
    using Recorder = Common::UberharTuningSample;
    auto& recorder = Recorder::Instance();
    recorder.SetDriver("actual-driver");
    recorder.BeginRun(1, "title", "fixed-quality", 5, 2, true);
    assert(!recorder.Begin(0));
    assert(!recorder.Begin(7199));
    assert(recorder.Begin(360));
    assert(!recorder.Begin(360));
    recorder.Pause(); // Opening the menu before the marked scene is not gameplay.
    assert(recorder.Snapshot().status == Recorder::Status::Armed);
    for (int i = 0; i <= 360; ++i)
        recorder.Observe(1'000'000'000ULL + i * 20'000'000ULL, i * 16667LL, i / 2, 2, 400);
    const auto sample = recorder.Snapshot();
    assert(sample.status == Recorder::Status::Complete);
    assert(sample.frames == 360 && sample.submissions == 180);
    assert(std::abs(sample.mean - 83.335) < .001);
    assert(sample.low == sample.high && sample.p99_ms == 20);
    assert(!sample.pacing_limited);
    assert(recorder.Snapshot().frames == sample.frames); // Non-destructive reader.
    recorder.EndRun(1);
    assert(recorder.Snapshot().status == Recorder::Status::Complete);
    assert(!recorder.Begin(360));

    recorder.BeginRun(2, "title", "fixed-quality", 0, 2, true);
    assert(recorder.Begin(360));
    recorder.Observe(100, 10, 0, 2, 400);
    recorder.Pause();
    assert(recorder.Snapshot().status == Recorder::Status::Invalid);
    assert(recorder.Begin(360));
    recorder.Observe(100, 10, 0, 2, 400);
    recorder.Observe(200, 20, 1, 4, 400);
    assert(recorder.Snapshot().reason.find("Resolution") != std::string::npos);
    assert(recorder.Begin(360));
    recorder.Observe(100, 10, 0, 2, 400);
    recorder.Observe(200, 20, 1, 2, 100);
    assert(recorder.Snapshot().reason.find("cap") != std::string::npos);
    assert(recorder.Begin(360));
    recorder.Observe(100, 10, 0, 2, 400);
    recorder.Observe(200, 5, 1, 2, 400);
    assert(recorder.Snapshot().reason.find("discontinuity") != std::string::npos);
    assert(recorder.Begin(360));
    recorder.EndRun(2);
    assert(recorder.Snapshot().status == Recorder::Status::Invalid);

    recorder.BeginRun(3, "title", "fixed-quality", 0, 2, true);
    assert(recorder.Begin(360));
    for (int i = 0; i <= 360; ++i)
        recorder.Observe(1'000'000'000ULL + i * 16'667'000ULL, i * 16667LL, i, 2, 100);
    assert(recorder.Snapshot().pacing_limited);
    recorder.Cancel();
    assert(!recorder.Observing());
    assert(recorder.Snapshot().status == Recorder::Status::Idle);
    std::cout << "Native tuning sample: measurement, context, pause, cap, lifecycle checks passed\n";
}

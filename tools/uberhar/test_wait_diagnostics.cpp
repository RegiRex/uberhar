// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Validate histogram boundaries and bounded retention after the old
// first-20 limit, including concurrent snapshot reads during event recording.
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>
#include "video_core/renderer_vulkan/uberhar_wait_diagnostics.h"

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    using namespace Vulkan;
    PipelineWaitDiagnostics boundaries;
    boundaries.Record({.elapsed_ns = 1});
    for (u64 bound : PipelineWaitDiagnostics::UpperBounds) {
        boundaries.Record({.elapsed_ns = bound - 1});
        boundaries.Record({.elapsed_ns = bound});
    }
    const auto bins = boundaries.Histogram();
    for (std::size_t i = 0; i < bins.size(); ++i)
        Check(bins[i] == (i == 7 ? 1U : 2U), "Histogram boundary assigned incorrectly");

    PipelineWaitDiagnostics retained;
    for (u64 i = 1; i <= 40; ++i)
        retained.Record({.elapsed_ns = 50000000 + i, .draw = i});
    retained.Record({.elapsed_ns = 2000000000, .draw = 41});
    auto worst = retained.Worst();
    Check(worst[0].draw == 41 && worst[0].elapsed_ns == 2000000000, "Late worst event lost");
    for (std::size_t i = 1; i < worst.size(); ++i)
        Check(worst[i].draw == 41 - i, "Bounded worst retention/order incorrect");

    PipelineWaitDiagnostics concurrent;
    std::vector<std::jthread> writers;
    for (u64 worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&, worker] {
            for (u64 i = 0; i < 1000; ++i) {
                concurrent.Record({.elapsed_ns = 100000000 + worker * 1000 + i});
                (void)concurrent.Worst();
            }
        });
    }
    writers.clear();
    const auto counts = concurrent.Histogram();
    Check(std::accumulate(counts.begin(), counts.end(), u64{}) == 4000, "Wait events lost");
    worst = concurrent.Worst();
    for (std::size_t i = 0; i < worst.size(); ++i)
        Check(worst[i].elapsed_ns == 100003999 - i, "Concurrent retention incorrect");
    std::puts("PASS: wait histogram boundaries, late worst events and 4000 concurrent records");
}

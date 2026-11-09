// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <cassert>
#include <iostream>
#include "common/logging/frame_telemetry.h"
#include "core/uberhar_frame_diagnostics.h"
using namespace Common::FrameTelemetry;

int main() {
    // CodexAstraLocal: Compare actual production accumulators on identical endpoints;
    // one-second partitioning must not change the established five-second arithmetic.
    Accumulator a;
    Core::UberharFrameDiagnostics old;
    Context context{1, 7, 1, 2, 2, 3, 400, true};
    std::uint64_t now = 0;
    std::int64_t ticks = 0;
    a.Observe({now, 0, ticks}, 0, context);
    old.Observe(now, 0, 0, 0, 400, true);
    for (unsigned i = 1; i <= 501; ++i) {
        now += 10'000'000;
        ticks += 10'000; // Intentionally not a nominal frame's guest advance.
        a.Observe({now, i / 2, ticks}, 123, context);
        old.Observe(now, ticks * 1000000 / GuestTicksPerSecond, i / 2, 123, 400, true);
    }
    Record r;
    a.Finish(r, FiveSecondEnd);
    assert(r.section_count == 6 && r.sections[5].totals.wall_ns == 10'000'000);
    assert(r.totals.wall_ns == old.Total().wall_ns && r.totals.intervals == old.Total().frames);
    assert(r.totals.submissions == old.Total().game_frames && r.totals.work_ns == old.Total().work_ns);
    assert(r.totals.histogram == old.Total().intervals && r.totals.guest_ticks == 5'010'000);
    std::uint64_t wall = 0, guest = 0;
    for (unsigned i = 0; i < r.section_count; ++i) {
        wall += r.sections[i].totals.wall_ns;
        guest += r.sections[i].totals.guest_ticks;
        const auto& e = r.sections[i].extremes;
        assert(e.count == (i == 5 ? 1 : 6));
        assert(e.shortest[0].ordinal == 100 * i + 1 && e.longest[0].ordinal == 100 * i + 1);
    }
    assert(wall == r.totals.wall_ns && guest == r.totals.guest_ticks);
    assert(r.session_extremes.shortest[5].ordinal == 6); // Stable equal-duration ties.
    a.Break(Pause);
    now += 100'000'000'000;
    a.Observe({now, 251, ticks}, 0, context); // Re-anchor; never score menu/paused host delay.
    now += 2'500'000'000; ticks += 12345;
    a.Observe({now, 252, ticks}, 1, context);
    context.scene = 8; context.generation = 2; context.limit = 100;
    now += 10; ticks += 17;
    a.Observe({now, 253, ticks}, 1, context);
    now += 2; ticks += 1;
    a.Observe({now, 253, ticks}, 1, context);
    a.Observe({now - 1, 253, ticks - 1}, 1, context); // Discontinuity, not impossible speed.
    Record final;
    final.kind = Kind::Final;
    a.Finish(final, Final);
    assert(final.totals.intervals == 3 && final.excluded == 3 && final.discontinuities == 1);
    assert(final.sections[0].totals.wall_ns == 2'500'000'000 && final.sections[0].flags & Duration);
    assert(final.sections[1].flags & ContextBoundary && final.sections[1].flags & SceneBoundary);
    assert(final.session_extremes.shortest[0].DurationNs() == 2);
    assert(final.session_extremes.longest[0].DurationNs() == 2'500'000'000);
    assert(final.session_totals.wall_ns == r.totals.wall_ns + 2'500'000'012);
    // The canonical guest endpoints survive localization; do not replace them with nominal ticks.
    assert(final.session_extremes.shortest[0].end.guest_ticks -
           final.session_extremes.shortest[0].begin.guest_ticks == 1);

    Accumulator churn;
    churn.Observe({0, 0, 0}, 0, context);
    for (unsigned i = 1; i <= 40; ++i) {
        ++context.generation;
        churn.Observe({i * 1000, i, i}, 1, context);
    }
    Record overflow;
    churn.Finish(overflow, Final);
    assert(overflow.section_count == MaxSections && overflow.section_overflow == 24);
    assert(overflow.totals.intervals == 40 && overflow.overflow_intervals == 24);
    assert(overflow.overflow_wall_ns == 24'000 && overflow.overflow_guest_ticks == 24);

    Common::Log::FrameTelemetryChannel channel;
    for (unsigned i = 0; i < 16; ++i) assert(channel.Submit(r));
    assert(!channel.Submit(final)); // Required-channel exhaustion is explicit, never a wait.
    channel.Drain([](const Record&) { return true; });
    auto status = channel.Status();
    assert(status.written == 16 && status.queue_drops == 1 && status.final_written == 0);
    assert(channel.Submit(final));
    channel.Drain([](const Record&) { return false; });
    assert(channel.Status().write_failures == 1 && channel.Status().final_written == 0);
    assert(channel.Submit(final));
    channel.Drain([](const Record&) { return true; });
    assert(channel.Status().final_written == channel.Status().final_requested);
    std::cout << Common::Log::FormatFrameTelemetry(r) << Common::Log::FormatFrameTelemetry(final);
    std::cerr << "PASS: production sections, conservation, exact ticks, ties, pause, mixed contexts, "
                 "discontinuity, overflow, nonblocking queue and final delivery accounting\n";
}

# Advisory performance tuning

<!-- CodexAstraLocal: Owner-approved integration, corrected November 8 to suggest settings without changing them. -->

The alpha Testing panel's tuning assistant records a bounded comparison and
identifies promising per-game settings. **It does not change graphics settings,
drivers, resolution, saves or shader caches.** Recommendations describe the
measured scene and configuration, not a universal optimum. The owner manually
chooses settings and replays the scene.

## What the comparison controls

Each session holds the game, renderer, driver, resolution and other graphics
settings fixed. It compares the six launch-owned work controls individually or
in explicitly selected pairs. The default is one hypothesis. A larger search
must be selected explicitly; the engine caps the plan at twelve candidates.
There is no automatic factorial expansion.

1. Select a repeatable scene and record the cache class, device performance mode
   and other conditions. Keep those conditions consistent between launches.
2. Manually select the requested baseline profile and launch the game.
3. At the scene marker, start measurement from the Testing panel and resume.
   Keep the speed cap fixed throughout the sample. Use sufficient turbo headroom
   when measuring throughput; a capped sample cannot rank faster alternatives.
4. Collect the completed measurement and confirm the scene and visible output.
5. Repeat with the suggested candidate and then the baseline. The app checks
   actual native flags and configuration rather than trusting the proposed tuple.
6. Review the measured changes and recommendation. Subsequent comparisons may
   reuse the preceding baseline when its complete context and scene match.

The settings themselves remain launch-owned. This feature does not pretend that
changing a Kotlin preference during gameplay reconfigures a live renderer.

## Measurement and decisions

Native measurements are independent of the overlay's resettable statistics.
The default window contains 1,200 guest vblank intervals. Low and high values
are the slowest and fastest 120-interval bins; mean uses total guest time divided
by total observed wall time. These are **not historical five-wall-second lows**.
Frame-interval p95/p99 and submission counts remain available alongside speed.

Pauses during measurement, changes to resolution or speed cap, timeline
discontinuities, missing graphics submissions and mismatched effective settings
invalidate the trial. Arming from a paused menu begins on resume. A completed
sample remains readable after normal exit. Collect it before another game launch.
The finite measurement window does not stop external logs or owner capture.

The engine compares candidate B with nearby baseline A measurements before and
after it. It rejects excessive baseline drift rather than crediting a change in
thermal conditions as a feature gain. Initial decision thresholds are a 3%
useful gain, no more than 2% regression in either low or mean, and no more than
5% baseline drift. These are practical screening thresholds, not a claimed
statistical confidence interval. One inconclusive retry is permitted, not forced.

Pairs remain eligible when a single feature loses. A pair still needs evidence
against its individual parents before claiming an interaction benefit. The
engine does not combine unrelated speed gains or synthesize an unmeasured winner.
Recommendations retain the measured configuration and evidence for later review.

## Limits and delivery gates

Guest-frame alignment does not establish identical content across launches.
The user must choose the same scene and confirm comparable play and visible
correctness. Thermal, background-app, power and cold/warm conditions also require
confirmation; native settings checks do not establish those facts. Existing
full capture remains the source for GPU/CPU attribution and visual investigation.

This integration changes the testing workflow, not shader algorithms. It earns
its place by reducing manual comparison errors and producing actionable measured
recommendations. It does not by itself establish a performance gain or qualify
0.2.0. Release qualification still requires the established game coverage,
complete rendering and sustained-play performance criteria.

ARMSX2's controller/sampler/applier/store separation informed the design. This
implementation is original GPL-2.0-or-later code; no GPL-3.0 source was copied.
Its quality-reducing tuning levers were not adopted. The pinned source-review
ledger is retained in the private build058-in-app-tuning architecture evidence.

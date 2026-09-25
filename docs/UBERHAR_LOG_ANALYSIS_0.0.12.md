# Uberhar 0.0.12: Native cold/warm results on Thor

<!-- AstraEH: Measured device evidence, workload limits and the next optimization target. -->

Source: `uberhar_log_9_25_0526_FEA.txt`, 860 lines, revision
`42cb2e5aefa2ccd081fce2395a59d3378328615d`, diagnostics 9.
Fire Emblem Awakening, title `00040000000A0500`, AYN Thor / QCS8550 / Adreno 740,
Android API 33. Both complete sessions use Native, Vulkan, 2x, CPU shader JIT,
100% speed cap, generic TEV, disk cache and disabled optional SPIR-V optimization.
GPU vertex specialization and CPU bridge are off as prescribed by the profile.

The owner reports a large improvement and an additional ally-attack animation
that occurs only in some replays. Treat this as a successful device milestone,
not a perfectly matched benchmark or proof of a compilation-free first playthrough.

## Current pair

| Measurement | Cold, session 1 | Warm, session 2 |
| --- | ---: | ---: |
| Observed frame interval time | 83.793 s | 67.987 s |
| Emulation speed over those intervals | 90.218% | 99.760% |
| System frame rate | 53.978 Hz | 59.688 Hz |
| Guest game-submission rate, mixed scenes | 41.865 Hz | 46.568 Hz |
| Generic foreground waits | 8.120581 s | 0.089293 s |
| Largest individual generic wait | 148.477 ms | 3.373 ms |
| Separate scheduler waits | 96.935 ms | 0.779 ms |
| Largest observed system-frame interval | 806.541 ms | 69.365 ms |
| Intervals at least 50 ms | 23 | 6 |
| Intervals at least 500 ms | 10 | 0 |
| Generic families / pipelines | 67 / 114 | 37 / 80 |
| Generic module disk hits / compile misses | 1 / 66 | 37 / 0 |
| Generic frontend/module work | 1165.352 ms | 46.062 ms |
| Generic driver calls | 6919.651 ms | 39.026 ms |
| CPU vertex-stage wall time | 21491.307 ms | 19646.855 ms |
| Submitted inputs / actual VS invocations | 115631612 / 40635231 | 105412044 / 36991992 |
| CPU JIT programs / total compilation | 12 / 6.355 ms | 12 / 5.441 ms |
| Worst CPU JIT program compilation | 0.853 ms | 0.694 ms |
| Skipped draws / generic failures / rejected cache files / cache write failures | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |

Evidence anchors: settings/run headers at lines 29–60 and 142 / 581; cold final
records at 432–458; warm final records at 832–856. Cache deletion occurs before
the first launch and its driver cache is absent; the second launch loads it.
A cold module hit is possible when distinct family keys generate identical source
and reuse a file written earlier in that same run. It does not by itself show that
the cache clear failed. All 37 modules requested on the warm run were cache hits.

Foreground waits, scheduler waits, worker times, vertex-stage times and full-frame
intervals overlap. Do not sum these columns into a total stall figure. Frame-rate
figures count emulated system frames and guest submissions, not Android display
presentation. The initial anchoring interval is excluded from pacing statistics.
The approximately 1.26 s / 1.12 s pauses are the recorded waits at each run's exit;
their durations are excluded from observed frame-interval totals.

## Sustained battle slowdown has substantially improved

All thirteen complete five-second warm windows report 98.456–100.253% emulation
speed. Three consecutive windows with about 29.9 game submissions per second
report 100.050%, 99.954% and 99.999% speed. This supports the owner's observation
that animation proceeds at full emulated speed in the slower-FPS scene, rather
than inferring slowdown solely from a game-submission rate below 60.
There are no explicit battle-boundary markers, so this is not a frame-exact battle
segmentation or a measurement of both panels' synchronization.

The cold run spends several windows near full speed too, but has concentrated
compilation bursts. Its two worst full windows report 35.597% and 57.183% speed.
The successful result does not mean every first-encounter scene is smooth yet.

The established CPU JIT accounts for only about six milliseconds of compilation
over each whole run. Indexed reuse hits about 64.9% of inputs. CPU vertex-stage
wall time per million submitted inputs is 185.86 ms cold / 186.38 ms warm, versus
639.96 / 585.18 ms in 0.0.10's Native pair: approximately 71.0% / 68.1% lower.
This normalization describes observed workloads; vertex instruction mixes and
reuse can differ, so it is not a controlled CPU benchmark or an isolated measure
of the JIT's contribution. The stage includes setup, execution, geometry and
memory synchronization. The inherited JIT, vertex-cache index and removal of
optional frontend optimization were introduced together in 0.0.11.

## Comparison with 0.0.10 Native

Prior source: `uberhar_log_9_24_2157_FEA.txt`, revision `f948671`, first two
sessions; see [the eight-session analysis](UBERHAR_LOG_ANALYSIS_0.0.10.md).

| Generic foreground waiting | 0.0.10 Native, 1x | 0.0.12 Native, 2x |
| --- | ---: | ---: |
| Cold | 19.052045 s | 8.120581 s |
| Warm | 5.278862 s | 0.089293 s |

The recorded wait totals fell 57.4% cold and 98.3% warm. These are reductions in
that counter, not overall game speedups. The older warm run included a long
interruption; cold coverage differs and the new cold run contains additional
animation/state variation. Both warm Native runs report 37 canonical families
and 80 generic pipelines, making module-cache reuse particularly useful evidence.
The new pair's cold run requires 67 families, while its warm replay requires 37.
That establishes unequal state coverage, consistent with the owner's caveat;
the log cannot assign the additional 30 families or a precise pause cost to the
ally attack without a gameplay/state marker.

## Remaining hitches are dominated by new generic driver builds

The cold run performs 66 generic module compilations and reports 66 generic
driver calls above the slow-build threshold. Total generic driver time is 6.920 s,
compared with 1.165 s in frontend/module work. Matching counts suggest that first
encounters with fragment programs are important; the bounded details do not prove
a one-to-one mapping for every module and pipeline.

One retained frame provides direct temporal evidence of clustering:

- Run origin derived from `run end` minus lifetime: log time 56.599348 s.
- Observed frame 2947 spans log time 106.985361–107.753970 s: 768.609 ms.
- Lines 323–328 show six generic pipeline builds completing inside that interval.
- Their driver-call durations sum to 601.281 ms. The starts implied by those
  durations also fall inside the interval; the generic worker processes them serially.

This explains how a roughly 100 ms individual pipeline build can contribute to a
much larger frame hitch when several new pipelines are encountered together.
Do not add the 601 ms to the 769 ms: it is work occurring within the frame interval.
The largest 807 ms frame has no complete per-build breakdown because detail logging
is capped. Source inspection confirms that `BindPipeline` waits for a newly demanded
generic and that `GetTevFallback` builds it on the serial worker.

## Reference baseline and next investigation

Preserve **0.0.12 Native at 2x on this Awakening section** as the current reference
for a successful device result. Keep the cached CPU JIT, persistent generic
modules, exact indexed reuse and existing correctness recovery while pursuing
the remaining first-use work. Warm performance must remain an explicit regression
check when broadening generic shader behavior.

The next priority is reducing first-use fragment-program variation and preparing
reusable generic work before it is demanded. `MakeDynamicTevFamilyConfig` already
turns TEV, alpha/scissor/depth mapping, fog and compatible texture controls into
runtime state. Lighting structure, procedural behavior and some typed resources
still affect generated programs. Inspect those remaining sources before choosing
which controls to move into runtime data; current counters do not isolate them.
Numerical lighting/texture and native-state comparisons must precede broader use.
A larger generic shader can trade fewer compilations for more GPU execution cost.

Merely adding compiler threads cannot remove a first-demand wait when the renderer
has not yet discovered future work. Warming previously observed records benefits
replay but is not a solution for an unseen first playthrough. A complete generic
bank remains a separate design/coverage task, including native pipeline state.

Compute remains zero-coverage in this game. Cold rejection counts include depth
testing on 171335 of 197070 considered draws and blending on 160329, often together
with alpha, culling or scissor. Those are non-exclusive counts, not an additive
partition. They do not justify removing correctness checks. The Thor driver
still advertises neither graphics pipeline libraries nor shader objects, and the
existing dynamic-state workaround remains active. Native refinement has immediate
evidence; a broad compute renderer and GPU vertex interpreter remain longer-term
work. Screen synchronization and visual presentation follow the shader work.

Device health shows power saver off and no low-memory flag. Thermal status is 0
but headroom is unavailable; battery temperature stays at 21 C. These reports do
not establish an absence of throttling or GPU temperature. Native heap readings
drop to zero in the warm run and must not be interpreted as total process RAM or
proof that native resources were freed. Sensor/allocator reporting needs caution.

This results analysis does not reset the architecture-review cadence. The next
default full review remains after 0.0.13, allowed after 0.0.12–0.0.14.

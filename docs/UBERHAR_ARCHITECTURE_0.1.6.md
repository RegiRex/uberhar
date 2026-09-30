# Full architecture review: 0.1.6 -> 0.1.7

<!-- AstraEH: Owner-requested review of the actual tested implementation, not a version-based claim of maturity. -->

Date: September 29, 2026 (America/New_York). Examined revision:
`f97f67b416cdd369638dd9486b08aac793ba5c6a`, released 0.1.6.
Implementation selected here: 0.1.7, fused CPU vertex input and measured recovery.
Host validation, CI publication and device acceptance are separate states.

## Evidence and reconciliation

The cumulative Thor log contains twelve sessions. Its first six sessions were
already supplied in the preceding log: count them once, not as independent runs
or a second build. The added sessions comprise Native 1x, Compute 1x/2x,
Automatic 2x cold/warm, and Monster Hunter 4 Ultimate at Native 2x. Automatic is
the UI Combo/hybrid-mode experiment; it is distinct from the fragment HybridTEV
setting. All observations here are from the same source revision.

Retain the owner's scene notes: FEA ally attacks occurred in the two cold tests
and warm 4x; warm 2x had an ally defense. Those are coverage observations, not
identical animation workloads. MH4U's early section was menus/character creation;
the long final fast-forward section was its intro. Do not request repeating
character creation. Logged phases remain Unknown: the intro annotation comes
from the owner, not an automatic scene detector.

| Session / mode | Resolution | Application cache evidence | Normal band | Fast band / observed time |
| --- | --- | --- | ---: | ---: |
| Sonic / Native | 2x | Present files, cold generic encounters | 68.323% | 315.889% / 15.39 s |
| FEA / Native | 2x | Empty, cold | 98.292% | 331.009% / 11.77 s |
| FEA / Native | 2x | Warm generic reuse | 99.912% | 326.275% / 9.62 s |
| FEA / Native | 4x | Empty, cold | 90.683% | 144.638% / 20.03 s |
| FEA / Native | 4x | Warm generic reuse | 91.682% | 140.078% / 18.76 s |
| LEGO City / Native | 2x | See raw inventory; do not infer driver warmth | 99.733% | 315.605% / 11.98 s |
| FEA / Native | 1x | Warm generic reuse | 99.989% | 375.168% / 32.30 s |
| FEA / Compute | 1x | Empty, cold | 99.054% | Not tested |
| FEA / Compute | 2x | Warm generic reuse | 100.389% | 315.298% / 35.45 s |
| FEA / Automatic | 2x | Empty, cold | 98.963% | Not tested |
| FEA / Automatic | 2x | Warm generic reuse | 99.489% | 307.498% / 33.10 s |
| MH4U / Native | 2x | Empty at start; mixed generic reuse later | 99.859% | 87.785% / 706.84 s |

All fast bands above commanded 400%, including the historical 4x runs. They are
phase-mixed observations, not certified gameplay floors. Early Sonic/LEGO bursts
must not be presented as sustained racing/open-world performance. Use the
per-band observed duration, which excludes recognized pauses; do not sum
cumulative pause counters from repeated progress records. Exclude mixed
100/400 windows from a pure-speed-window comparison, not from the archive.

MH4U has 140 complete constant-400 five-second windows: approximately 87.702%
wall-weighted speed, with means spanning 34.969%-244.681%. These are window means,
not individual-frame minima or 1% lows. Its full fast band reports 706.782 s of
work over 706.835 s observed. This exposes a sustained limit; it does not identify
thermal throttling or GPU utilization. Moving the aggregate 87.785% to 400%
would require about 4.56 times the throughput for that same workload. That is a
gap calculation, not a forecast or proof every scene scales equally.

## Corrections to the prior conversational interpretation

1. **Compute is an exact solid-rectangle subset, not a complete 3D renderer.**
   `vk_compute_rect.cpp`, `vk_rasterizer.cpp` and `uberhar_test_profile.h` show that
   unsupported work retains native rasterization and CPU vertices. Every session
   reports zero eligible rectangles and zero compute draws. Compute 2x FEA routes
   all 435623 draws natively; Automatic 2x warm routes all 306968 natively.
   Thus 315.298% versus 307.498% is not a compute-versus-native speed comparison.
   Reported GPU sample counts are also zero: they do not profile general GPU cost.
2. **MH4U is largely on specialized fragment recovery:** 15137298 of 19613982
   draws (77.176%). Generic covers 4476684. Recovery preserves rendering; these
   are not missing draws. The old log cannot identify every blocking reason.
   Generic-only preparation/shader tuning therefore misses much of this workload.
3. **Cold first use is not finished.** FEA Native 2x cold has 607.659 ms total
   generic waiting, a largest generic wait of 166.308 ms, and a maximum normal
   system-frame interval of 389.847 ms. Automatic cold has a 398.530 ms maximum
   interval. With Unknown phases, neither hide these nor claim they all occurred
   in controllable gameplay. Gameplay-versus-loading attribution is still needed.
4. **Low total compilation cannot explain all sustained slowdown.** MH4U records
   517.452 ms generic waiting, 673.451 ms scheduler pipeline waiting, 1943.798 ms
   specialized driver builds, and 282.535 ms CPU vertex-program compilation over
   its whole session. Build/wait counters overlap and must not be added as a
   serial frame budget. Persistent execution/command costs remain important.
5. **Do not convert submissions to hardware frame-rate claims.** Emulated speed,
   system ticks, game submissions and physical panel presentations are different.
   Software frame counters here explicitly have `display_timing=false`.

## Architecture alternatives and decision

Retain Native as the reference and optimize the shared CPU vertex transport.
MH4U processes 3.593 billion input indices and 1.908 billion actual CPU vertex
shader invocations across its whole mixed session. The existing no-geometry
Native branch handles 3.572 billion of those input indices, despite accounting
for only 9.022 million of 19.614 million batches. Numerous geometry-path batches
are small. That makes per-vertex transport a plausible broadly relevant target,
not proof that JIT arithmetic or GPU shading has ceased to matter.

**Selected for 0.1.7:** resolve safe memory spans and decode format/register
transport once per admitted no-GS batch, then convert attributes directly into
shader input registers. Avoid a temporary AttributeBuffer plus a second copy and
repeated physical mapping on every vertex invocation. Keep live memory/default
reads, ascending alias writes, exact f24 conversion, and the existing within-draw
FIFO/output reuse. Pin references only for the current batch. Decline uncertain
layouts, short mappings and wraparound before executing the new path. Existing
geometry, debug and unsupported paths remain unchanged.

Also check the unchanged fragment-support predicate before constructing dynamic
TEV transport that unsupported draws never consume. Add exclusive recovery-reason
and fused-input admission counters. This is an actual hot-path change plus the
missing evidence for the next one; it does not broaden shader arithmetic support.

**Alternatives kept open:** a ready GPU vertex route with accurate CPU fallback;
a broader correctly validated generic fragment path; command/descriptor batching;
true loading-time warm-up with bounded queues; a general compute/vertex interpreter.
A full compute renderer is not selected without useful workload coverage and
numerical/pixel tests. Do not remove the existing AddSigned/shadow/LUT guards merely
to make generic coverage green. Do not implement cross-draw transformed-vertex
caching without complete memory/uniform invalidation. Do not fix FEA ghosting with
a global sampling offset on the strength of resolution dependence alone.

**Decision after 0.1.7 evidence:** retain fused input only if paired playback
supports it without regressions. Check the actual admission fraction and slow
window stage costs. If input transport becomes cheap but sustained throughput
remains limited, move to ready GPU vertices or command work instead of repeatedly
micro-optimizing the same loop. Use recovery reasons to select the next exact
fragment extension. Missing GPU timing is still an attribution gap, not permission
to invent GPU utilization from host duration.

## Roadmap and version gates

Keep `release.beta.alpha` numbering. 0.1.7 is an experiment within the current
beta; 0.2.0 is earned by the throughput architecture gate, not by enthusiasm or
number of changed lines. Minor milestones are overlapping acceptance checkpoints,
not nine mandatory sequential rewrites. Accuracy, cold-first-use work and device
coverage run continuously. This review proposes the following refined gates:

| Milestone | Primary acceptance evidence |
| --- | --- |
| 0.2 | Measurably better demanding matched scenes, exact output, retained 2x FEA baseline; selected CPU/GPU/command architecture supported by profiling. |
| 0.3 | Broad game coverage and honest fallback reasons; FEA, Sonic, LEGO, MH4U and selected additional titles, not a single-game success story. |
| 0.4 | Touch input/presentation timestamps and matching dual-screen frame IDs; no latency or pacing regression, with physical tests where available. |
| 0.5 | Per-scene 2x/400% and 4x/200% qualification report, including slow-window floors, cold/warm distinction and unqualified titles. Targets remain open where unmet. |
| 0.6 | Bounded and measured first-use work; cold/warm gameplay converge without dropped draws, invisible effects or unlimited prewarming memory. |
| 0.7 | Accuracy/crash/save/load/pause/surface-loss regression matrix; recovery paths tested, not hidden. |
| 0.8 | Resolution-aware correctness investigation, beginning with FEA; 1x/2x/4x and upstream/hardware comparisons, no unconditional ghosting hack. |
| 0.9 | Release candidates: feature freeze, repeatable tests, documented defaults/limits and reproducible signed delivery. |
| 1.0 | Stable declared renderer scope: correctness, cold behavior, pacing, recovery and ordinary-speed reliability across a named suite/device tier. Explicit per-title headroom results. |

The owner's 400%/200% ambitions are **not lowered**. Separating a stable renderer
release from an all-games headroom guarantee is a proposed scope clarification,
not silently declaring performance goals complete. If 1.0 must certify those
numbers in every torture-test scene, record that stricter gate explicitly; present
data cannot support a fast release-date promise. No milestone may be waived by
averaging demanding scenes together with menus. Hardware coverage is continuous,
not a prerequisite to purchasing devices or a dedicated replacement for 0.8.
Retroid Duo qualification remains unmeasured; Thor margin does not transfer by a
single percentage across different CPU/GPU/driver limits. Full general compute,
automatic compatibility profiles, static recompilation and power optimization are
separate later scope unless subsequently promoted by the owner.

## Measurement and next device test

Use a fixed named scene, stable effective cap, exact build/driver/settings and
cache-start/reuse evidence. Keep a normal-speed correctness run in addition to
headroom stress. Do not repeatedly toggle speed inside the measured scene.
Report wall-weighted speed, lowest one-second and five-second intervals when
available, frame-time tails from actual per-frame samples (not five-second means),
compilation waits, path counts and explicit pauses. Existing five-second bins do
not yield exact frame percentiles. Add finer/cross-thread CPU/GPU/input timestamps
incrementally, measuring diagnostic overhead against disabled logging.

For 0.1.7: Native FEA cold 2x normal plus warm 2x fixed400 battle; FEA 4x normal
plus warm fixed400 capacity test or fixed200 usability test (label which); Sonic
2x repeatable race with the chosen cap held steady; MH4U 2x the same intro at400.
Retain current caches for warm tests; clear only the application's named cache
for cold tests, never saves. Do not repeat MH4U character creation. Alternate
FEA modes need not be repeated until eligible compute coverage changes. Broader
LEGO/Smash effects checks and pause/resume/title changes validate retained output.

Host scalar/bitwise/model tests cannot certify Thor speed, correct game images,
real display latency or battery savings. Existing shader-pixel/Vulkan, Android,
package and signing gates stay mandatory. Device acceptance remains pending.

## Review cadence

This completes an early full review after three successors to the 0.1.3 review,
justified by the owner's request and MH4U/mode-coverage evidence. Count 0.1.7 as
successor one. Default next full review after four successors, allowed after
three to five, before starting a sixth. Minor version promotion does not reset
the counter. Earlier ledgers and roadmap scope/history remain preserved.

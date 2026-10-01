# Uberhar 0.1.9 — full architecture review and 0.1.10 source candidate

<!-- AstraPro: October 1, 2026. Measured evidence, implementation and publication are separate. -->

## Status and decision

Reviewed released source `1bce59f62b595da60f7f386e654bffbdeea5c876` and all three newly supplied exports. The current connection exposes GitHub reads but not commit/push/workflow actions; the local environment has no authenticated GitHub CLI. **0.1.10 is a local source candidate, not a published commit, CI run, or installable APK.** No branch, tag, workflow, signing material or release was changed remotely. A hash-checked patch, changed-file manifest, validation logs and guarded apply/publish helper accompany this review.

The architectural decision is to retain Native as the CPU-vertex control and keep Combo opt-in, but stop requiring a generic fragment shader for every covered GPU-vertex draw. Implement bounded, ready-only specialization using the inherited GLSL fragment generator, complete CPU/generic fallback on any miss, and cheap preflight before speculative vertex uploads. Do not widen strip/fan or guest-geometry admission in this iteration. GPU participation without lower total scene cost is not success.

This is an early full review after the third published successor of 0.1.6, justified by substantial GPU coverage without improvement in Dark Moon and mixed high-resolution results. It rechecks code, device evidence, alternatives, resource lifetime, correctness and acceptance criteria. The public review ledger remains unchanged until this patch is applied/published. No milestone is promoted to 0.2 or 1.0.

## Evidence and extraction

The 21:09 shorter export is an **exact byte prefix** of the longer 21:09 export, not another run. Use the 21:00 process as A1–A4 and the longer 21:09 process as B1–B10: **14 completed sessions, six titles, two distinct application launches**. All identify 0.1.9 / `1bce59f`. Input sizes and SHA-256 hashes are in `input_provenance.json`.

Many sessions change resolution while running. Their final `resolution` field does not retroactively describe every interval or their lifetime normal/fast bands. The table below therefore uses complete frame windows, paired to their same-time limit records, and separates renderer, resolution and effective limit. Exclude mixed-limit windows and the first window observing a settings change. Retain original session totals in the JSON. Do not add cumulative progress values to final totals. Already excluded pauses remain excluded; unmarked inactivity and game phases are not guessed.

There are 936 qualifying windows, covering 4,722.461 observed seconds in 31 groups. Weight means by `observed_wall_ms`. The minima/maxima are **five-second-window mean speeds**, not individual frame lows, 1% lows or display FPS. Short fragments of normal/fast bands do not qualify a game. Settings are periodic observations in 0.1.9, so this conservative segmentation cannot identify every unobserved within-window change. The new candidate adds frame-end setting ranges for future logs.

Scene labels remain Unknown. Different durations, routes, game progression, cache contents and host operating conditions prevent clean causal speedup percentages. Logs cannot establish image equivalence. Draw counts and submitted input slots are not pixel weighting, actual GPU shader invocation counts or GPU time. CPU-stage wall spans include any descheduling or stalls inside that stage.

## Split-window results

| Session | Game | Mode | Scale | Limit | Windows | Observed seconds | Mean speed | Window mean min–max | Seconds in windows <95% |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| A1 | FEA | Native | 2× | 400% | 1 | 5.006 | 332.231% | 332.231–332.231% | 0.000 |
| A1 | FEA | Native | 2× | 100% | 7 | 35.105 | 99.887% | 99.044–100.246% | 0.000 |
| A2 | FEA | Native | 4× | 400% | 13 | 65.186 | 108.945% | 77.184–173.631% | 25.083 |
| A3 | FEA | Combo | 4× | 100% | 2 | 10.017 | 85.927% | 74.044–97.822% | 5.011 |
| A3 | FEA | Combo | 4× | 400% | 12 | 60.152 | 107.392% | 62.559–228.058% | 25.094 |
| A4 | LEGO City | Combo | 4× | 100% | 4 | 20.024 | 98.743% | 97.107–99.914% | 0.000 |
| A4 | LEGO City | Combo | 4× | 400% | 21 | 105.157 | 233.372% | 138.446–307.789% | 0.000 |
| A4 | LEGO City | Combo | 2× | 400% | 6 | 30.031 | 399.267% | 398.434–400.011% | 0.000 |
| B1 | LEGO City | Native | 4× | 400% | 37 | 185.219 | 233.083% | 99.414–360.688% | 0.000 |
| B1 | LEGO City | Native | 4× | 100% | 1 | 5.016 | 99.955% | 99.955–99.955% | 0.000 |
| B1 | LEGO City | Native | 2× | 400% | 12 | 60.078 | 295.251% | 232.049–383.309% | 0.000 |
| B2 | Dark Moon | Native | 2× | 100% | 34 | 170.961 | 30.727% | 21.560–92.723% | 170.961 |
| B2 | Dark Moon | Native | 4× | 400% | 64 | 326.413 | 9.852% | 3.880–29.036% | 326.413 |
| B3 | Dark Moon | Combo | 2× | 100% | 135 | 690.495 | 8.869% | 5.309–92.584% | 690.495 |
| B3 | Dark Moon | Combo | 4× | 100% | 27 | 137.505 | 10.271% | 5.253–16.350% | 137.505 |
| B4 | Dark Moon | Native | 2× | 100% | 32 | 161.027 | 31.387% | 23.015–92.159% | 161.027 |
| B4 | Dark Moon | Native | 4× | 100% | 68 | 347.359 | 9.748% | 3.933–29.453% | 347.359 |
| B5 | Kid Icarus | Native | 2× | 100% | 75 | 376.614 | 81.013% | 51.564–100.103% | 236.219 |
| B5 | Kid Icarus | Native | 2× | 400% | 3 | 15.083 | 152.478% | 44.874–253.806% | 5.065 |
| B6 | Kid Icarus | Combo | 2× | 100% | 56 | 281.021 | 89.260% | 67.213–100.083% | 200.823 |
| B6 | Kid Icarus | Combo | 2× | 400% | 6 | 30.028 | 204.776% | 172.124–238.893% | 0.000 |
| B7 | MH4U | Native | 2× | 100% | 23 | 115.211 | 81.297% | 40.851–100.215% | 90.154 |
| B7 | MH4U | Native | 2× | 400% | 26 | 130.458 | 73.474% | 37.734–137.883% | 90.399 |
| B7 | MH4U | Native | 4× | 100% | 68 | 341.230 | 51.082% | 40.542–59.936% | 341.230 |
| B8 | MH4U | Combo | 2× | 400% | 49 | 245.575 | 80.467% | 38.101–204.164% | 190.503 |
| B8 | MH4U | Combo | 4× | 400% | 61 | 305.979 | 50.576% | 39.590–108.258% | 300.977 |
| B9 | Sonic | Native | 2× | 400% | 2 | 10.003 | 365.587% | 345.281–385.884% | 0.000 |
| B9 | Sonic | Native | 2× | 100% | 23 | 115.301 | 66.463% | 54.057–87.168% | 115.301 |
| B9 | Sonic | Native | 4× | 100% | 24 | 120.514 | 41.661% | 28.726–76.734% | 120.514 |
| B10 | Sonic | Combo | 2× | 400% | 24 | 120.298 | 99.145% | 68.294–377.670% | 105.278 |
| B10 | Sonic | Combo | 4× | 400% | 20 | 100.396 | 36.492% | 29.384–51.779% | 100.396 |

### Fire Emblem: control survives; 4× headroom still unqualified

A1's final normal band is 99.612% over 38.793 seconds. Its seven pure normal windows average 99.887%; mixed-speed startup work is not silently called a perfect cold start. Only one pure accelerated 2× window qualifies. At 4×, Native's 13 fixed400 windows average 108.945% (minimum 77.184%), versus Combo's 12 windows at 107.392% (minimum 62.559%). Those different scene samples do not establish an improvement. The generic cold-wait issue remains active.

Combo A3 really promotes 69,446 batches; CPU vertex-stage time falls to 1.720 seconds in that run, but total speed does not improve proportionately. Do not subtract unequal-run stage totals to claim a measured gain. PICA route totals are at physical lines 1164 and 1823 of the 21:00 file; final normal/fast bands remain available in the evidence JSON.

### LEGO: a meaningful local success, not universal qualification

Combo A4's late 2× portion has six complete fixed400 windows totaling 30.031 seconds, averaging **399.267%**, range 398.434–400.011%. The individual frame-window source lines are 2796, 2825, 2854, 2883, 2912 and 2942 in the 21:00 file. This establishes sustained limiter saturation for that recorded section, not every scene or thermal state. Native B1's separate 2× section averages 295.251% over 60.078 seconds; it is not a matched-scene causal speedup test.

At 4× the recorded fixed400 means are almost equal: Combo 233.372% and Native 233.083%. Both fluctuate, and Combo's minimum window is 138.446%, so a stable 200% floor is not certified. A4 promotes 927,978 batches, representing 98.959% of submitted input slots but 57.241% of batches. The latter distinction explains why draw percentage alone understates vertex coverage. There are 89 optional GPU pipelines and 1.998 seconds of recorded successful driver creation time.

### Dark Moon: treat the 2× result as a serious performance-regression signal

The two Native 2× normal-window samples average 30.727% and 31.387%. Combo's 2× sample averages **8.869%** across 690.495 observed seconds. Native 4× samples are near 9.8%; Combo's 4× sample is 10.271%. Native and Combo did not execute time-aligned scenes, so these are not controlled ratios, and 4× appearing slightly faster than 2× is not evidence that upscaling helps.

The 0.1.9 admission change works: Combo promotes **1,563,863 draws**, 56.785% of batches and 92.789% of submitted input slots. CPU vertex-stage wall time totals 17.235 seconds in Combo. Yet whole-scene execution remains unacceptable. The next task is not simply admitting more vertices.

There are 55 optional GPU pipelines with **79.278 seconds of accumulated successful driver creation time**, maximum **3.331 seconds** for one build. This work runs on a worker; it is not 79 seconds of directly measured foreground stalls and must not be added to CPU wait totals. Compilation is not the only remaining problem: the 2× window at log second 977.783 averages 7.392%, while the bracketing progress records at 972.142 and 977.787 retain 33 completed pipelines, unchanged driver-time/dependency/deferred counters, and increasing GPU selections. Physical source lines 5418, 5447 and 5450 in the longer file establish that observation. It does not identify GPU execution time or exclude every other kind of work.

The combination of high promotion, expensive driver builds and recurring poor throughput makes covered GPU fragment specialization a justified experiment. It does **not** prove that generic fragments, rather than fixed geometry fixup, shader precision implementation, uploads, driver synchronization or other GPU work, are the sole cause. Keep Native as the operational fallback until the new route is demonstrated correct and faster.

### Kid Icarus: more coverage, expensive preparation, still confounded

Pure normal windows average 81.013% Native and 89.260% Combo. Combo has 30.028 seconds of pure fixed400 windows at 204.776%; Native's 15.083-second accelerated subset mixes materially different work and ranges from 44.874% to 253.806%. Do not call the difference a measured global speedup.

Combo promotes 488,980 draws and 95.101% of input slots. It builds 229 optional GPU pipelines, totaling 39.207 seconds of successful driver time, maximum 879.102 ms. It does not hit the 256-pipeline cap. Raising that cap without evidence is not the immediate fix. This title remains valuable for shader-cost and visual regression coverage, but the log does not measure physical touchscreen latency.

### MH4U: a distinct coverage problem remains

Native 2× normal windows average 81.297%; its fixed400 subset averages 73.474%. Combo fixed400 windows average 80.467% at 2× and 50.576% at 4×. Native's 4× normal subset averages 51.082%. The scene/limit mixtures differ.

Combo records **zero GPU attempts and zero promoted draws**. The exclusive reasons are 4,993,601 strip-topology batches and 2,544,044 guest-geometry-shader batches. The initial no-GS Shader-list expansion cannot handle either class. This is neither a fragment-cache-cap problem nor evidence that the GPU executed MH4U badly. CPU vertex processing remains substantial (340.906 seconds in its Combo run).

A future extension needs explicit strip/fan continuation, winding and output-tail preservation across mixed GPU/CPU decisions. Copying the existing triangle-list predicate to strips would not establish that invariant. Guest geometry work requires a separate design and validation path. This candidate does not claim a direct MH4U throughput improvement.

### Sonic: the average hides the remaining slowdown

Native normal windows average 66.463% at 2× and 41.661% at 4×. Combo fixed400 windows average 99.145% at 2×, but **21 of 24 windows are below 95%**; the 377.670% peak is not a performance floor. At 4×, Combo averages 36.492%, range 29.384–51.779%. Not full speed.

Combo promotes 3,907,088 draws and 83.892% of input slots. Its 44 optional pipelines total only 1.241 seconds of recorded driver creation, so pipeline compilation is not a sufficient explanation for sustained slowdown. There are 7,965,251 small batches left on the CPU; lowering the threshold without measuring setup cost could make performance worse. CPU-stage time falls in the new route, but neither that nor a higher average qualifies the title.

## Device and correctness limits

The health snapshots report thermal status zero, unknown thermal headroom/GPU clocks and unobserved host Performance-mode selection. CPU current/max readings differ across parts of the sequence. These are context, not proof of thermal causality or controlled operating conditions. The large gap between B3 and B4 is not an emulated-frame stall; the later Native rerun began after a different host/device interval. Do not invent user activity during the gap.

The logs record no failed optional GPU draw attempts, pipeline-identity mismatches or reached optional caps in the completed GPU totals. That validates those counters, not exhaustive image correctness. Compare lighting, geometry, animation, framebuffer effects and lifecycle behavior explicitly. FEA ghosting and physical dual-screen latency are not addressed by this change.

## Source-level review and alternatives

<!-- AstraPro: Re-evaluate the architecture, not just the test names or the version label. -->

The reviewed execution chain is PICA admission → optional vertex analysis/upload → inherited programmable VS/fixed GS → fragment selection → exact-state ready GPU pipeline → ordered submission. The full CPU draw remains the fallback. 0.1.9 forced generic fragments even for covered ready-GPU draws, while shaders requiring correctness recovery retained the specialized implementation. The profile, readiness checks, framebuffer cancellation, command order, shader ownership and cache-drain paths were rechecked in the source.

| Alternative | Decision and reason |
|---|---|
| Retain generic GPU fragments everywhere and only widen admission | Rejected as the next step. Dark Moon now has high coverage with worse 2× behavior; Sonic/FEA 4× also lack a clear benefit. |
| Bounded ready specialization for covered GPU fragments | Selected as an opt-in experiment. Reuse inherited exact generator; preserve CPU/generic fallback; add admission/cost evidence and do not silently widen accuracy support. |
| Unbounded background precompile of every raw fragment configuration | Rejected. Variant growth, driver contention and memory pressure could dominate. Use repeated demand, a fixed module cap and one optional fragment job at a time. |
| Raise all GPU program/pipeline limits | Deferred. The completed totals show no cap rejection. Count actual demand before changing budgets. |
| Lower the 96-input GPU threshold | Deferred. Small batches dominate draw count in Sonic but not input slots; upload/bind cost can exceed saved CPU work. |
| Admit strips/fans immediately | Deferred until CPU assembler tail/winding and GPU-to-CPU continuation have an exact design. MH4U is a separate blocker, not a reason to remove its guard. |
| Full compute rasterization or universal GPU PICA interpreter | Research alternative, not a proven fast replacement. This device set lacks a comparative implementation/timing basis. |
| Vulkan extension/workaround changes | Rejected without targeted driver validation. The logged Qualcomm restrictions and unavailable fast-linking capabilities are not overridden. |
| Change guest CPU clock, drop draws, reduce accuracy or change resolution automatically | Rejected. Those would invalidate the intended correctness/performance comparison. |
| GPU timestamp/command-buffer profiling | Valuable next step if specialization still fails. Host attempt samples cannot replace GPU timestamps, but they distinguish speculative host work from the older CPU-only stage metric. |

## 0.1.10 implementation

1. **Bounded optional specialized fragments.** Combo's effective preset no longer forces generic fragments for ready GPU-vertex draws. Use the inherited GLSL specialized generator for supported/cacheable configurations. The CPU fallback remains the existing generic route; mandatory specialized recovery and its support guards remain unchanged. Native/Compute remain forced-generic CPU-vertex controls.
2. **Demand and readiness limits.** At most 128 optional fragment modules, 64 direct-mapped demand slots, 16 repeated matching lookups before new admission, and one optional fragment compilation in flight. Full FS configuration plus immutable profile equality protects hash reuse. An explicit compiler overload carries the captured optimizer choice into the worker instead of rereading the mutable setting; legacy callers retain their behavior. Pending, failed, capped or mismatched resources preserve CPU rendering. Failed optional entries do not poison the separate mandatory cache.
3. **Preflight and unused work.** Check optional fragment readiness before vertex scans/uploads; a failed preflight changes no framebuffer ownership. Recheck synchronized final state before selection. A specialized-only GPU attempt skips preparation of unused generic TEV transport; a failed attempt rebuilds complete CPU transport on retry. Ready-cache hits compare snapshots without copying a full profile each time.
4. **Recoverable optional driver errors.** Optional specialized GPU pipelines publish failure for CPU recovery just as optional generic ones do. The diagnostic shader-kind label is no longer overloaded as the only recovery control. Existing mandatory behavior is unchanged.
5. **Bounded diagnostics.** New module/admission/cost counters, optimized-GPU draw and transport-bypass counts, and host attempt samples once per 1,024 actual GPU attempts. Host spans may include backpressure and do not measure GPU shader execution. Sampling may alias draw patterns; do not extrapolate them into a full GPU budget.
6. **Frame-end context.** New per-window mode/resolution min/max, context-change and unknown counts capture both sampled endpoints of an interval. They retain raw timing, distinguish automatic resolution zero from unknown, and respect pauses/resets. These are setting samples, not physical surface/scanout measurements, and cannot see a change-and-revert wholly between samples.
7. **Validation harness reliability.** The full fragment pixel oracle now bounds resident program/VAO objects at 16, explicitly releasing evicted objects. Shader sources, all 1,056 cases, tolerances and comparison logic are unchanged. This prevents the earlier unbounded test-object cache from being necessary to complete validation.

All new logical sections and logging notes use AstraPro. Historical AstraEH and upstream attribution is retained. No new external dependencies, runtime network requests, telemetry uploads, permissions, device power settings, driver workarounds, signing changes or workflow shortcuts are added.

## Acceptance and stop criteria

The candidate is an experiment, not a fastest-path selector or a guaranteed improvement. Qualify it only when matched scenes are visibly correct, `optimized_gpu_draws`/ready-specialized hits show real use, and total scene speed or slow-window exposure improves. Keep CPU recovery intact; a higher GPU count without lower total cost is a failed performance hypothesis. If specialization produces visual errors, restore Native and preserve the offending log/screenshots before broadening coverage.

For Dark Moon, first seek a material improvement at 2× with correct output. Do not extrapolate an unmeasured speed multiplier to 100% or 4×. For LEGO, protect the already observed near-400% 2× section. For Sonic, raise the repeated sub-100% windows rather than menu-like peaks. MH4U remains an explicit, separate coverage milestone.

The functional goal remains sustained 100% in named MH4U/Sonic/Dark Moon scenes at 2×, then 4×, with correct output and acceptable pacing. Preserve separate 400% at 2× and ≥200% at 4× headroom targets. Stable Dark Moon at 4× would qualify a demanding Thor workload, not every game/device or an absolute mathematical optimum. 0.8 resolution-aware visuals and 0.4 latency remain unchanged; hardware coverage continues alongside them.

## Next tests once an APK passes all existing gates

See `TEST_CARD_0.1.10.md`. No additional 0.1.9 marathon is needed. Prioritize FEA Native 2×, a short fixed-resolution Dark Moon Native/Combo comparison, and LEGO's known Combo headroom section. A short Sonic comparison is useful next. Do not replay MH4U's intro/character creation for this fragment-only change; no observed MH4U draw reaches the altered path. Avoid live scale changes inside a measured section. Stop a visibly incorrect or extremely slow test rather than requiring the whole cutscene.

## Validation and delivery

See the accompanying `VALIDATION.md` for final local results and precise limits. Host tests, syntax checks, Vulkan module validation, and Mesa pixel execution are distinct from an Android link, CI signing/publication or Thor pixel parity. No 0.1.10 APK or device-speed claim is made. The apply/publish helper requires an explicitly authenticated clean clone at the pinned 0.1.9 parent; its publication option is not executed here and does not bypass CI gates.

## Follow-up owner scene context — October 1, 2026

<!-- AstraPro: Preserve the original analysis and attach newly supplied context. -->
See [scene notes](UBERHAR_SCENE_NOTES_0.1.9.md). LEGO's near-399% section is now
identified as coast-facing, not dense-city performance. Dark Moon's repeated
ghost-electrification cue aligns its 2x sections and its 4x sections across runs,
strengthening the within-resolution regression evidence. Sonic countdown toggles
are prospective user markers. No historical speed value is changed by this annotation.
The initial local-only publication status above is historical; consult the follow-up
handoff and GitHub build status for the subsequent publication attempt.

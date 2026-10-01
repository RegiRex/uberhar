# 0.1.10 matched evidence: preserve Sonic, repair retry correctness

<!-- AstraPro: October 1, 2026. Measurements, owner scene notes and inference are separate. -->

## Scope and provenance

Input: `uberhar_log_10_1_1706_LSMDM_SASRT.txt`, build
`8f2d9c86180c21fb043b7771331d8d762d7263eb` (0.1.10), Thor/Adreno 740.
Four completed sessions: Dark Moon Combo/Native followed by Sonic Combo/Native.
The earlier Dark Moon-only log is an exact prefix and is NOT counted again.
Raw user logs are not included in this repository change.

The owner reports an almost-full-speed Sonic race in Combo, slowdown entering lap
three and recovery late in that lap in both modes. An item sent the Native car
off course. These are owner observations; exact lap/finish timestamps are not
logged. Each Sonic run records one 100-to-400 transition near startup, not the
proposed additional countdown toggle. Do not relabel all accelerated frames as
race frames or derive precise lap boundaries from speed dips. FEA was reportedly
normal in Native; its missing log means this is qualitative evidence, not a new
numerical baseline.

## Measurements

All four runs remain at 2x and guest CPU clock 100%. Sonic predominantly commands
400%; Dark Moon commands 100%. Mode profiles differ (including force_tev false
in Combo versus true in Native); this is a comparison of complete mode profiles,
not a one-variable microbenchmark.

| Sonic measurement | Combo | Native |
| --- | ---: | ---: |
| Whole accelerated band mean | 125.054% | 86.632% |
| Accelerated-band observed seconds | 159.005 | 229.680 |
| System frames, entire session | 12,074 | 12,066 |
| Game submissions, entire session | 11,435 | 11,455 |
| Complete constant-400 windows | 31 | 45 |
| Median complete-window speed | 97.480% | 66.516% |
| Minimum complete-window speed | 81.954% | 49.272% |
| Windows at or above 95% | 18 / 31 | 4 / 45 |
| CPU vertex shader invocations | 161,419,177 | 805,770,409 |
| CPU vertex-stage wall seconds | 21.611 | 108.906 |
| GPU-promoted batches | 4,225,954 | 0 |
| Optional GPU pipeline builds / driver seconds | 52 / 0.525 | 0 / 0 |

The aggregate speed ratio is about 1.44, but menus/scene differences/cache state
and host conditions prevent treating it as a clean causal race-only speedup.
The slower-window evidence and approximately 80% lower CPU invocation count make
a blanket rollback of the working Combo route inappropriate. A 125% aggregate
is not a full-speed floor: the 81.954% window and other sub-100 intervals remain.
These are approximately five-second means, not 1% lows or display latency.
The plotted system-frame axis is only approximate progression, not measured lap
alignment; the opponent detour may change scene content at a given frame.

Combo starts with empty application caches, Native with present files/three
generic hits. Thus a warm-cache advantage for Combo does not explain this pair.
Driver-internal caches are unknown. Recorded battery temperature is 28 C;
instantaneous CPU clocks change and host performance mode is unobserved, so these
are not controlled thermal experiments or a power-efficiency measurement.

The retained Dark Moon pair is still a failure: Combo 13.287% over 309.054 s,
Native 33.123% over 123.375 s, with 2,457 versus 2,445 system frames and the same
222 guest reads / 25,641,387 bytes. Both start with empty application caches.
Combo promotes 485,811 batches and builds 80 optional pipelines with 107.876 s
aggregate driver creation time, versus Sonic's 0.525 s for 52. Worker driver time
is not foreground stall duration. Shader-module compilation is a separate metric.
The pre-crash comparison exits normally; it is not a native crash trace.

## Source review and selected correction

Parent for the new change: published 0.1.11
`22abb13398fb161c4122e8ee839372ea44c64a87`. Its logger-retention workflow passed;
rendering is unchanged from 0.1.10. Reviewed the ready-draw flow, uniform upload,
shared uniform sync, shader admission, vertex input bounds and assembly policy.

A concrete retry defect exists in `RasterizerAccelerated::SyncDrawUniforms`:
`vs_data_dirty = flip_changed` replaces, instead of accumulates, a pending upload.
A ready GPU attempt calls this function, consumes the PICA dirty-register table,
and may return before `UploadUniforms` because its pipeline is not ready. CPU
recovery calls sync again with unchanged registers. The old assignment can clear
the pending clip/viewport update; the CPU draw can use the previous uploaded
block. The same invariant matters for any earlier no-draw return before upload.

Change that assignment to OR accumulation. Only the existing upload path clears
`vs_data_dirty`. No shader math, clocks, GPU eligibility, fragment policy, cache
limits, fixed-GS behavior, draw order or driver workarounds are changed.
A bounded counter reports repeated visits to an unuploaded clip/viewport block.
It does not claim that each visit caused a bad pixel or uniquely identifies a
failed GPU attempt. The affected 32-byte block is clip/viewport state, not the
separate PICA program-constant block.

This is a proven host-state defect and a plausible contributor to image errors,
NOT proof of the reported Dark Moon crash's cause. No trace or GPU capture
connects the crash to this flag. It does not address the expensive driver builds
or promise full-speed Dark Moon. Additional failures may remain.

## Alternatives and next decision

Retain Sonic's specialized GPU route and existing CPU fallbacks. Do not globally
revert Combo based on Dark Moon; do not remove quaternion geometry correction to
make it faster without numerical/image validation. Do not change cache caps just
because aggregate driver time is high. Do not use FPS alone to select an allegedly
correct route: a faster image can still be wrong.

The next device question is whether the small retry correction changes Dark
Moon's anomalies, while Sonic retains its better slow-window performance. If
anomalies remain, investigate GPU stage outputs, buffer/descriptor lifetimes and
fixed-GS behavior with bounded stage-specific evidence, not another broad
throughput change. Existing Dark Moon observations warrant Native for normal
play until correctness is demonstrated.

This is a focused early correctness review, not a reset of the full architectural
review anchor. The full 0.1.9 review remains the anchor; 0.1.12 becomes successor
three only after publication. Default full review after 0.1.13 evidence, allowed
0.1.12-0.1.14 and before 0.1.15, or sooner for another changed assumption.

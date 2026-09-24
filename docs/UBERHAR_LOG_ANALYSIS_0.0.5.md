<!-- AstraEH: Owner-supplied device evidence; no raw logs, personal paths or game files included. -->
# Awakening 0.0.5 device log — 2026-09-24

The attachment `uberhar_log_9_24_0254_FEA.txt` identifies source
`a3c0476502c5ea217b6546de46ee8b9733806312` (0.0.5), Vulkan, Adreno 740,
Qualcomm driver 512.676.53, 2x internal resolution and a 100% frame limit.
Both runs have hybrid enabled and **Force TEV disabled**, with asynchronous
shaders/presentation and SPIR-V generation enabled, and the SPIR-V optimizer
disabled. Explicit shader-cache deletions precede the first launch. The second
loads a 16,294 KB driver cache for the same title. Both shutdown totals are present.

| Observation | Cold run | Warm run, altered gameplay |
| --- | ---: | ---: |
| Draw requests | 193,452 | 164,423 |
| Specialized-pending observations | 1,175 | 79 |
| Selected fallback draws | 149 | 2 |
| Skipped draws | 0 | 0 |
| Fallback families / pipelines built | 37 / 51 | 12 / 12 |
| Scheduler pipeline waits | 206 | 17 |
| Scheduler wait total | 29,127.665 ms | 3,942.500 ms |
| Maximum scheduler wait | 1,101.304 ms | 474.793 ms |
| Waits at least 50 ms | 115 | 12 |
| First-ready waits / late fallback draws | 35 / 10 | 9 / 2 |
| Fallback GLSL/module preparation | 610.541 ms | 211.787 ms |
| Fallback pipeline build wall time | 24,149.605 ms | 5,864.256 ms |
| Fallback driver-call total | 24,113.470 ms | 5,863.971 ms |
| Fallback VS / FS / GS wait total | 35.340 / 0 / 0 ms | 0 / 0 / 0 ms |
| Fallback driver maximum | 1,537.072 ms | 891.479 ms |
| Specialized builds | 404 | 438 |
| Specialized driver-call total | 105,999.296 ms | 6,876.976 ms |
| Specialized worker queue total | 218,194.080 ms | 337.309 ms |

Worker durations overlap each other and scheduler stalls. Driver-call time can
include internal locks; it is not pure compiler CPU time. Do not add these
columns together or divide them by session duration to claim a frame-time
percentage. Startup cache reconstruction is included in specialized build counts.

## Conclusions supported by this capture

1. **Fallback pipeline creation is expensive inside the driver.** Shader module
   preparation and VS/GS dependency waits are relatively small here. A vertex
   interpreter is not the first correction indicated by these measurements.
2. **First-ready scheduling alone is insufficient.** Only ten cold draws use a
   fallback that completed after enqueue. The fallback often cannot arrive in
   time. Many builds may be wasted, but 0.0.5 cannot identify per-pipeline utility;
   0.0.6 adds that measurement rather than inventing a wasted-build count.
3. **The warm run reaches additional pipeline states.** It builds 438 specialized
   pipelines versus 404 cold, consistent with the owner's changed gameplay and
   in-game fast-forward use. The same title cache is loaded. Neither this log nor
   the cache key code establishes a separate cache selected by fast-forward mode.
4. **A measured speedup over 0.0.3 is not established.** The prior cold capture
   had 18,221.986 ms of waits across 195,671 draws. This one has 29,127.665 ms across
   193,452 draws. The scenes/timing are not perfectly controlled; the owner's
   subjective impression and these counters measure different things.
5. **The sustained forced-mode penalty is a different comparison.** Force TEV
   intentionally keeps using the interpreter after specialization. It was off in
   this capture. Two fallback draws among 164,423 warm draws cannot establish a
   persistent interpreter GPU penalty for this run. GPU time is not logged, and
   2x was requested as a controlled test setting, not proved necessary.

## 0.0.6 response

Source inspection found six expanded copies of the full interpreter, each
decoding up to six operands and repeatedly calling texture sampling helpers.
0.0.6 uses one six-stage loop with a SPIR-V DontUnroll hint, per-fragment lazy
texture reuse, and operand arity checks. It preserves stage order and upstream
combiner formulas. Normal driver optimization is restored; the preceding flag
experiment did not establish an improvement and could sacrifice execution speed.

The host probe's simple unlit family shrinks from 33,690 to 11,592 GLSL bytes and
73,980 to 24,892 unoptimized SPIR-V bytes. Other representative lighting/procedural
families also shrink. These measurements establish a smaller compiler input,
not faster compilation on Qualcomm. The log records new module sizes and
completed fallback pipelines used/unused by the scheduler, plus the driver time
of unused builds. Progress snapshots can change; drained totals are final.

The new loop and texture reuse passed 57,344 exact RGBA8 comparisons and 57,344
independent texture-use checks on Mesa. The 64 full module families validate
with the frontend optimizer off and on (128 checks). Android/device verification
is a separate gate. The changes do not skip draws or enable blacklisted Vulkan
extensions. Further family generalization or prewarming must be justified by
actual fallback utility and driver timing in the next device capture.

## Preprocessing limits

The PICA texture combiner is controlled by live GPU register state; see
[3dbrew's register research](https://www.3dbrew.org/wiki/GPU/Internal_Registers).
The local `FSConfig(regs)` generator and pipeline keys show how Uberhar combines
that state with sampling, lighting, vertex/geometry programs and fixed-function
state. Consequently, a static ROM scan is not a reliable inventory of every
host pipeline a game can request. Offline game-aware analysis may recover some
programs; complete first-run coverage is not established.

`ShaderDiskCache::InitPLCache` already reconstructs previously observed exact
pipelines at startup. A useful future preprocessing feature would generalize
or prewarm fallback coverage beyond that existing behavior. It must also avoid
turning a few stalls into large speculative startup or driver-cache costs.

Attachment SHA256:

```text
f3618ef7598e766626661b435dcab8462cee0a1ddcc5909624777438999739e5
```

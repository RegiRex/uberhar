# 0.0.14 Thor results and the compact-lighting response

<!-- AstraEH: Measured device evidence, pause handling, limits and the next alpha. -->

Analyzed 2026-09-26 America/New_York (2026-09-27 UTC), from
`uberhar_log_9_26_2109_FEA.txt`, 917 lines. Revision `4624dfd0891b747f03cda50de76780e39fc6829a`
identifies released 0.0.14; startup records renderer diagnostics 11, ABI 4/120 bytes,
and frame schema 1. Both runs use Native/Vulkan, 2x, CPU JIT, generic-primary
fragments, disk cache, optional SPIR-V optimization disabled and a 100% emulator
speed cap. All draws route through native rendering; compute eligibility is zero.
The owner's report is very good performance with a few remaining battle pauses.

## Sleep does not invalidate this capture

Run 1 pauses at log 47.364147 s and resumes at 624.597646 s (lines 339/347), a recorded
**577233.519 ms**, or about 9 minutes 37 seconds. Surface teardown/recreation accompanies
the pause. A second 1110.341 ms pause is the exit menu. The lifetime is 653316.013 ms;
active observed frame intervals total 74676.045 ms. Explicit pauses and crossing
frames are excluded by the existing accounting; two excluded intervals and zero
clock discontinuities are reported. Run 2 has only its 1168.940 ms exit pause.

Consequently the sleep interval is not a shader hitch or a 9-minute worst frame.
It can still change the device's physical/thermal conditions, which the log cannot
control. CPU-stage `window_wall_ms` progress snapshots can span a pause and must
not be used as active-time speed estimates; frame `observed_wall_ms` is the
appropriate active denominator. Compiler wait/build totals do not become frame
speed measurements by dividing by raw session lifetime.

## Results

| Metric | Cold run 1 | Warm run 2 |
| --- | ---: | ---: |
| Observed active seconds | 74.676 | 67.688 |
| Emulation speed | 95.346% | 99.757% |
| System FPS | 57.046 | 59.686 |
| Game submission FPS, mixed scenes | 45.825 | 47.690 |
| System frames / game submissions | 4260 / 3422 | 4040 / 3228 |
| Generic foreground wait | 3297.621 ms | 108.247 ms |
| Longest generic wait | 297.766 ms | 4.927 ms |
| Scheduler pipeline wait | 96.600 ms | 1.000 ms |
| Longest frame interval | 581.915 ms | 66.538 ms |
| Intervals at least 50 / 250 / 500 ms | 15 / 6 / 4 | 11 / 0 / 0 |
| Generic families / pipelines built | 16 / 64 | 16 / 63 |
| Generic disk module hits / misses | 0 / 16 | 16 / 0 |
| Generic frontend stage | 347.542 ms | 26.077 ms |
| Generic pipeline wrapper stage | 2939.222 ms | 77.855 ms |
| Actual generic driver calls | 2937.550 ms | 76.309 ms |
| Longest generic driver call | 265.684 ms | 2.974 ms |
| Slow generic driver builds | 16 | 0 |
| CPU vertex stage | 21326.051 ms | 20322.036 ms |
| CPU shader JIT programs / total compilation | 12 / 6.363 ms | 12 / 5.234 ms |
| Input vertices / executed vertex shaders | 115388355 / 40702233 | 108846290 / 38263095 |
| Indexed vertex cache hits | 74686122 | 70583195 |
| Generic / specialized recovery draws | 184294 / 15163 | 171790 / 13677 |
| Specialized pipelines / fragment modules | 16 / 13 | 16 / 13 |
| Specialized driver calls | 208.869 ms | 4.430 ms |

Totals appear at lines 486–510 and 889–913. There are **zero skipped draws, generic
failures, module rejections or module write failures**. Warm complete five-second
windows range 98.228–100.260% speed. Battle-like windows near 30 game submissions/s
report about 100% emulation speed, consistent with full speed for those scenes,
not proof of a game-specific battle marker or physical display synchronization.

Five health samples show power saver off, low-memory false, battery 75%, 23–24°C,
and available system memory 9766–9960 MiB. Thermal status 0 with unavailable headroom
does not rule out throttling; battery temperature is not GPU temperature. A final
zero native-heap sample is not evidence of zero process memory.

## Comparison with 0.0.13

| Metric | 0.0.13 cold | 0.0.14 cold | 0.0.13 fully warm | 0.0.14 warm |
| --- | ---: | ---: | ---: | ---: |
| Generic wait, ms | 2947.990 | 3297.621 | 59.313 | 108.247 |
| Largest frame, ms | 495.818 | 581.915 | 90.394 | 66.538 |
| Emulation speed | 95.847% | 95.346% | 99.776% | 99.757% |
| Intervals at least 500 ms | 0 | 4 | 0 | 0 |
| Observed generic families | 21 | 16 | 18 | 16 |

The same-state census gives **37 families under 0.0.12 rules, 21 under 0.0.13 and 16
under 0.0.14 in each current run**. This is a clean 23.81% key reduction from 0.0.13's
rules for these draws. Remaining lighting shapes number 10; procedural shapes 1.
These counts are uncapped and no hypothetical old programs are compiled.

Fewer keys did **not** reduce this capture's total cold wait: it rises 11.86%,
actual generic driver time rises 17.42%, and the worst interval rises 17.36%. The
frontend stage falls from 431.831 to 347.542 ms, but larger generic lighting programs
cost more in the driver. There are fewer 50-ms-or-longer intervals (19→15), yet
more half-second ones (0→4). Warm speed remains essentially unchanged, although
small hitch counts/times fluctuate. Preserve 0.0.13 as a cold-performance reference;
do not label 0.0.14 a uniform latency improvement merely because its key count fell.

Replay lengths, draw/vertex counts, animation coverage and the sleep interruption
still differ. Same-state key counts isolate sharing, not saved milliseconds or
proof of a causal timing regression. The previous fully warm run was the third
0.0.13 run; that log's second run encountered new ally-attack effects.

## What still blocks

The family details now connect source size, active lights and compile events:

| Observed family class | Families | GLSL bytes | SPIR-V bytes | First driver call range |
| --- | ---: | ---: | ---: | ---: |
| Unlit | 1 | 13884 | 32980 | 95.622 ms |
| One light | 5 | 18391–18909 | 45632–47168 | 116.208–129.397 ms |
| Three lights | 2 | 21162–21361 | 55692–56232 | 171.654–181.400 ms |
| Four lights | 4 | 22979–23183 | 61768–62312 | 195.742–225.636 ms |
| Five lights | 4 | 24802–25001 | 67848–68388 | 226.250–265.684 ms |

The cold run's four longest frames contain pairs of slow native pipeline builds:

| Frame | Interval | Contained driver calls | Source lines |
| --- | ---: | ---: | --- |
| 3432 | 581.915 ms | 493.048 ms | 422, 424 |
| 3532 | 581.239 ms | 491.934 ms | 443, 445 |
| 3338 | 517.413 ms | 431.782 ms | 416, 418 |
| 3458 | 517.393 ms | 421.378 ms | 426, 428 |

Intervals use run origin 8.507950 s, reconstructed from run-end timestamp minus
lifetime. Build records occur at completion, with start derived from driver duration;
both endpoints lie within each listed frame. These timings overlap with foreground
wait, frontend and frame work; never sum them as independent pauses.

The pairs use 2D versus cube resources, which must remain correctly typed. The
larger programs come from expanding the light body repeatedly, while per-slot
attenuation/two-sided flags split additional families. CPU shader translation is
only milliseconds for the entire session; replacing it is not the current cold
stall priority. Warm's longest frame is not explained by its 4.927 ms generic wait.

## Next step: 0.0.15

Use one ordered runtime lighting loop, carrying count and seven per-slot operation
flags in two additional words (ABI 5, 128 bytes). Do not process inactive slots or
change summation order. Preserve physical source remapping, the inherited LUT
indexing convention, disabled LUT defaults, final-light Fresnel and shadow rules.
Keep global bump/shadow/material structure, cube/2D resource separation, native
fixed-function state and driver workarounds. Validate all 1–8 light counts plus
zero, every operation flag, color/depth, both compiler optimizer modes and retained
specialized recovery. The intended reduction is both fewer families and less
repeated compiler input; device speed and shader sizes still need measurement.

Add same-workload `alpha14_families` and runtime light-count coverage. Separate
normal/fast/uncapped lifetime frame statistics with fixed memory and at most three
shutdown records. Limit-change intervals remain in overall timing but are excluded
from band attribution; pause behavior is retained. This supports the owner's next
**Native/2x cold then warm tests using emulator fast-forward capped at 400%**.
The cap is a request, not a guaranteed speed; in-game fast-forward alone is not
an emulator throughput test. Existing five-second windows still show scene variation.

Keep 0.0.15 an alpha. A reasonable 0.1.0 milestone is repeatably smooth cold/warm
Native at 2x in the agreed test, stable speed/visuals with no dropped draws or
experimental failures, and useful fast-forward headroom evidence. It need not hit
400% to qualify, nor imply compatibility with all games or completed compute/display
work. Confirm the owner finds remaining pauses satisfactory before advancing.
Then test 3x and 4x separately;4x has four times 2x's internal pixel area, so fast-forward
headroom at 2x does not by itself prove 4x full speed. The next full architectural
review remains due by default after 0.0.16; this result analysis does not reset it.

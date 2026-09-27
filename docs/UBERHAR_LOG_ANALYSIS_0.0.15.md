# 0.0.15: four Thor runs and the Native beta milestone

<!-- AstraEH: Device evidence, not a prediction of 0.1.0 performance. -->

Source: owner's `uberhar_log_9_27_0101_FEA.txt`, 1,611 lines, revision
`6b9769567d9680fc987099409e8a6c0bd7ae7ba5`, released 0.0.15. Reviewed 2026-09-27.
All four sessions are Fire Emblem Awakening (`00040000000A0500`) on AYN Thor,
Adreno 740, Native/Vulkan at 2x, CPU shader JIT enabled, GPU vertex shaders disabled,
generic TEV forced, bridge disabled and disk cache enabled. The owner reports no
noticeable normal-speed slowdown and considers this the 0.1.0 milestone.

## Results

| Measurement | Normal cold | Normal warm | Fast cold | Fast warm |
| --- | ---: | ---: | ---: | ---: |
| Session | 1 | 2 | 3 | 4 |
| Achieved speed in the intended speed band | 98.858% | 99.795% | 283.379% | 292.518% |
| Band active wall time, seconds | 71.583 | 70.409 | 47.207 | 47.938 |
| Band frames | 4,234 | 4,204 | 8,004 | 8,390 |
| Whole-session generic foreground wait, ms | 662.286 | 43.311 | 701.886 | 45.223 |
| Whole-session generic module hits / misses | 0 / 4 | 4 / 0 | 0 / 4 | 4 / 0 |
| Generic families / native pipelines | 4 / 37 | 4 / 37 | 4 / 37 | 4 / 37 |
| Whole-session maximum frame interval, ms | 385.727 | 77.787 | 399.741 | 64.105 |
| Intervals at least 100 ms | 3 | 0 | 3 | 0 |
| Intervals at least 500 ms | 0 | 0 | 0 | 0 |
| CPU vertex-stage wall time, ms | 21,369.564 | 21,011.332 | 20,529.914 | 20,428.012 |
| CPU shader JIT compilation, ms | 5.795 | 5.151 | 5.456 | 5.240 |

Header lines: 139, 563, 979, 1356. Speed-band lines: 431, 838, 1226–1227,
1601–1602. Totals blocks: 415–440, 822–847, 1210–1233, 1585–1607.

Sessions 3 and 4 begin at 100%, then use a temporary 400% emulator limit. Their
whole-session speeds (269.307% and 258.539%) are **not** valid fast-only comparisons:
the warm session spent 10.251 seconds at normal speed versus 3.825 seconds cold.
The table uses the separate fast bands, omitting one transition interval each.
A complete warm fast window reaches as low as 189.049%, consistent with the
owner's observation. A 400% cap is a ceiling, not a delivered rate. Scene timing,
input and animation coverage remain uncontrolled, so these are observational runs.

Each session has one short exit pause and one excluded startup interval. There are
no clock discontinuities. The longest cold intervals all end within the first
7.577 seconds (normal) or 6.866 seconds (fast) after their run starts. The two
longer normal-cold intervals end at 5.438/7.577 seconds; the corresponding logs
show first-use lighting program builds. Do not call these literal zero-hitch runs
or infer a precise game scene without video. Later normal-cold intervals remain
below 100 ms, consistent with the owner's much smoother gameplay report.

All four sessions report zero skipped draws, generic build failures, rejected
modules or module write failures. Compute coverage remains zero. Routine guest
filesystem/service errors also appear; absence of renderer failures does not mean
that every log line is error-free or every title is compatible.

## What improved

| Cold Native/2x comparison | 0.0.14 | 0.0.15 |
| --- | ---: | ---: |
| Generic foreground waiting | 3,297.621 ms | 662.286 ms |
| Generic frontend work | 347.542 ms | 74.686 ms |
| Actual generic driver calls | 2,937.550 ms | 580.570 ms |
| Longest individual generic driver call | 265.684 ms | 160.498 ms |
| Largest frame interval | 581.915 ms | 385.727 ms |
| Intervals at least 500 ms | 4 | 0 |

Observed generic waiting falls **79.916%**. These wall times overlap and must not
be added into an invented total cost. CPU vertex-stage time stays near 21 seconds;
the compact loop primarily addressed shader compilation, not all emulation work.

The same observed state in each new run would produce 37 families under 0.0.12,
21 under 0.0.13 and 16 under 0.0.14; 0.0.15 actually uses four. That **16-to-4**
comparison is stronger evidence of key reduction than comparing unequal prior
sessions. Three lighting shapes remain. Counts 1, 3, 4 and 5 occur at runtime
(`seen_light_counts=03A`), sharing compact code. All 37 created generic pipelines
are used. Attachments, blending, rasterization and depth/stencil still require
native pipeline distinctions; four shader programs do not imply four pipelines.

## Next bottleneck and limits

Normal sessions process 115.264/112.767 million vertex inputs but execute only
40.603/39.715 million vertex shaders. The existing FIFO supplies the other
74.661/73.052 million inputs, approximately **64.8%**. Fast sessions have about
190 million inputs and 121 million hits each. Source inspection shows that each
hit still copies a 256-byte output buffer and repeats semantic conversion before
primitive assembly. This is avoidable work, independent of shader compilation.

The 20–21-second stage includes loading, execution, caching, conversion and
assembly; it is **not** a measurement of shader arithmetic alone. Fast sessions
also have more vertices and different cost per invocation. GPU execution time is
unmeasured. Thermal status remains zero and battery temperature is 23–24 C in
recorded samples, which does not rule out clock or scheduling limitations.

The [beta architecture review](UBERHAR_ARCHITECTURE_0.1.0.md) selects exact final-
vertex reuse and prepared register transport, plus sparse stage timing to identify
where subsequent work belongs. Accept Native as a scoped beta, preserve this
fragment implementation, and require another 2x cold/warm normal/fast comparison
before raising resolution. 0.1.0 has no device timing evidence yet.

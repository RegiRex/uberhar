<!-- AstraEH: Device evidence used to choose the 0.0.8 implementation. -->
# Awakening 0.0.7 log analysis

Input: owner's `uberhar_log_9_24_0517_FEA.txt`, captured on 2026-09-24.
The log identifies revision `09fb76f5ad110534612edfee7df39a3183d7dd7b`
(0.0.7). Comparison: the complete 0.0.6 capture discussed in
[the architectural review](UBERHAR_ARCHITECTURE_2026-09-24.md).
Private device logs are not committed to the public repository.

Both captures use Adreno 740, Qualcomm driver 512.676.53, Vulkan 1.3.128,
2x resolution, hybrid on, forced fallback off, asynchronous presentation on,
SPIR-V generation on and frontend optimization off. The 0.0.7 cold session
explicitly starts without a pipeline cache; the warm session loads 314 records.
The paths are not a controlled replay: the cold draw counts differ by 5.4%.

| Cold-session measurement | 0.0.6 | 0.0.7 |
| --- | ---: | ---: |
| Draw requests | 183,418 | 193,352 |
| Scheduler pipeline waits | 138 | 127 |
| Total scheduler wait | 18,848.574 ms | 18,957.466 ms |
| Longest scheduler wait | 1,042.670 ms | 1,062.682 ms |
| Waits at least 50 ms | 71 | 64 |
| Specialized pipeline builds | 314 | 314 |
| Specialized driver-call wall time | 68,196.689 ms | 67,785.019 ms |
| Built fallback fragment families | 37 | 18 |
| Built fallback pipelines | 52 | 50 |
| Fallback driver-call wall time | 10,076.261 ms | 10,075.305 ms |
| Fallback draws | 161 | 210 |
| Fallback pipelines used / unused | 9 / 43 | 15 / 35 |
| Unused fallback driver-call wall time | 8,959.527 ms | 7,808.628 ms |
| Warm-session scheduler waits | 0 | 0 |

**No meaningful reduction in cold scheduler waiting is established.** Total
waiting is 0.58% higher and the maximum is 1.92% higher. Fewer wait events or
fallback families do not establish smoother play. The owner's impression of
similar performance is consistent with these measurements. A warm session again
has no pending specializations or fallback draws. These counters are not GPU
frame times and cannot measure every cause of visible stutter.

The new census records 124 raw fragment families versus 73 canonical families,
but 246 raw pipeline combinations versus 217 canonical combinations. It also
observes 27 vertex configurations, two geometry configurations, 21 vertex layouts,
two attachment combinations, six blending states, four rasterization states and
14 depth/stencil states. The census is uncapped. These independent counts must
not be multiplied; they describe eligible candidates, not compiled objects.

Source consolidation works, but native pipeline combinations remain expensive.
The 0.0.7 fallback frontend total is only 128.661 ms, whereas its driver-call
total is 10.075 seconds. Specialized VS dependency waiting is 225.728 ms and FS
waiting 17.640 ms, versus 67.785 seconds in driver calls. These worker wall times
overlap one another and gameplay; do not add them to, or subtract them from,
18.957 seconds of actual scheduler waiting.

There are no renderer Error/Critical records in either capture. Repeated
service/BOSS and missing-file messages also appear in the comparison capture;
the logs do not establish them as a new rendering regression.

## Implementation decision

Retain 0.0.7's verified canonicalization, without presenting it as a latency win.
0.0.8 implements the next parts of the existing architecture roadmap together:

1. Put alpha, scissor, depth mapping, fog and compatible texture sampling controls
   into runtime fragment data, so more configurations share a generic module.
2. Offer a completed generic pipeline with a fixed vertex interface by temporarily
   using the existing CPU vertex engine for bounded eligible draws. GPU
   specialization still proceeds in the background.
3. Reuse complete pipelines when guest configurations already resolve to the
   same host shader modules; ignore only genuinely inactive pipeline fields.
4. Add failure-aware fallback selection and bounded feature-specific diagnostics,
   including capability evidence for future pipeline-library work.

This is still an on-demand generic bank. Its first native state can stall while
warming, and CPU vertex work can itself cost time. Test the bridge independently
using its switch. Ready startup coverage and a GPU vertex interpreter remain
separate future steps. The [0.0.8 notes](releases/0.0.8.md) define the retest.

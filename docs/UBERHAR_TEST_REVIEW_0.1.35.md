# 0.1.35 Thor test review

<!-- CodexAstraLocal: Report the delivered source and one test per method;
private raw game artifacts remain outside the public repository. -->

Released source is `7d2fb88864499dffe67bebc4bd8a9f7de3dc666b`.
[Actions run 37911046064](https://github.com/RegiRex/uberhar/actions/runs/37911046064)
passed all 24 required steps. Source, APK checksum, package and signing
compatibility were independently checked before installing with app data intact.
The protected 0.1.22 state remains unchanged. No uninstall or data clear occurred.

One Native and one full Combo Dark Moon opening completed with a fresh app
process for each, File 1 reset through the game UI, title Vulkan cache deleted,
Vulkan 2x, normal 100% limiter, CPU JIT and accurate multiplication. Ordinary
startup logs confirm empty application cache inventories; driver-internal caches
remain unknown. Both runs returned normally to the game list. No Calculated run
was repeated. Screen timeout was restored and both panels confirmed off afterward.

## Speed and scope

<!-- CodexAstraLocal: Distinct scene windows and observers prevent a causal
mode/version comparison or an interpretation of inclusive wall as CPU cycles. -->

| Measure | Native | Combo |
|---|---:|---:|
| Start action to first Back | 250.352337 s | 206.041529 s |
| Complete ordinary windows | 49 | 40 |
| Reporting wall | 246.739490 s | 201.481499 s |
| Mean normal speed | 27.664270% | 28.279028% |
| P05 reporting-window speed | 17.581% | 22.073% |
| Worst reported frame interval | 203.714 ms | 392.377 ms |
| Separate inclusive LoadVertices/wall | 79.434151% | 68.113714% |

Every accepted ordinary reporting window is below the 99% target. P05 is
wall-weighted over window averages, not a percentile of individual frames.
LoadVertices includes transport, execution, joins and ordered submission; it is
not exclusive shader time. Its remainder is not measured GPU time. Menus and
exit navigation are excluded from the opening figures but retained in lifecycle
counts. Native's adaptive operation took longer; the runs cover different scene
populations and temperatures, so these means do not establish a causal gain.

Excluding possible video and other observer overlap leaves 23 Native windows
(30.208937%) and ten Combo windows (27.675476%). Those are different scene
subsets, not an estimate of recording overhead. Optional log omissions remain
explicit: 95/54 reported over Native/Combo lifecycles, respectively. Missing
records are not zero-filled.

## Actual multicore work

<!-- CodexAstraLocal: Use explicitly aligned cumulative endpoints for invocation
coverage, and keep finite CPU samples separate from counts and lifetime gauges. -->

| Common retained counter span | Native | Combo remaining CPU route |
|---|---:|---:|
| No-GS CPU shader invocations | 1,012,641,195 | 715,973,499 |
| Invocations in certified batches | 21,265,254 | 2,884,165 |
| Certified fraction | 2.099979% | 0.402831% |
| Invocations executed by workers | 13,444,312 | 2,074,870 |
| Worker fraction of same denominator | 1.327648% | 0.289797% |

Adjacent input/parallel reports share the same owner and cumulative endpoints;
independent reconstruction verified these specific denominators. Internal
report populations differ because of optional loss. The fractions describe
invocations, not time, and do not cover the unobserved opening edges. Combo GPU
invocation counts are unknown; submitted GPU inputs are not added to this
CPU-invocation denominator.

TemporaryCarry rejects 42.138323%/42.378097% of retained Native/Combo draw checks.
Fewer than 256 submitted inputs account for 54.348450%/56.209229%. These draw
frequencies do not weight the cost of either refusal. The separate cutoff of
256 actual FIFO misses keeps some accepted batches on the owner. There were no
observed allocation or worker-startup failure deltas.

Seven named vertex workers existed in each of two 20-second samples per mode.
Their combined CPU consumption was 0.08 and 0.12 seconds in Native, and 0.01 and
0.16 seconds in Combo. Dominant emulation-thread consumption was 19.57/11.93
and 16.77/19.11 seconds, respectively. This establishes mostly idle vertex
workers in those samples, not sustained all-core scaling. Other compiler
threads are separate. Process CPU includes worker CPU and cannot be added to it.

The sampled GPU driver reported high activity in opaque device-wide recent
windows. These are not app-exclusive, interval-aligned GPU measurements or
proof of maximum GPU capacity. CPU tick time is also not frequency-weighted
capacity. The primary multicore objective remains unmet.

## Consecutive visual evidence and memory

<!-- CodexAstraLocal: Count decoded-frame events separately for moon and ghosts;
never equate capture PTS with guest-frame or physical-panel persistence. -->

All retained decoded frames were reviewed: Native 645 moon and 649 ghost frames;
Combo 629 moon and 623 ghost frames. Native footage contains four isolated moon
block/band events and three separate isolated ghost events, each clearing in the
next decoded frame. Combo footage contains no comparable abrupt event in its
visible population. The owner's moon-priority threshold was not crossed.
No cause is assigned to the recorded events.

Native's ghost clip begins with electrical-machine activity and covers later
beam/broom/book angles; its earlier quiet near-ghost lead-in is absent. Combo
covers the near ghost and early wide/book-holder sequence; later beam/broom
angles are absent. These unequal scenes cannot establish a mode/version event
frequency improvement or full-game correctness. No sustained prior dense
irregular ghost pattern was identified in the covered populations.

Battery readings were 23–24°C Native and 26°C Combo, AC powered, saver off.
Battery temperature is not chip temperature. During-play own-process KGSL
readings were 1075.164→1077.945 MiB Native and 2519.195→3027.984 MiB Combo;
RSS was 891.074→903.738 and 954.145→970.867 MiB. These endpoints establish the
observed growth, not a long-duration leak bound. Counters are not additive.
No routine exit-memory sweep was performed under the owner's updated policy.

## Development decision

<!-- CodexAstraLocal: Let actual reach guide the next candidate; preserve the
small-job cutoff and require proof before relaxing any rendering contract. -->

Increasing thread count is not the next useful step. Improve admission of the
currently serialized work or reduce its actual cost. An exact general ordered
carry/status split and an independent consumer audit are being investigated.
Neither may change consumed outputs, draw order or guest-visible state.
ARM64 partial writeback is a separate locally validated candidate with no device
speed claim. Small-job policy remains until measured complete-path evidence
supports changing it. General Calculated rendering, sustained 99% at 2x/4x,
longer gameplay and broader beta qualification remain unproved.

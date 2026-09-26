# Uberhar 0.0.13: shorter first-use pauses, warm speed retained

<!-- AstraEH: Device evidence separates new effect coverage from fully warm replay. -->

Source: `uberhar_log_9_26_1605_FEA.txt`, 1,310 lines, revision
`768a72794d0fe2df1e408f4e12e3f66e29607aaf`. The 0.0.13 prerelease and
[Actions run 36169628263](https://github.com/RegiRex/uberhar/actions/runs/36169628263)
are published/successful. This analysis is documentation, not a new runtime alpha.

All three complete runs are Fire Emblem Awakening (`00040000000A0500`) on the
AYN Thor / QCS8550 / Adreno 740, Android API 33. All use Native, Vulkan, 2x,
100% frame cap, CPU JIT, generic TEV, disk cache and disabled optional SPIR-V
optimization. Hardware vertex specialization and the CPU bridge are off.

The owner reports very short pauses and a large improvement. Run 1 is cold;
run 2 adds an ally attack; run 3 repeats without that attack and is the best warm
comparison with run 1. The log confirms new shader coverage in run 2, but has no
explicit attack marker, so exact attribution of each new module to the attack
remains an inference from the user's account.

## Three-run measurements

| Measurement | Run 1: cold | Run 2: retained cache, extra attack | Run 3: fully warm observed workload |
| --- | ---: | ---: | ---: |
| Observed frame-interval time | 74.843472 s | 72.435542 s | 69.952915 s |
| Emulation speed | 95.847% | 97.949% | 99.776% |
| System frame rate | 57.346 Hz | 58.604 Hz | 59.697 Hz |
| Mixed-scene guest submission rate | 46.163 Hz | 47.159 Hz | 47.775 Hz |
| Generic foreground waiting | 2947.990 ms | 1496.058 ms | 59.313 ms |
| Largest individual generic wait | 169.307 ms | 184.612 ms | 2.797 ms |
| Separate scheduler waiting | 88.859 ms | 1.433 ms | 1.010 ms |
| Worst observed frame interval | 495.818 ms | 540.242 ms | 90.394 ms |
| Frame intervals at least 50 ms | 19 | 13 | 10 |
| Frame intervals at least 250 ms | 6 | 3 | 0 |
| Frame intervals at least 500 ms | 0 | 2 | 0 |
| Generic families / pipelines | 21 / 64 | 30 / 74 | 18 / 59 |
| Generic module cache hits / misses | 0 / 21 | 21 / 9 | 18 / 0 |
| Generic frontend/module work | 431.831 ms | 217.234 ms | 24.945 ms |
| Generic pipeline wrapper work | 2503.344 ms | 1269.888 ms | 31.938 ms |
| Actual generic driver calls | 2501.697 ms | 1268.217 ms | 30.701 ms |
| Largest generic driver call | 149.022 ms | 161.838 ms | 0.981 ms |
| Slow generic driver builds | 21 | 9 | 0 |
| CPU vertex-stage wall time | 21047.781 ms | 21134.290 ms | 21048.272 ms |
| CPU JIT programs / compilation | 12 / 6.372 ms | 12 / 5.633 ms | 12 / 5.792 ms |
| Accurate recovery draws | 14717 | 14238 | 14146 |
| Skipped draws / generic build failures | 0 / 0 | 0 / 0 | 0 / 0 |

Evidence anchors: run headers at lines 207, 629, 1040; final renderer/CPU/frame
records at 479–506, 890–917, 1277–1304. Generic cache rejection and write-failure
counters are zero throughout. Draw totals are 195703, 195088 and 192245.
Every run creates 16 specialized recovery pipelines and 13 specialized fragment
modules. Their driver-call totals are 191.868, 4.828 and 5.146 ms. Thus run 3 has
zero *new generic module compilations*, not literally zero compilation of any kind.

Cache deletion precedes run 1 and no driver cache is found (line 208). Run 2 loads
788 KB (line 630); run 3 loads 1,125 KB (line 1041). All 18 generic modules needed
in run 3 load from disk. Run 2's nine misses explain why it must be treated as
partly warm. Run 3 uses 18 families versus run 1's 21, so even this closer pair is
not a byte-identical draw replay.

Foreground waits, scheduler waits, worker times, CPU stage time and frame
intervals overlap; do not add them. These are emulator frame-end measurements,
not physical panel presentation timings. Each run has one actual exit pause
(1169.538 / 1041.613 / 1049.245 ms), excluded from observed frame intervals.
Frame-limit records remain 100 throughout, with zero temporary-limit frames.

## Direct evidence that the family reduction works

Unlike comparisons between separate replays, the new census applies the previous
lighting key rules to the *same observed configurations* without compiling old
programs. No census is capped.

| Same observed workload | Families under 0.0.12 lighting key rules | 0.0.13 families | Reduction |
| --- | ---: | ---: | ---: |
| Run 1 | 37 | 21 | 43.24% |
| Run 2 | 55 | 30 | 45.45% |
| Run 3 | 31 | 18 | 41.94% |

This validates that runtime lighting controls reduce required family keys; it
does not turn the differences into hypothetical saved milliseconds. Remaining
lighting-shape counts are 16 / 22 / 14, while procedural-texture shape count is
one in all runs. Lighting remains a useful next target, but these counts alone
do not identify which remaining lighting fields cause each split.

## Comparison with the 0.0.12 baseline

The prior [two-run result](UBERHAR_LOG_ANALYSIS_0.0.12.md) was also Native/Vulkan/2x.
Cold generic waiting fell from 8120.581 to 2947.990 ms: **63.70% less observed
waiting**. The worst cold frame interval fell from 806.541 to 495.818 ms: **38.53%
shorter**. The cold run's worst complete five-second speed window improved from
35.597% to 75.289%. There are still six 250–500 ms cold intervals; the absence of
500+ ms intervals should not disguise the worst one being almost half a second.

Coverage differs: the older cold run reached 67 families under its own rules,
whereas the new cold run reaches only 37 under those same rules. The owner also
reported additional animation in the previous test. Therefore the 63.70% wait
reduction is an observed result, **not an isolated causal speedup**. The 43.24%
same-workload key reduction is the cleaner evidence for the implementation change.

Warm overall speed is retained: 99.760% previously versus 99.776% in run 3.
All thirteen complete run-3 windows report 98.383–100.258% speed. Three consecutive
windows at about 29.9 game submissions per second report 100.022%, 99.942% and
100.006%, consistent with full emulated speed in the battle-like scene. The log
has no exact battle-boundary labels. Warm generic waiting is now 59.313 ms versus
89.293 ms previously, with different family/pipeline coverage.

Not every pacing statistic improves: run 3's worst interval is 90.394 ms versus
69.365 ms in the older warm run, and its 50+ ms intervals number ten versus six.
These short outliers remain despite zero generic module misses. Current evidence
does not identify their precise CPU/GPU/scheduling cause. Near-full-speed averages
are not proof of perfectly even frame delivery or synchronized physical screens.

## Remaining long intervals still contain build clusters

Run origins derived from each run-end timestamp minus lifetime are approximately
25.791087, 103.891915 and 178.790184 log seconds. Clock/log emission introduces
small timestamp uncertainty; this does not affect the following multi-build clusters.

- Run 1's worst frame, 3426, spans log time 85.050161–85.545979 s (495.818 ms).
  Three slow generic driver builds complete inside it (lines 435, 437–438):
  119.174, 129.532 and 149.022 ms, totaling **397.728 ms**. Their implied starts
  also fall inside that interval. This is direct evidence that remaining
  first-use driver builds dominate this particular hitch.
- Run 2's worst three frames span 159.149431–159.689673,
  160.571466–161.073966 and 161.186976–161.684225 s. Each contains three generic
  driver builds. The nine calls total **1236.348 ms** (lines 830–840), out of
  1268.217 ms of generic driver calls for the whole run. This is consistent with
  the added effect encountering previously unseen programs.
- Run 3's largest generic wait is only 2.797 ms and largest driver call 0.981 ms.
  Its 90.394 ms worst interval cannot be explained by one such compilation wait;
  aggregate records are insufficient to assign that outlier to a specific cause.

Do not add these driver totals to the enclosing frame duration. Individual new
programs are not faster to compile: the cold maximum generic wait rises from
148.477 to 169.307 ms, and maximum driver call from 120.006 to 149.022 ms compared
with 0.0.12. The successful tradeoff is fewer programs/bursts while retaining warm
speed, not cheaper compilation of every individual program.

## Health, capabilities and diagnostic limitations

The seven health samples report power saver off, low-memory false, battery 80–79%,
battery temperature 21–23 C and available memory 9,583–10,188 MiB. Thermal status
is zero and headroom unknown, which does not establish absence of throttling.
Native heap reporting again becomes zero; it must not be interpreted as actual
zero process memory or proof of resource reclamation.

Graphics pipeline libraries and shader objects remain unavailable. Extended
dynamic state remains disabled by the existing workaround. Push-constant capacity
is 256 bytes. Compute eligibility remains zero, with depth/blend and other
unsupported-state guards active; this result validates the native path.

**Diagnostic label inconsistency:** `PerfStats` still prints `diagnostics=9`, while
the renderer correctly prints `diagnostics=10`, `fallback_abi=3`, `push_bytes=120`
and `runtime_lighting_luts=true`. The build hash confirms 0.0.13 throughout. The
core string was not updated with the renderer schema; it is not evidence of a
failed APK update. The next runtime alpha should remove this ambiguity by naming
the frame-accounting schema separately or sharing an appropriate version constant.
No timing or rendering defect is established by this label mismatch.

## Next development target

Retain **0.0.13 Native at 2x** as the current successful reference. The next
optimization should address remaining first-use lighting structure, particularly
LUT-enable/configuration variations, while preserving correct disabled-LUT
fallback values, light order and warm battle speed. The existing packed control
bytes leave room to investigate enable controls without expanding the ABI, but
actual family reduction and runtime cost require validation rather than assumption.
Do not disable driver workarounds or expand compute admission without correctness work.

Track same-run family reduction, cold worst-frame clusters and warm speed together;
keep unrelated warm pacing outliers as a separate profiling question. Record the
core schema-label correction with the next runtime change, not a logging-only APK.
This analysis does not reset architectural-review cadence: default after 0.0.16,
allowed after 0.0.15–0.0.17, before beginning 0.0.18. Dual-screen delivery and model
clarity remain later roadmap items; this log does not measure physical panel matching.

<!-- CodexAstraLocal: Final performance report, sanitized for the owner-authorized public archive. -->
> Public report copy, archived November 20, 2026. Measurements and historical qualifications are retained. Private paths, device identifiers and operational closeout details are omitted; raw captures were deleted during the authorized storage cleanup. References to retained raw evidence describe the original analysis, not its present availability. This archive does not authorize device operations or settings restoration.

# Expanded Uberhar history — Dark Moon, Fire Emblem and earliest other titles

<!-- CodexAstraLocal: Consolidate retained historical comparisons with .21 as explicitly named nearest .20-era evidence. No new benchmark or interpolated extrema. -->

The completed November19 Sonic results are published separately in this archive. Original .48 and CPU-only .48 remain distinct. Earlier version rows preserve their recorded resolution, renderer, speed cap and score population.


---

# Early milestones: .10 / .20 / .21 / .30 / .40

<!-- CodexAstraLocal: Correct an incomplete historic inventory using explicit version-pinned gameplay evidence. -->

**Correction, November 19:** Version 0.1.10 has verified Dark Moon and Sonic device measurements. The earlier bounded inventory missed the later report that explicitly identifies build `8f2d9c86180c21fb043b7771331d8d762d7263eb`. Dark Moon averaged **33.123% Native** and **13.287% Combo**, both at 2×. These are whole observed-run means; no five-second low/high has been reconstructed. [Original matched evidence](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_CORRECTNESS_0.1.12.md). Exact-version .20 gameplay remains unlocated.

<!-- CodexAstraLocal: Existing-report inventory only; keep version identity and scope distinct from run numbering. -->

**Dark Moon has retained gameplay measurements for .10, .30 and .40. Exact .20 gameplay remains unlocated. Sonic also has measured .10 runs; FEA .10 is owner-reported only.** This is not a claim that unindexed tests never existed. The JSON binds original sources, settings and hashes. No device work, raw-log extraction or new scene reduction was performed.

| Version | Dark Moon | Xenoblade | Fire Emblem Awakening | Sonic |
|---|---|---|---|---|
| 0.1.10 | Verified Native/Combo 2× measurements | Not found | Owner qualitative report; numerical log missing | Verified Native/Combo 2× measurements |
| 0.1.20 | No verified gameplay result found | Not found | Not found | Not found |
| 0.1.21 | Verified Full Combo 2×; nearest .20-era reference | Not found | Not found in this title-only capture | Not found in this title-only capture |
| 0.1.30 | 2 retained rows | Not found | Not found | Not found |
| 0.1.40 | 4 retained rows | Not found | Not found | Not found |

The .10 local validation predates the later .10 device runs; it is not evidence that .10 went untested. The .20 progress record reports packaging/signing failures and no observed candidate gameplay; its attached gameplay evidence belongs to .17. Neither is relabeled as a .10/.20 benchmark. See [the .10 validation](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_VALIDATION_0.1.10.md), [.20 release](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/releases/0.1.20.md) and [publication progress](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_PROGRESS.md).

**0.1.21 is included as the nearest available reference for missing .20 gameplay data; it remains labeled .21.** Its 55 stable Turbo400 windows average 12.959%, with a 7.605% low and 25.634% high. The separate normal-speed band lasts only 6.134 seconds and is not a representative gameplay benchmark. Cache files existed, but all observed generic requests missed; this is not a certified empty-cache run.

**Existing Dark Moon results.** Low/mean/high are percentages of normal game speed over approximately five-second complete logged windows. They are not 1s or individual-frame extremes. Means retain the original wall weighting.

| Version / renderer | Simulated GPU timings | Windows / accepted seconds | Low / mean / high | Mean app cores | Scope |
|---|---|---:|---:|---:|---|
| [0.1.21 Full Combo (historical mode 3)](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.21.md) | Not verified in this summary | 55 / 273.430 | 7.605 / 12.959 / 25.634 | — | Stable Turbo400 windows; nearest available .20-era reference |
| 0.1.30 Native (source not included in this compact archive) | OFF | 40 / 201.738 | 16.076 / 20.986 / 28.095 | — | Start→first Back |
| 0.1.30 Full Combo (historical mode 3) (source not included in this compact archive) | OFF | 40 / 201.695 | 16.424 / 20.932 / 28.547 | — | Start→first Back |
| 0.1.40 Native (source not included in this compact archive) | OFF | 49 / 246.279 | 26.345 / 38.771 / 90.012 | 1.919 | Start→first Back |
| 0.1.40 Combo (historical mode 3) (source not included in this compact archive) | OFF | 52 / 261.479 | 18.879 / 33.564 / 62.270 | 1.679 | Start→first Back |
| 0.1.40 Native (source not included in this compact archive) | ON | 35 / 175.523 | 57.046 / 68.598 / 90.456 | 2.031 | Start→first Back |
| 0.1.40 Combo (historical mode 3) (source not included in this compact archive) | ON | 56 / 280.774 | 38.256 / 66.361 / 90.350 | 2.050 | **Limited observation; exit input unknown** |

The .30 high values use only the original 40 accepted native-log line IDs in each frozen parsed result. The original low and mean are unchanged. .30 phases were recorded as unknown: its approximately 206-second Start→Back interval is not a complete, four-scene opening benchmark. Its worst system intervals were 337.695 ms Native and 315.366 ms Combo; these are host cadence, not an actual short-window emulation speed. App-core/GPU-busy measurements are unavailable there; LoadVertices wall-time brackets cannot fill those cells.

The six original .30/.40 rows used Vulkan 2x, **Linear Filtering OFF**, a 100% normal limiter and cold application shader caches; driver-private cache state was unknown. The runtime setting records confirm .30 simulated GPU timings OFF and the explicit .40 OFF/ON split, render-thread delay 0, CPU clock setting 100%, JIT and accurate multiplication ON. Exact driver package/hash, right-eye/guest-slider runtime values and matched Standard host-mode proof were not recovered from these reviewed summaries; blank INI fields are not inferred settings. Historical Full Combo/mode 3 is kept under its original name, not automatically equated with the later CPU-vertices ON/OFF variants.

The .40 timing-ON Combo row has 56 accepted windows within its predeclared 284.097-second observation; the later roughly 89-minute lifetime is excluded. First Back/observed normal-exit input is unknown. Timing-ON Native advanced through lab/moon sooner, so scene mixtures differ. Native timing OFF reported 6 available participants/5 workers, timing ON 7/6; this is not proof of worker causality. Battery samples were 29–31°C, AC-powered; thermal HAL was unavailable. Native timing-ON temporal captures contain documented events and review remains incomplete, so these measurements do not certify graphics correctness.

**Available .40 GPU context:** Native OFF low/high 75.2%/48.4%; Native ON 99.7%/44.4%; limited Combo ON 24.2%/70.1%; Combo OFF unavailable at either extreme. These are enclosed device-wide sample points with opaque integration, not scene GPU averages. The CPU ranges accompanying extrema overlap rather than fit wholly inside each speed window. [Original resource and visual qualifications](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_TEST_REVIEW_0.1.40.md).

**What can be said about .58.** Its current clean Dark Moon rows use Linear Filtering and simulated GPU timings ON, both diagnostics ON, Standard mode and cold application caches, with named guest-content scene bounds. Keep their existing 5s Overall values separate from the old whole-observation averages:

| Current .58 clean renderer | Opening moon | First lab | Ghost/moon | Second lab |
|---|---:|---:|---:|---:|
| Native — Current clean baseline | 58.61% | 54.46% | 54.02% | 52.45% |
| Combo OFF — Late clean baseline | 48.92% | 54.51% | 49.89% | 52.45% |
| Combo ON — Current clean baseline | 57.91% | 54.59% | 53.58% | 51.39% |

Current .58 observations are substantially faster than the approximately 21% .30 configuration in these named scenes. **No numerical build-speedup factor is justified:** the timing/quality settings and guest-content weighting differ. Conversely, .40 timing ON already had 68.60% Native / 66.36% limited Combo opening-population means, numerically above several current .58 scene means. That does not prove .58 regression either. This evidence does not support a claim of uniform, monotonic progress across all four milestones. Closed .58 Dark Moon stage (source not included in this compact archive).

No early 1s metrics, scene-level historical means, app utilization from vertex timers, missing driver identity, or other-game measurements have been manufactured. The missing evidence is an exact-version gameplay report with matched settings and guest-content boundaries; this inventory does not request replacement tests. Machine-readable values and complete source hashes (source not included in this compact archive).

---

# Fire Emblem Awakening — expanded recorded history, 0.1.0–0.1.58

<!-- CodexAstraLocal: Reformat existing reports and already-scored summaries; keep version, renderer, resolution, scope and original metric populations explicit. No raw-log rescoring, new tests, device operations or old-file changes. -->

**Speed is percent of normal.** This expands the retained history back to **0.1.0**, adding .38, .43, .49–.51 and .53–.56 to the earlier .48/.52/.57/.58 table. These are recorded checkpoints, not a continuous matched benchmark. Blank values remain unavailable; releases without a located numerical gameplay result are listed at the end.

`Low / mean / high` means accepted approximately **five-second window averages**, except where a row explicitly says **band** or **review mean**. Band means are the application's time-weighted normal/accelerated populations, which can include partial windows; do not combine their means with unrelated full-window extrema. **Actual one-second** low/high values appear only for .58. No early one-second values, missing highs, or scene boundaries were reconstructed. One window gives equal low/mean/high without establishing stable pacing.

App cores are process CPU core-equivalents; GPU busy is sampled whole-device activity. Resources belong to the same run and saved population, but older resource subsets may be shorter than the speed selection. `.38` GPU figures are medians; later figures are means unless noted. These figures do not isolate causal feature cost. Source reports retain available All-Core charts, low/high CPU7/GPU contexts, clocks and pacing; the links below preserve that context without pretending old missing measurements exist.

## Native 2× — normal-speed records

Normal caps are100% where documented; the .3 review summary does not restate its cap. Early scenes, cache histories, power profiles and durations differ. A result near 100% establishes the recorded capped behavior, not uncapped capacity.

| Build / run | Mean % | Recorded 5s low / high % | Population / evidence |
|---|---:|---:|---|
| .0 cold | 98.895 | — / — | Intended normal band; [F0] |
| .0 warm | 99.810 | — / — | Intended normal band; [F0] |
| .3 cold | 99.077 | — / — | Review whole-run mean; [F123] |
| .3 warm | 99.786 | — / — | Review whole-run mean; [F123] |
| .4 S2 | 98.709 | — / — | Normal band58.044s; [EARLY] |
| .4 S3 | 99.681 | — / — | Normal band50.084s; [EARLY] |
| .4 S4 | 99.667 | — / — | Normal band38.101s; [EARLY] |
| .6 cold | 98.292 | — / — | Normal band; [F6] |
| .6 warm | 99.912 | — / — | Normal band; [F6] |
| .7 cold | 98.197 | — / — | Normal band72.950s; [F7] |
| .7 warm | 98.320 | — / — | Short normal band12.359s; [F7] |
| .8 | 99.586 | 97.451 / — | Normal band74.886s; separately reported complete-window low; **not one shared score population**; [F8] |
| .9 A1 | 99.887 | 99.044 / 100.246 | Seven complete windows35.105s; whole normal band separately99.612%/38.793s; [F9] |
| .13 | 99.521 | — / — | Normal band72.131s, warm file inventory; [F13] |
| .14 B3 | 98.612 | — / — | Normal band54.644s; [F14] |

## Native and Combo 2× — accelerated records

All rows are at400% except the .0 intended-fast-band rows, whose exact cap is not restated in the retained summary. The early scene populations are not the later4× map/battle contract.

| Renderer / build / run | Low / mean / high % | Population / app cores / GPU busy | Evidence |
|---|---:|---|---|
| Native .0 cold | — / 270.601 / — | Intended fast band; resources unavailable | [F0] |
| Native .0 warm | — / 274.831 / — | Intended fast band; resources unavailable | [F0] |
| Native .7 warm | — / 258.159 / — | Fast band30.480s | [F7] |
| Native .9 A1 | 332.231 / 332.231 / 332.231 | One complete5s window only | [F9] |
| Native .12 | — / 323.573 / — | Fast band35.450s | [F12] |
| Native .14 B3 | — / 252.561 / — | Brief fast band5.446s | [F14] |
| Native .43 clean retest | 272.615 / 343.717 / 389.595 | Six windows30.012s; early chapter/tactical mixture;1.453cores /97.09%GPU in25.010s CPU subset | [F43] |
| Native original .48 S7 | 238.53 / 361.76 / 395.13 | Historical mixed context30.02s;1.508cores /74.9%GPU | [H54] |
| Combo .12 | — / 343.189 / — | Fast band37.198s; historical Combo | [F12] |
| Combo CPU vertices OFF .43 clean retest | 273.356 / 353.357 / 391.838 | Five windows25.007s; tactical+combat mixture;1.538cores /75.87%GPU in20.005s CPU subset | [F43], [H54] |

## Native 4× — early records

| Build / run | Cap | Low / mean / high % | Population / evidence |
|---|---:|---:|---|
| .1 | Unstated | — / approximately81 / — | Retained review summary; [F123] |
| .2 | Unstated | — / approximately88 / — | Retained review summary; [F123] |
| .3 | Unstated | — / approximately92 / — | Review mean; late5s windows separately approximately67–73%; [F123] |
| .4 S6 | 100 | — / 89.625 / — | Normal band42.686s; [EARLY] |
| .4 S7 | 100 | — / 92.770 / — | Normal band49.418s; [EARLY] |
| .5 warm | Unstated | — / — / — | Two reported late windows approximately64% and59%; **not** a mean/min/max range; [F5] |
| .6 cold | 100 | — / 90.683 / — | Normal band; [F6] |
| .6 warm | 100 | — / 91.682 / — | Normal band; [F6] |
| .7 first run | 100 | — / 92.529 / — | Normal band81.971s; [F7MAIN] |
| .7 second run | 400 | — / 119.275 / — | Fast band63.926s; complete-window subset separately105.712%/55.131s; [F7MAIN] |
| .9 A2 | 400 | 77.184 / 108.945 / 173.631 | Thirteen complete windows65.186s, unknown scene mixture; [F9] |
| .13 | Accelerated | — / 110.074 / — | Accelerated band; unequal scene/frame coverage versus Combo; [F13] |
| .38 S5 cold | 400 | 76.489 / 90.584 / — | Five complete windows25.020s;0.675app cores/98.58%GPU median in separate20.005s subset; simulated GPU timing **OFF**; [F38] |

## Other early renderer/resolution records

Historical Automatic/Combo labels are retained; they are not silently assigned a modern CPU vertices ON/OFF preset. The .6 Compute-labelled runs executed zero compute draws and fell back to Native.

| Renderer / scale / build | Cap | Low / mean / high % | Population / evidence |
|---|---:|---:|---|
| Native1× .6 | 100 | — / 99.989 / — | Normal band; [F6] |
| Compute1× .6 | 100 | — / 99.054 / — | Normal band; zero compute draws, Native fallback; [F6] |
| Compute2× .6 | 100 | — / 100.389 / — | Normal band; zero compute draws, Native fallback; [F6] |
| Automatic2× .6 cold | 100 | — / 98.963 / — | Normal band; zero compute draws; [F6] |
| Automatic2× .6 warm | 100 | — / 99.489 / — | Normal band; zero compute draws; [F6] |
| Combo2× .14 B2 | 100 | — / 98.652 / — | Normal band58.857s; [F14] |
| Combo4× .9 A3 | 100 | 74.044 / 85.927 / 97.822 | Two complete windows10.017s; [F9] |
| Combo4× .9 A3 | 400 | 62.559 / 107.392 / 228.058 | Twelve complete windows60.152s; [F9] |
| Combo4× .13 | Accelerated | — / 235.685 / — | Accelerated band; unequal scene/frame coverage versus Native; [F13] |
| Combo4× .38 S6 warm | 100 | 99.946 / 99.991 / — | Three windows15.027s; [F38] |
| Combo4× .38 S6 warm | 400 | 85.836 / 114.833 / — | Four windows20.071s; [F38] |
| Combo4× .38 S7 cold | 100 | 99.279 / 99.822 / — | Five windows25.048s; [F38] |
| Combo4× .38 S7 cold | 400 | 84.081 / 101.779 / — | Three windows15.042s; [F38] |

.38 S6 resource union:0.812app cores,97.75%GPU median; S7:0.687cores,78.03%GPU median. Those resource unions mix normal/turbo work and must not be attributed separately to both cap rows. Timing simulation is OFF. S6 has present generic/driver cache files; S7 empty application inventories. No high value was stored in the accepted speed-summary fields, so none is reconstructed.

## Later 4× pooled map+combat — keep separate from scene results

These are accelerated composite selections. Every low/mean/high triple is from one named run/cohort. .48 original uses the verified fresh reference, not the different older S9 selection. CPU-only .48 is a distinct variant. .51 S3 is the retained historical-sequence selection; separate Native repeats S1/S2 were52.77/73.87/98.07 and53.77/65.98/81.83 and are not combined with it. Historical driver/scene and fast-forward uncertainty prevents a matched build-speedup calculation.
### Native 4× — pooled historical selections

| Build / run | 5s low / mean / high % | App cores | GPU busy % | Scope / source |
|---|---:|---:|---:|---|
| .48 original fresh S2 | 80.82 / 119.12 / 144.86 | 0.916 | 98.22 | FF unknown; [LATER] |
| .48 CPU-only | 79.98 / 120.95 / 142.59 | 0.766 | 98.85 | Variant; [LATER] |
| .49 S11 | 76.67 / 96.80 / 109.68 | 0.681 | 99.0 | FF unknown; [H54] |
| .50 G1.S6 | 57.87 / 76.93 / 97.39 | 0.569 | 97.9 | FF unknown; [H54] |
| .51 S3 | 59.86 / 75.41 / 91.16 | 0.545 | 98.8 | Battle FF OFF; [H54] |
| .52 | 59.61 / 75.21 / 90.64 | 0.522 | 99.21 | Historical defaults; [LATER] |
| .54 S2 | 78.43 / 120.97 / 151.91 | 0.806 | 98.9 | Post-Continue composite; [H54] |

### Combo CPU vertices OFF 4× — pooled historical selections

| Build / run | 5s low / mean / high % | App cores | GPU busy % | Scope / source |
|---|---:|---:|---:|---|
| .48 original fresh S4 | 70.86 / 113.30 / 149.52 | 0.840 | 92.77 | FF OFF; [LATER] |
| .48 CPU-only | 73.82 / 112.70 / 150.75 | 0.677 | 98.13 | Variant; [LATER] |
| .49 S9 | 76.07 / 121.72 / 188.41 | 0.764 | 96.1 | FF unknown; [H54] |
| .50 G1.S7 | 63.40 / 89.56 / 103.22 | 0.669 | 98.3 | **In-game FF ON**; [H54] |
| .51 S8 | 60.47 / 88.46 / 110.58 | 0.604 | 97.7 | Battle FF OFF; [H54] |
| .52 | 61.88 / 84.00 / 103.44 | 0.587 | 98.74 | Historical defaults; [LATER] |

### Combo CPU vertices ON 4× — pooled historical selections

| Build / run | 5s low / mean / high % | App cores | GPU busy % | Scope / source |
|---|---:|---:|---:|---|
| .48 original fresh S6 | 73.87 / 118.56 / 142.33 | 0.981 | 94.49 | FF OFF; [LATER] |
| .48 CPU-only | 72.78 / 117.43 / 152.29 | 0.768 | 99.70 | Variant; [LATER] |
| .49 S10 | 80.02 / 96.94 / 113.41 | 0.674 | 98.9 | FF unknown; [H54] |
| .50 G1.S10 | 61.83 / 76.02 / 97.17 | 0.576 | 99.1 | FF unknown; [H54] |
| .51 S7 | 52.69 / 74.45 / 95.88 | 0.539 | 99.0 | Battle FF OFF; [H54] |
| .52 | 56.74 / 75.10 / 101.39 | 0.548 | 97.91 | Historical defaults; [LATER] |
| .54 S3 | 72.49 / 114.45 / 140.65 | 0.747 | 98.3 | Post-Continue composite; [H54] |

## Later 4× map and battle — comparable scene categories, not matched replays

All selected .54–.58 rows below use the System/stock driver. .55 uses application-cold launches except the explicitly warmr14. .56-r2 originals are cold Standard-mode runs; its repeat rows additionally follow closure of background apps. .55/.56 map includes scripted dialogue/tutorial/unit movement; .57 uses its older bounds; .58 uses the separately retained cinematic14–38, cursor-approach and nominal battle5–13 contracts. No precise .55→.58 matched-scene delta is justified.

Only a compact set of stock anchors is included: .55 Native generic-OFF/ON cold, Combo OFF generic-OFF cold and generic-ON cold/warm; .56 Native right-eye suppression OFF, Combo OFF/ON suppression ON at slider0%. .56 Combo delay100µs differs from .57/.58 delay0µs. .56 generic optimization is ON; .57/.58 baselines have it OFF. .58 detailed GPU timing and frame census are ON; .57 baseline diagnostics differ. These controls prevent calling this a pure version trend. See [F55], [F56] and [CURRENT] for all controls, variants and source rows.

In these tables `n5/n1` are window counts, **not repeated trials**. `old5s` resources use the saved scene population. .58 resource population `5s` or `1s` is retained beside the source. Unavailable actual1s data is written `— / —`.
### Native 4× — scene history through .58

| Build / configuration | Scene | 5s low / mean / high % | Actual1s low / high % | App cores | GPU busy % | n5 / n1 | Source / resource population |
|---|---|---:|---:|---:|---:|---:|---|
| .53 candidate | Map candidate | 98.74 / 119.39 / 138.66 | — / — | 0.883 | 88.1 | 3 / — | [H54]; old5s |
| .53 candidate | Combat candidate | 71.22 / 82.05 / 92.92 | — / — | 0.670 | 97.8 | 2 / — | [H54]; old5s |
| .54 S2 | Map candidate | 138.22 / 139.72 / 141.21 | — / — | 0.822 | 99.7 | 2 / — | [H54]; old5s |
| .54 S2 | Battle candidate¹ | 81.73 / 107.21 / 132.80 | — / — | 0.792 | 97.8 | 2 / — | [H54]; old5s |
| .55 r9 generic OFF cold | Map | 135.97 / 139.02 / 142.72 | — / — | 0.775 | 99.6 | 3 / — | [F55]; old5s |
| .55 r9 generic OFF cold | Battle | 78.63 / 103.56 / 132.01 | — / — | 0.772 | 97.6 | 3 / — | [F55]; old5s |
| .55 r11 generic ON cold | Map | 135.28 / 143.86 / 149.42 | — / — | 0.839 | 99.4 | 3 / — | [F55]; old5s |
| .55 r11 generic ON cold | Battle | 79.30 / 102.45 / 140.97 | — / — | 0.784 | 98.0 | 3 / — | [F55]; old5s |
| .56-r2 S1 suppression OFF | Map | 111.05 / 132.82 / 153.32 | — / — | 0.890 | 91.6 | 3 / — | [F56]; old5s |
| .56-r2 S1 suppression OFF | Battle | 70.70 / 93.83 / 116.85 | — / — | 0.737 | 98.7 | 2 / — | [F56]; old5s |
| .57 baseline | Map (older bounds) | 135.6 / 145.2 / 158.3 | — / — | 0.780 | 98.9 | 6 / — | [CURRENT]; 5s |
| .57 baseline | Battle (older bounds) | 80.8 / 80.8 / 80.8 | — / — | 0.650 | 98.9 | 1 / — | [CURRENT]; 5s |
| .58 baseline (qualified) | Cinematic 14–38 | 145.6 / 148.6 / 151.5 | 121.4 / 161.5 | 1.137 | 23.2 | 2 / 11 | [CURRENT]; 5s |
| .58 baseline (qualified) | Map cursor approach | — / — / — | — / — | — | — | — / — | [CURRENT]; — |
| .58 baseline (qualified) | Battle 5–13† | — / — / — | 68.2 / 81.5 | — | — | 0 / 7 | [CURRENT]; — |
| .58 unchanged profile | Cinematic 14–38 | 162.6 / 167.5 / 172.4 | 152.5 / 190.7 | 1.159 | 28.8 | 2 / 9 | [CURRENT]; 5s |
| .58 unchanged profile | Map cursor approach | — / — / — | 130.8 / 159.1 | 1.364 | 92.7 | 0 / 3 | [CURRENT]; 1s |
| .58 unchanged profile | Battle 5–13† | 81.2 / 81.2 / 81.2 | 73.5 / 98.0 | 1.169 | 96.9 | 1 / 4 | [CURRENT]; 5s |

### Combo CPU vertices OFF 4× — scene history through .58

| Build / configuration | Scene | 5s low / mean / high % | Actual1s low / high % | App cores | GPU busy % | n5 / n1 | Source / resource population |
|---|---|---:|---:|---:|---:|---:|---|
| .54 fresh stock S33 | Map | 146.35 / 147.20 / 148.05 | — / — | — | — | 2 / — | [F54FRESH]; old5s |
| .54 fresh stock S33 | Battle | 81.93 / 98.15 / 128.87 | — / — | 0.730 | 98.1 | 3 / — | [F54FRESH]; old5s |
| .55 r7 generic OFF cold | Map | 139.11 / 145.55 / 151.45 | — / — | 0.787 | 99.6 | 3 / — | [F55]; old5s |
| .55 r7 generic OFF cold | Battle | 89.52 / 117.06 / 144.73 | — / — | 0.892 | 98.5 | 2 / — | [F55]; old5s |
| .55 r13 generic ON cold | Map | 147.91 / 156.10 / 163.58 | — / — | 0.910 | 98.9 | 3 / — | [F55]; old5s |
| .55 r13 generic ON cold | Battle | 92.67 / 114.95 / 140.74 | — / — | 0.825 | 97.7 | 3 / — | [F55]; old5s |
| .55 r14 generic ON warm | Map | 155.69 / 159.29 / 163.95 | — / — | 0.823 | 98.7 | 3 / — | [F55]; old5s |
| .55 r14 generic ON warm | Battle | 77.92 / 98.68 / 119.49 | — / — | 0.631 | 98.0 | 2 / — | [F55]; old5s |
| .56-r2 S4 suppression ON | Map | 145.71 / 154.73 / 160.49 | — / — | 0.818 | 97.6 | 3 / — | [F56]; old5s |
| .56-r2 S4 suppression ON | Battle | 77.20 / 99.51 / 121.83 | — / — | 0.727 | 98.7 | 2 / — | [F56]; old5s |
| .57 baseline | Map (older bounds) | 149.1 / 152.1 / 159.5 | — / — | 0.750 | 99.4 | 5 / — | [CURRENT]; 5s |
| .57 baseline | Battle (older bounds) | 98.1 / 98.1 / 98.1 | — / — | 0.740 | 98.8 | 1 / — | [CURRENT]; 5s |
| .58 baseline | Cinematic 14–38 | 167.2 / 170.7 / 174.2 | 144.8 / 192.0 | 1.154 | 28.6 | 2 / 9 | [CURRENT]; 5s |
| .58 baseline | Map cursor approach | — / — / — | 155.0 / 161.0 | — | — | 0 / 4 | [CURRENT]; — |
| .58 baseline | Battle 5–13† | — / — / — | 82.3 / 86.1 | 1.204 | — | 0 / 3 | [CURRENT]; 1s |
| .58 unchanged profile | Cinematic 14–38 | 202.8 / 202.8 / 202.8 | 165.8 / 245.4 | 1.174 | 33.0 | 1 / 8 | [CURRENT]; 5s |
| .58 unchanged profile | Map cursor approach | — / — / — | 143.4 / 172.5 | 1.181 | 98.2 | 0 / 4 | [CURRENT]; 1s |
| .58 unchanged profile | Battle 5–13† | 82.8 / 82.8 / 82.8 | 71.7 / 94.5 | 0.952 | 97.8 | 1 / 4 | [CURRENT]; 5s |

### Combo CPU vertices ON 4× — scene history through .58

| Build / configuration | Scene | 5s low / mean / high % | Actual1s low / high % | App cores | GPU busy % | n5 / n1 | Source / resource population |
|---|---|---:|---:|---:|---:|---:|---|
| .54 S3 | Map candidate | 134.90 / 136.03 / 137.16 | — / — | 0.801 | 98.4 | 2 / — | [H54]; old5s |
| .54 S3 | Battle candidate | 72.49 / 91.31 / 109.54 | — / — | 0.700 | 98.2 | 3 / — | [H54]; old5s |
| .56-r2 S6 suppression ON | Map | 127.86 / 141.80 / 148.94 | — / — | 0.820 | 99.6 | 3 / — | [F56]; old5s |
| .56-r2 S6 suppression ON | Battle² | 84.29 / 114.72 / 145.28 | — / — | 0.857 | 97.9 | 2 / — | [F56]; old5s |
| .57 baseline | Map (older bounds) | 135.4 / 144.1 / 158.4 | — / — | 0.780 | 99.3 | 4 / — | [CURRENT]; 5s |
| .57 baseline | Battle (older bounds) | 80.2 / 80.2 / 80.2 | — / — | 0.690 | 99.1 | 1 / — | [CURRENT]; 5s |
| .58 baseline | Cinematic 14–38 | 237.6 / 237.6 / 237.6 | 204.6 / 281.9 | 1.169 | 36.1 | 1 / 5 | [CURRENT]; 5s |
| .58 baseline | Map cursor approach | — / — / — | 118.2 / 134.7 | — | — | 0 / 5 | [CURRENT]; — |
| .58 baseline | Battle 5–13† | — / — / — | 58.7 / 92.2 | 1.042 | 97.4 | 0 / 5 | [CURRENT]; 1s |

¹ .54 Native bounds are screenshot-inferred with roughly7.5s uncertainty. The broader S2 composite includes a78.43% window excluded by its short battle candidate. For Combo OFF, the later [fresh-stock report][F54FRESH] corrected earlier S4's93.29% battle mean because a window overlapped the exit drawer; its conservative two-window mean was89.86%. Fresh S33 is used above instead of recycling that older selection.

² .56 S6 excludes an uncertain terminal73.51% window. Its selected84.29% low is **not the whole battle minimum**. All .56 original selections omit one uncertain terminal window. The repeat report preserves terminal sensitivity and gives further same-build context: Native suppression-OFF S23 map146.54/147.86/149.18 and battle88.24/116.72/145.34; Combo OFF S18 map118.12/135.64/161.54 and battle86.16/114.04/141.85; Combo ON S21 map142.35/146.26/151.42 and battle85.24/109.58/133.94. These are separate runs, with different recorded combat moments; no original/repeat extrema are merged. [F56REPEAT]

**.58 qualifications:** qualified Native baseline retained compact cinematic/battle measurements after the ordinary log hit its100MiB cap; no clean map score is available. The unchanged profile turns ON generic optimization, original right-eye suppression, adaptive workers and displayed-eye preparation while leaving all eight additions OFF and capacity8. .58 battles are descriptive: attack/support/outcome/victory coverage differs; the nominal5–13 rows are not equal combat work. No unchanged Combo ON profile exists in these references. The .58 selected profile and baseline remain the primary current comparison; historical points do not replace that test contract. [CURRENT]

## What the retained history establishes

- **2× was already close to normal speed in .0**, with recorded fast-band means270.601–274.831%. Later short2× .43/.48 mixtures reached roughly344–362% means, but do not isolate an equal-scene improvement.
- **Early4× was uneven:** .1–.3 review means approximately81→88→92%; .9 Native/Combo complete-window means108.945/107.392% had lows77.184/62.559%. The high means never implied every battle stayed at100%.
- **.49–.52 preserve a substantial4× slowdown in the historical composites**, especially Native/Combo ON. .54 returns the broader Native sequence near the .48 range. These are mixed-scene observations, not per-commit causal proof.
- **.54–.58 make the map/battle gap explicit.** For example, .55 stock Combo OFF generic-ON cold map mean156.10% coexists with battle low92.67%; the warm battle low is77.92%. Short .57 and .58 battle interiors must not be ranked as if they covered those entire earlier battles.

## Coverage and missing releases

Located numerical gameplay/speed records are **.0–.9, .12–.14, .38, .43, .48–.58**. `.5` supplies only reported late-window values and cache/wait measurements, not a recovered aggregate. Some versions have several settings or repeats; the tables retain useful anchors rather than every driver/slider experiment.

No exact-version numerical FEA gameplay record was located for **.10, .11, .15–.37, .39–.42, .44–.47** in this bounded review. This means **not located**, not “never tested.” .10 has qualitative owner feedback and a missing FEA log; .44/.45 retain Software/Calculated startup/menu/failure evidence that is explicitly not qualified gameplay throughput. .46/.47 diagnostic/source work is not a substitute for that absent title/version benchmark. The .39 Azahar FEA reference is a different emulator, not Uberhar .39.

Search scope: existing milestone verification, current FEA table and later-version JSON/report; source release/log/architecture/test-review reports; targeted retained `.38`, `.43`, `.44–.47`, `.48–.54`, `.55` and `.56` reports. Existing .54 history supplies original paths to its .49/.50/.51/.53 reductions. The accepted `.38` summary fields were read directly, without computing new extrema from raw windows. No raw capture rescoring, network publication, device action, build or test was performed. Only this new report and a temporary report-formatting script were written.

## Source links and retained resource evidence

- [EARLY]: EARLIEST_GAME_DATA_VERIFICATION.md (source not included in this compact archive)
- [CURRENT]: FEA_CHAT_DATA.md (source not included in this compact archive)
- [LATER]: LATER_VERSIONS.md (source not included in this compact archive)
- [F0]: [UBERHAR_LOG_ANALYSIS_0.1.0.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.0.md)
- [F123]: [UBERHAR_ARCHITECTURE_0.1.3.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_ARCHITECTURE_0.1.3.md)
- [F5]: [0.1.6.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/releases/0.1.6.md)
- [F6]: [UBERHAR_ARCHITECTURE_0.1.6.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_ARCHITECTURE_0.1.6.md)
- [F7]: [UBERHAR_LOG_ADDENDUM_0.1.7.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ADDENDUM_0.1.7.md)
- [F7MAIN]: [UBERHAR_LOG_ANALYSIS_0.1.7.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.7.md)
- [F8]: [UBERHAR_LOG_ANALYSIS_0.1.8.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.8.md)
- [F9]: [UBERHAR_ARCHITECTURE_0.1.9.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_ARCHITECTURE_0.1.9.md)
- [F12]: [UBERHAR_LOG_ANALYSIS_0.1.12.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.12.md)
- [F13]: [UBERHAR_ARCHITECTURE_0.1.13.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_ARCHITECTURE_0.1.13.md)
- [F14]: [UBERHAR_LOG_ANALYSIS_0.1.14.md](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.14.md)
- [F38]: REVIEW.md (source not included in this compact archive)
- [F43]: REPORT.md (source not included in this compact archive)
- [H54]: REPORT.md (source not included in this compact archive)
- [F54FRESH]: REPORT.md (source not included in this compact archive)
- [F55]: REPORT.md (source not included in this compact archive)
- [F56]: REPORT.md (source not included in this compact archive)
- [F56REPEAT]: REPORT.md (source not included in this compact archive)


.38 accepted numeric summary: summary.json (source not included in this compact archive). .49–.53 provenance and original reduced records: HISTORY.json (source not included in this compact archive). .55 All-Core and each low/high CPU7/GPU context are embedded in [F55]; .56 has All-Core (source not included in this compact archive) and EXTREMA.md (source not included in this compact archive). .57/.58 source links, source SHA pins and per-scene resource populations remain in [CURRENT].

---

# Other games with verified 0.1.0 / 0.1.1 measurements

<!-- CodexAstraLocal: Inventory actual release-identified game data and retained later context without treating session numbers as versions, rescoring captures or merging unlike scenes. -->

Checked 2026-11-19. **Besides Fire Emblem Awakening, the qualifying games found are The Legend of Zelda: Ocarina of Time 3D and Sonic & All-Stars Racing Transformed at 0.1.0, plus Super Smash Bros. for Nintendo 3DS at 0.1.1.** Dark Moon is handled separately. These are real device measurements, not planned tests or host validation results.

The local release tags resolve to:

- `0.1.0` → `7af34fbfe61cdae68f598fb43a16178ec0b5ab78`.
- `0.1.1` → `74adc350830f65b4fe97f39810ae9c5316151893`.

The original exports independently print the matching short Git hash at line 5. Their `session=1`, `session=2`, etc. fields are run identifiers **within a release**, never version numbers. Despite the log field name “Azahar Version,” the branch/hash identifies Uberhar; these are **Native/Vulkan** results and not stock Azahar or Custom-mode results. All early rows below use CPU vertex JIT and CPU clock 100%.

## Earliest usable points

| Game | Actual release | Renderer / resolution | Measured speed | Duration / scope | Evidence |
|---|---|---|---:|---|---|
| Zelda: Ocarina of Time 3D | 0.1.0 | Native Vulkan 2x | **99.912%** | Normal-limit band, 293.666083 s, 17,555 system frames; maximum system-frame interval 148.949 ms | [O0] |
| Zelda: Ocarina of Time 3D | 0.1.0 | Native Vulkan 2x | **374.628%** | Separate 400%-cap band, 16.127966 s, 3,615 system frames; maximum interval 17.600 ms | [O0] |
| Sonic & All-Stars Racing Transformed | 0.1.0 | Native Vulkan 2x | **44.512 / 53.270 / 62.462%** low / mean / high | 17 selected complete racing windows, 85.342 s; header-relative window endpoints 95.49–175.80 s | [S0] |
| Sonic & All-Stars Racing Transformed | 0.1.0 | Native Vulkan 1x | **49.852 / 69.037 / 95.102%** low / mean / high | 23 later complete racing windows, 115.360 s; endpoints 200.09–310.41 s | [S0] |
| Super Smash Bros. for Nintendo 3DS | 0.1.1 | Native Vulkan 2x | **99.718%** | Normal-limit band, 393.697451 s, 23,489 system frames; maximum system-frame interval 231.001 ms | [B1] |

Ocarina's whole-session 114.223% mixes normal and fast play; keep the two bands above separate. Sonic's whole-session 74.667% over 328.444640 s mixes menus/loading and both resolutions; it is not a sustained 2x racing result. Sonic's selected 2x and 1x cohorts also cover different laps/scenes, so they do not prove a controlled resolution speedup. Smash's mean is a whole normal band; no scene-only mean or five-second minimum is invented here.

Additional actual Sonic .1 data exists: the September 27 evening export (source not included in this compact archive) records Native 2x normal-band **65.102% across 313.572255 s**, 12,214 system frames, max interval 238.250 ms (run header line 143). This is a whole normal band, not a matched continuation of the .0 selected race cohort. The earlier .1 FEA/Sonic export starts a Sonic session at line 2164 but has no final speed-band record for it; no aggregate is reconstructed.

## Sonic later history suitable for labeled context

The low/high values in this table are **lowest/highest approximately five-second averages**, not brief-dip minima/maxima or one-second observations. Cohort boundaries, limit settings, driving, diagnostic settings and old driver identity differ. These are historical observations, not matched build-speedup estimates. Original .48 and CPU-only .48 remain distinct builds.

| Release / cohort | Renderer / resolution | Low / mean / high speed % | Source |
|---|---|---:|---|
| 0.1.0 earlier race, 17 windows | Native 2x | 44.512 / 53.270 / 62.462 | [S0] |
| Original 0.1.48, session 8, post-marker/onset cohort | Native full ON 2x | 60.73 / 74.92 / 89.69 | [L] |
| CPU-only 0.1.48, session 6, post-marker/onset cohort | Native full ON 2x | 58.57 / 75.00 / 111.14 | [L] |
| 0.1.52, session 5, post-marker/onset cohort | Native full ON 2x | 57.61 / 74.65 / 100.49 | [L] |
| 0.1.52, session 6, post-marker/onset cohort | Native full ON 2x | 60.38 / 73.20 / 90.41 | [L] |
| Original 0.1.48, session 13, post-marker/onset cohort | Combo CPU vertices OFF 2x | 96.08 / 109.30 / 131.51 | [L] |
| CPU-only 0.1.48, session 1, post-marker/onset cohort | Combo CPU vertices OFF 2x | 99.48 / 114.90 / 130.49 | [L] |
| 0.1.52, session 2, post-marker/onset cohort | Combo CPU vertices OFF 2x | 97.78 / 119.25 / 165.12 | [L] |
| 0.1.57, R1, photographed road / first two laps | Native stock 2x | 58.48 / 71.06 / 83.28 | [S57] |
| 0.1.57, R3, photographed road / first two laps | Combo CPU vertices OFF, stock 2x | 104.61 / 113.23 / 122.53 | [S57] |
| 0.1.57, R4, photographed road / first two laps | Combo CPU vertices OFF, Turnip v27 2x | 112.57 / 119.26 / 128.56 | [S57] |

The .57 source lives under a `build058-in-app-tuning` directory, but the retained first-six report explicitly identifies **Sonic .57**. It is not a .58 measurement. Those .57 rows use a stable 400% cap, OGS and six new work flags OFF, Detailed GPU pass timing ON, empty application shader-cache inventories, and unknown driver-internal cache coldness. The .0 selected race was normal-limit play. Do not join Native and Combo into a single renderer series, or infer that all later gains came from the version alone. The broader .57 report retains separate final-water portions and settings candidates; those should stay separate from the road cohort.

No .58 Sonic test was performed in the reviewed .58 campaign. No modern Ocarina or Smash comparison point was located in the retained reports reviewed for this bounded pass. Ocarina and Smash therefore support an **early reference**, not a demonstrated .0/.1-to-.58 improvement chart. This is a search result, not proof these titles were never tested later. A later release's request to test Smash does not count as data.

## Evidence anchors

- **[O0]** [0.1.0 review](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.0.md) identifies the release/hash and both exports; Ocarina results begin at line 48. Original shared Ocarina/Sonic export (source not included in this compact archive): hash line 5, title `0004000000033500` and Native/Vulkan/2x launch line 135, normal and fast shutdown bands lines 1106 and 1107. Generic fragment family count 1, 24 pipelines and 132.196 ms generic wait are retained in review lines 54–56; zero skipped draws. These timing counters are not speed measures.
- **[S0]** [Sonic .0 review](https://github.com/RegiRex/uberhar/blob/e9c71e94a792d1b7845197ba02ca588d7fd7f7a7/docs/UBERHAR_LOG_ANALYSIS_0.1.0.md): title `00040000000B3500`; selected-window table lines 83–88, mixed-cohort warning 90–94. Original shared export has launch at line 1240 and whole normal band at 15571. Three generic families and 33 pipelines finish by 72.832 s; 612.534 ms total foreground generic wait, 116.060 s broad CPU vertex-stage work, zero skipped draws. This supports the report's sustained-workload interpretation, not a hardware GPU timing claim.
- **[B1]** Original Sonic/Smash .1 export (source not included in this compact archive): hash line 5; Smash title `00040000000EDF00` machine/loader identity lines 1431–1432, Native/Vulkan/2x launch line 1437; shutdown totals line 2793, normal band line 2795, worst-frame records 2796–2803. All-limit normal 100%, zero temporary frames or transition intervals. The 231.001 ms interval is a system-frame gap, not an instantaneous speed percentage or measured screen-presentation latency.
- **[L]** Retained later-version synthesis (source not included in this compact archive) contains the .48/.52 Sonic 2x cohort table. Its accompanying JSON (source not included in this compact archive) preserves original source paths, source hashes, narrower confirmed-race subsets and resource context. Historical loaded-driver matching remains unresolved.
- **[S57]** First-six report identifies release .57 (source not included in this compact archive), scope lines 3–14, admission/settings lines 62–65. Frozen 27-run summary (source not included in this compact archive) supplies more precise road/final-water rows at lines 11–18. Its R1/R3/R4 labels are run numbers, not releases.

Scope checked: all root-level original text-export headers in `source reference omitted`, then exact run and final-band records only from the four exports matching the .0/.1 hashes; relevant release/log/architecture documents; retained .48/.52 synthesis and already available .57 summaries. The matching .0 exports contain FEA, Ocarina and Sonic; the matching .1 exports contain FEA, Sonic and Smash. No additional .0/.1 title was present in those exports. No device actions, builds, raw-window rescoring, source mutation or broad capture scan was performed.

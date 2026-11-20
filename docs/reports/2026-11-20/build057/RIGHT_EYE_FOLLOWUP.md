<!-- CodexAstraLocal: Final performance report, sanitized for the owner-authorized public archive. -->
> Public report copy, archived November 20, 2026. Measurements and historical qualifications are retained. Private paths, device identifiers and operational closeout details are omitted; raw captures were deleted during the authorized storage cleanup. References to retained raw evidence describe the original analysis, not its present availability. This archive does not authorize device operations or settings restoration.

# Uberhar 0.1.57 — right-eye and Guest 3D follow-up report

<!-- CodexAstraLocal: Final offline report; capture closed, settings preserved. -->

**The 1% Guest 3D Slider is a useful optimization target, but it is not a generally better setting yet.** Fire Emblem gains substantial map throughput while its battle pacing deteriorates in both tested profiles. Xenoblade shows a more favorable battle-low and pacing tradeoff. The right-eye controls omitted no rendering work in either game, so none of those changes demonstrates successful removal of the hidden eye.

**Practical choices from this batch**

| Game | Working recommendation | Evidence and tradeoff |
|---|---|---|
| Fire Emblem | Keep THREE with Guest 3D 0% as the stability reference; retain 1% as an experimental comparison. | After collector repair, THREE T2→T3 map Overall rises 163.8%→230.1%, while the consistently defined battle-overlap Overall falls 96.1%→82.1%. FOUR also gains map speed but has a 45.0% lowest five-second battle average at T3. |
| Xenoblade | Keep prior FIVE as the balanced reference; retain T3 as the promising battle/pacing variant. | T3 battle Averages Low improves 53.8%→73.6% against prior, while stationary Overall is 128.0% versus 135.1%. Its stationary intervals ≥50 ms are zero. Collector/shot differences qualify the exact gain. |
| Dark Moon | Keep FOUR with both right-eye controls OFF. | The title compatibility veto prevents suppression. T2 saves no lists/draws/vertices; opening, first-lab and ghost/moon means are lower, while the late-lab mean is slightly higher with different endpoint/clock context. |

These are recommendations for the next comparisons, not settings applied to the Thor. Driver and renderer remain separate choices. No global feature removal follows from one game's results.

**Fire Emblem: the important distinction is sustained speed versus brief stalls.** In THREE's consistently defined battle-overlap windows, T2→T3 increases the longest interval from 62.37 ms to 105.52 ms; its cadence-equivalent low falls from 26.8% to 15.8%. Intervals ≥50 ms rise from 0.52% to 9.20%. FOUR T3 reaches 138.50 ms, a 12.1% cadence equivalent, with 19.07% of battle-overlap intervals ≥50 ms. Strict T3 battle windows independently show the same bad pacing direction (17.18% and 16.47% ≥50 ms for THREE and FOUR). Battle attacks and support outcomes differ, so the exact magnitude is qualified.

The map gains use more CPU work: THREE T2→T3 rises from about 0.84 to 1.56 app core-equivalents, with whole-device CPU7 rising 65.4%→88.9%. GPU busy remains high, 99.3%→96.1%. In battle, app demand remains around 0.6–0.7 cores while GPU busy stays near 99%. This supports a GPU-limited combat problem and a different, faster map workload; it does not support simply adding CPU workers as the solution to those battle lows.

Typed primitive append does not establish a clear overall Fire Emblem advantage here. FOUR map means are lower than THREE at 0%, but higher in the retained 1% sample; strict T3 battle means are essentially equal while FOUR has the worse observed brief stall and sustained low. Different map coverage and combat outcomes limit an isolated Typed attribution. Its usefulness in Sonic remains a separate finding.

**Xenoblade: the improved sustained battle low does not eliminate the worst individual hitch.** Prior FIVE versus T3 has a longest battle interval of 128.19 versus 126.62 ms—nearly unchanged—while intervals ≥50 ms fall from 3.51% to 2.93%. The favorable signal is fewer long intervals and a better sustained low, rather than a dramatic improvement to the worst single interval. T3's stationary gameplay also has fewer long intervals, though its mean throughput remains below prior FIVE.

**Dark Moon remains a different scheduling problem.** Its opening uses about 2.5–2.6 app core-equivalents and roughly 97–98% of whole-device CPU7, while GPU busy averages only about 42–45%. The other larger cores are already contributing, but the prime-thread path remains pressured. The late-lab tradeoff and the uneven clock samples should not be used to claim either right-eye setting improved this game.

**The workload evidence supports further 3D investigation, not an assumption that every extra call belongs to the right eye.** Fire Emblem command lists per guest submission rise from 5→6 on the map and 11→15 in battle. Delivered vertex lower bounds roughly double on the map and rise by about 55–59% in battle. The list count itself does not double. Xenoblade's latest per-callback geometry counts do not show the same doubling. Neither the proposed lower-texture-resolution explanation nor a universal 800→400 rendering-width change has been established by these captures.

**Recommended next implementation priorities**

1. Make the current suppression rejection cheap: test known disabled/title/layout prerequisites before expensive bookkeeping, and expose specific rejection reasons. This prevents a rejected optimization from doing unnecessary CPU work.
2. Develop the architect's versioned render-region/transfer provenance so that proven unused right-eye geometry can be rejected before vertex preparation and deferred work. Keep command/state effects and shared-resource dependencies intact. Start with a test toggle and measurable work counters; do not blindly drop alternating draws or half of a batch.
3. Preserve the 1% map workload while targeting the additional combat work. Validate reductions in lists, vertices, passes and intervals ≥50 ms against the existing 0% and 1% references; averages alone are insufficient.
4. Repair diagnostic delivery before another large testing cycle: ordinary-log rotation instead of the 100 MiB stop, visible omission accounting, bounded aggregate census output, and shortest-interval/paired guest-time measurements for an honest brief peak. Avoid increasing per-draw logging volume.

No new implementation or build was performed for this report. The architecture investigation below separates directly supported repairs from experimental work requiring hardware validation.

**How to read the speed figures**

All speeds are percentages of normal emulation speed. **Averages Low / Overall / High** means the lowest retained approximately five-second mean, the duration-weighted mean of retained windows, and the highest retained five-second mean. These are observed ranges, not confidence intervals. Paused/menu/mixed-limit windows are excluded where required by the recorded phase admissions.

**Brief low** is a nominal system-frame cadence estimate: `1671.368087504493 / longest_interval_ms`. The actual longest interval is also shown. This exposes brief stalls that a five-second mean hides, but it is not measured instantaneous guest speed or Android display FPS. **Brief Top is unavailable in every run:** the installed logger did not retain the shortest interval or its guest-time advance. Averages High is retained separately, never relabeled as a brief peak.

The ≥50 ms fraction counts slow intervals, not the percentage of wall time spent slow. Resource low/high contexts surround the lowest/highest five-second averages; the capture cannot assign CPU/GPU utilization to an exact individual dip.

**Settings and test identities**

**THREE:** Adaptive CPU workers, Prepare displayed eye only, Optimized Generic Shaders. **FOUR:** THREE + Typed primitive append. **FIVE:** FOUR + Render-thread delay 100 µs. Renderer, resolution and driver remain separate factors. Both Detailed GPU pass timing and Frame workload census were ON for this follow-up.

| Variant | Disable Right Eye Render | Early right-eye suppression | Guest 3D Slider |
|---|---|---|---|
| Prior reference | OFF | OFF | 0% |
| T1 | ON | OFF | 0% |
| T2 | ON | ON | 0% |
| T3 | ON | ON | 1% |

Fire Emblem: stock driver, Combo CPU Vertices OFF, 4x, THREE/FOUR. Xenoblade: Gen8 V38, Combo CPU Vertices OFF, 2x, FIVE. Dark Moon: v27, Combo CPU Vertices OFF, 2x, FOUR; only the requested T2 follow-up. Linear Filtering and simulated 3DS GPU timing remain ON. Standard mode and Wi-Fi OFF were recorded. Application shader caches were cold; driver-internal caches were not independently reset.

**Fire Emblem Awakening: scene performance**

† Battle overlap is a separately reported sensitivity: pure-400% windows with at least 70% overlap with the photographed battle interval. It may include transitions and is not pooled with the stricter battle row. Two runs have no complete strictly bounded battle window, but their captured combat remains available in these qualified rows. Cinematics were too brief/sparsely photographed to isolate complete comparable five-second windows; they are not given invented scene scores.

| Profile | Scene | Windows | Averages Low | Overall | High | Brief low estimate | Longest interval ms | Intervals ≥50 ms |
|---|---|---|---|---|---|---|---|---|
| THREE T1 | Map | 2 | 155.0 | 158.0 | 160.9 | 35.6 | 46.90 | 0.00% |
| THREE T1 | Battle | 2 | 82.8 | 86.5 | 90.2 | 23.9 | 69.88 | 0.77% |
| THREE T1 | Battle overlap† | 3 | 82.8 | 98.2 | 121.5 | 23.9 | 69.88 | 0.57% |
| THREE T2 | Map | 1 | 163.8 | 163.8 | 163.8 | 38.7 | 43.14 | 0.00% |
| THREE T2 | Battle | 0 | N/A | N/A | N/A | N/A | N/A | N/A% |
| THREE T2 | Battle overlap† | 2 | 90.4 | 96.1 | 101.7 | 26.8 | 62.37 | 0.52% |
| THREE T3 | Map | 3 | 210.6 | 230.1 | 243.5 | 37.0 | 45.19 | 0.00% |
| THREE T3 | Battle | 2 | 60.0 | 64.9 | 69.9 | 15.8 | 105.52 | 17.18% |
| THREE T3 | Battle overlap† | 3 | 60.0 | 82.1 | 116.5 | 15.8 | 105.52 | 9.20% |
| FOUR T1 | Map | 2 | 140.3 | 149.0 | 157.6 | 40.3 | 41.44 | 0.00% |
| FOUR T1 | Battle | 0 | N/A | N/A | N/A | N/A | N/A | N/A% |
| FOUR T1 | Battle overlap† | 2 | 78.8 | 88.5 | 98.2 | 21.8 | 76.67 | 1.32% |
| FOUR T2 | Map | 1 | 158.7 | 158.7 | 158.7 | 35.3 | 47.40 | 0.00% |
| FOUR T2 | Battle | 1 | 76.1 | 76.1 | 76.1 | 23.7 | 70.60 | 1.32% |
| FOUR T2 | Battle overlap† | 1 | 76.1 | 76.1 | 76.1 | 23.7 | 70.60 | 1.32% |
| FOUR T3 | Map | 1 | 244.0 | 244.0 | 244.0 | 41.1 | 40.66 | 0.00% |
| FOUR T3 | Battle | 3 | 45.0 | 64.8 | 91.2 | 12.1 | 138.50 | 16.47% |
| FOUR T3 | Battle overlap† | 4 | 45.0 | 63.0 | 91.2 | 12.1 | 138.50 | 19.07% |

Fire Emblem Awakening sustained speed and brief lows (source not included in this compact archive)

**CPU/GPU context**

App cores are attributed to Uberhar. Individual CPU utilization is whole-device activity and does not identify worker placement. N/A is preserved for invalid or missing counters.

| Profile | Scene | Mean app cores | Mean CPU7 % | Mean GPU % | Low: CPU7 / GPU % | High: CPU7 / GPU % |
|---|---|---|---|---|---|---|
| THREE T1 | Map | 0.82 | 66.2 | 99.1 | 67.5 / 99.1 | 64.9 / 99.2 |
| THREE T1 | Battle | 0.62 | 56.1 | 99.1 | 56.2 / 99.2 | 56.0 / 99.0 |
| THREE T1 | Battle overlap† | 0.72 | 59.3 | 98.3 | 56.2 / 99.2 | 65.9 / 96.8 |
| THREE T2 | Map | 0.84 | 65.4 | 99.3 | 65.4 / 99.3 | 65.4 / 99.3 |
| THREE T2 | Battle | N/A | N/A | N/A | N/A / N/A | N/A / N/A |
| THREE T2 | Battle overlap† | 0.72 | 53.4 | 97.8 | 50.4 / 98.1 | 56.4 / 97.5 |
| THREE T3 | Map | 1.56 | 88.9 | 96.1 | 83.0 / 91.2 | 90.8 / 98.9 |
| THREE T3 | Battle | 0.70 | 53.9 | 98.4 | 57.4 / 98.4 | 50.3 / 98.4 |
| THREE T3 | Battle overlap† | 0.85 | 58.6 | 98.3 | 57.4 / 98.4 | 68.0 / 98.1 |
| FOUR T1 | Map | 1.04 | 76.6 | 96.5 | 77.5 / 98.6 | 75.6 / 94.4 |
| FOUR T1 | Battle | N/A | N/A | N/A | N/A / N/A | N/A / N/A |
| FOUR T1 | Battle overlap† | 0.66 | 53.0 | 99.0 | 49.0 / 98.7 | 56.9 / 99.2 |
| FOUR T2 | Map | 0.87 | 65.7 | 99.4 | 65.7 / 99.4 | 65.7 / 99.4 |
| FOUR T2 | Battle | 0.63 | N/A | 98.8 | N/A / 98.8 | N/A / 98.8 |
| FOUR T2 | Battle overlap† | 0.63 | N/A | 98.8 | N/A / 98.8 | N/A / 98.8 |
| FOUR T3 | Map | 1.63 | 91.7 | 96.0 | 91.7 / 96.0 | 91.7 / 96.0 |
| FOUR T3 | Battle | 0.70 | 55.1 | 98.7 | 47.4 / 99.5 | 64.5 / 98.4 |
| FOUR T3 | Battle overlap† | 0.67 | 53.4 | 98.3 | 47.4 / 99.5 | 64.5 / 98.4 |

Fire Emblem Awakening all-core utilization at mean, low and high (source not included in this compact archive)

Exact per-core values, raw invalid counters, clock/temperature context and provenance are retained in the resource CSVs linked below.

**Xenoblade Chronicles 3D: scene performance**

| Profile | Scene | Windows | Averages Low | Overall | High | Brief low estimate | Longest interval ms | Intervals ≥50 ms |
|---|---|---|---|---|---|---|---|---|
| Prior FIVE | Opening cinematic | 6 | 147.4 | 202.1 | 252.7 | 25.7 | 64.99 | 0.17% |
| Prior FIVE | Battle cinematic | 30 | 53.8 | 109.4 | 163.2 | 13.0 | 128.19 | 3.51% |
| Prior FIVE | Stationary gameplay | 7 | 126.6 | 135.1 | 141.9 | 37.1 | 45.11 | 0.00% |
| T1 | Opening cinematic | 6 | 139.1 | 212.2 | 260.5 | 26.4 | 63.29 | 0.16% |
| T1 | Battle cinematic | 30 | 56.5 | 107.7 | 158.4 | 12.8 | 130.10 | 4.26% |
| T1 | Stationary gameplay | 7 | 124.8 | 128.1 | 131.6 | 32.3 | 51.72 | 0.04% |
| T2 | Opening cinematic | 7 | 157.3 | 196.3 | 252.3 | 28.0 | 59.68 | 0.12% |
| T2 | Battle cinematic | 30 | 54.0 | 107.4 | 160.8 | 13.6 | 122.94 | 3.77% |
| T2 | Stationary gameplay | 6 | 111.7 | 114.1 | 118.3 | 27.9 | 59.90 | 0.44% |
| T3 | Opening cinematic | 6 | 147.2 | 198.2 | 242.0 | 19.5 | 85.83 | 0.17% |
| T3 | Battle cinematic | 29 | 73.6 | 112.4 | 160.7 | 13.2 | 126.62 | 2.93% |
| T3 | Stationary gameplay | 7 | 119.9 | 128.0 | 133.9 | 39.6 | 42.23 | 0.00% |

Xenoblade Chronicles 3D sustained speed and brief lows (source not included in this compact archive)

**CPU/GPU context**

App cores are attributed to Uberhar. Individual CPU utilization is whole-device activity and does not identify worker placement. N/A is preserved for invalid or missing counters.

| Profile | Scene | Mean app cores | Mean CPU7 % | Mean GPU % | Low: CPU7 / GPU % | High: CPU7 / GPU % |
|---|---|---|---|---|---|---|
| Prior FIVE | Opening cinematic | 1.04 | 72.9 | 98.2 | 77.5 / 99.2 | 64.5 / 99.1 |
| Prior FIVE | Battle cinematic | 1.05 | 73.2 | 90.7 | 47.2 / 99.3 | 92.1 / 84.8 |
| Prior FIVE | Stationary gameplay | 1.43 | 92.5 | 94.5 | 90.4 / 97.5 | 93.1 / 94.8 |
| T1 | Opening cinematic | 1.05 | 73.4 | 97.0 | 75.5 / 97.9 | 69.3 / 97.8 |
| T1 | Battle cinematic | 1.04 | 73.2 | 94.1 | 69.3 / 95.2 | 92.4 / 94.7 |
| T1 | Stationary gameplay | 1.36 | 89.4 | 97.1 | 91.6 / 98.5 | 86.5 / 97.1 |
| T2 | Opening cinematic | 1.06 | 73.3 | 97.4 | 89.1 / 94.9 | 75.4 / 98.4 |
| T2 | Battle cinematic | 1.06 | 74.4 | 90.6 | 66.9 / 82.6 | 89.8 / 83.1 |
| T2 | Stationary gameplay | 1.25 | 84.8 | 98.1 | 83.9 / 98.6 | 85.7 / 98.1 |
| T3 | Opening cinematic | 1.10 | 75.1 | 97.3 | 79.1 / 98.8 | 70.5 / 98.3 |
| T3 | Battle cinematic | 1.13 | 77.9 | 91.0 | 55.9 / 95.1 | 79.4 / 98.3 |
| T3 | Stationary gameplay | 1.43 | 92.8 | 87.3 | 91.2 / 88.6 | 92.4 / 87.1 |

Xenoblade Chronicles 3D all-core utilization at mean, low and high (source not included in this compact archive)

Exact per-core values, raw invalid counters, clock/temperature context and provenance are retained in the resource CSVs linked below.

**Luigi’s Mansion: Dark Moon: scene performance**

| Profile | Scene | Windows | Averages Low | Overall | High | Brief low estimate | Longest interval ms | Intervals ≥50 ms |
|---|---|---|---|---|---|---|---|---|
| Prior FOUR | Opening moon | 2 | 66.0 | 69.3 | 72.6 | 24.6 | 67.89 | 3.61% |
| Prior FOUR | First laboratory | 5 | 70.7 | 87.9 | 98.2 | 30.9 | 54.09 | 0.23% |
| Prior FOUR | Ghost / moon | 4 | 77.3 | 82.6 | 88.2 | 25.2 | 66.22 | 0.30% |
| Prior FOUR | Second laboratory | 2 | 86.8 | 87.6 | 88.4 | 21.2 | 79.02 | 1.14% |
| T2 | Opening moon | 2 | 59.7 | 64.8 | 70.0 | 26.0 | 64.37 | 2.31% |
| T2 | First laboratory | 5 | 59.8 | 83.5 | 92.1 | 27.4 | 60.91 | 0.40% |
| T2 | Ghost / moon | 4 | 73.5 | 80.0 | 85.3 | 25.0 | 66.78 | 0.31% |
| T2 | Second laboratory | 2 | 88.1 | 89.4 | 90.8 | 29.0 | 57.71 | 0.19% |

Luigi’s Mansion: Dark Moon sustained speed and brief lows (source not included in this compact archive)

**CPU/GPU context**

App cores are attributed to Uberhar. Individual CPU utilization is whole-device activity and does not identify worker placement. N/A is preserved for invalid or missing counters.

| Profile | Scene | Mean app cores | Mean CPU7 % | Mean GPU % | Low: CPU7 / GPU % | High: CPU7 / GPU % |
|---|---|---|---|---|---|---|
| Prior FOUR | Opening moon | 2.54 | 97.0 | 45.3 | 98.3 / 60.3 | 95.8 / 30.3 |
| Prior FOUR | First laboratory | 2.02 | 92.4 | 78.6 | 94.5 / 87.0 | 91.4 / 79.6 |
| Prior FOUR | Ghost / moon | 2.16 | 93.4 | 69.8 | 94.8 / 55.8 | 91.5 / 82.7 |
| Prior FOUR | Second laboratory | 1.70 | 86.1 | 71.6 | 82.4 / 58.2 | 89.9 / 84.9 |
| T2 | Opening moon | 2.60 | 97.5 | 41.8 | 98.8 / 50.7 | 96.2 / 32.8 |
| T2 | First laboratory | 2.08 | 93.0 | 79.8 | 98.3 / 83.0 | 90.7 / 76.6 |
| T2 | Ghost / moon | 2.14 | 93.6 | 71.3 | 96.2 / 68.5 | 92.9 / 81.6 |
| T2 | Second laboratory | 1.86 | 92.4 | 76.5 | 92.5 / 74.6 | 92.3 / 78.5 |

Luigi’s Mansion: Dark Moon all-core utilization at mean, low and high (source not included in this compact archive)

Exact per-core values, raw invalid counters, clock/temperature context and provenance are retained in the resource CSVs linked below.

**Sequential changes and profile comparisons**

The reviewed comparison exports distinguish speed percentage points from relative percentage changes. For example, 114.1% → 128.0% is +13.9 points and +12.2% relative. Do not interpret differences between unlike cameras, battle outcomes or collector workloads as isolated causal effects.

FEA detailed comparisons and qualifications (source not included in this compact archive)

XDM detailed comparisons and qualifications (source not included in this compact archive)

The earlier requested comparisons remain part of this report set: all 15 THREE/FOUR/FIVE cross-game runs, reanalyzed for brief stalls (source not included in this compact archive), and the initial owner Fire Emblem runs versus automated runs (source not included in this compact archive). Those initial owner runs had different feature/diagnostic combinations, so they are not substituted for the six controlled right-eye follow-ups.

**Fire Emblem sequential and THREE→FOUR comparisons**

Battle rows below use the same explicitly labeled overlap rule on both sides. They are sensitivity comparisons, not exact enemy-attack matches.

| Scene | Change | Low change | Overall change | High change |
|---|---|---|---|---|
| Map | THREE T1 → THREE T2 | 5.64% | 3.67% | 1.77% |
| Battle overlap† | THREE T1 → THREE T2 | 9.23% | -2.13% | -16.30% |
| Map | THREE T2 → THREE T3 | 28.61% | 40.51% | 48.66% |
| Battle overlap† | THREE T2 → THREE T3 | -33.66% | -14.53% | 14.60% |
| Map | THREE T1 → THREE T3 | 35.86% | 45.67% | 51.29% |
| Battle overlap† | THREE T1 → THREE T3 | -27.53% | -16.35% | -4.08% |
| Map | FOUR T1 → FOUR T2 | 13.07% | 6.53% | 0.68% |
| Battle overlap† | FOUR T1 → FOUR T2 | -3.44% | -14.01% | -22.50% |
| Map | FOUR T2 → FOUR T3 | 53.77% | 53.77% | 53.77% |
| Battle overlap† | FOUR T2 → FOUR T3 | -40.85% | -17.21% | 19.86% |
| Map | FOUR T1 → FOUR T3 | 73.87% | 63.80% | 54.82% |
| Battle overlap† | FOUR T1 → FOUR T3 | -42.89% | -28.81% | -7.11% |
| Map | THREE T1 → FOUR T1 | -9.49% | -5.72% | -2.07% |
| Battle overlap† | THREE T1 → FOUR T1 | -4.80% | -9.85% | -19.20% |
| Map | THREE T2 → FOUR T2 | -3.12% | -3.12% | -3.12% |
| Battle overlap† | THREE T2 → FOUR T2 | -15.85% | -20.80% | -25.19% |
| Map | THREE T3 → FOUR T3 | 15.83% | 6.02% | 0.21% |
| Battle overlap† | THREE T3 → FOUR T3 | -24.97% | -23.28% | -21.75% |

Fire Emblem deltas with exact percentage-point changes (source not included in this compact archive)

| Game / scene | Change | Low change | Overall change | High change |
|---|---|---|---|---|
| xenoblade / Opening cinematic | Prior FIVE → T1 | -5.61% | 5.01% | 3.10% |
| xenoblade / Battle cinematic | Prior FIVE → T1 | 4.92% | -1.55% | -2.92% |
| xenoblade / Stationary gameplay | Prior FIVE → T1 | -1.41% | -5.17% | -7.26% |
| xenoblade / Opening cinematic | Prior FIVE → T2 | 6.73% | -2.85% | -0.17% |
| xenoblade / Battle cinematic | Prior FIVE → T2 | 0.25% | -1.81% | -1.46% |
| xenoblade / Stationary gameplay | Prior FIVE → T2 | -11.79% | -15.53% | -16.64% |
| xenoblade / Opening cinematic | Prior FIVE → T3 | -0.12% | -1.91% | -4.22% |
| xenoblade / Battle cinematic | Prior FIVE → T3 | 36.71% | 2.75% | -1.52% |
| xenoblade / Stationary gameplay | Prior FIVE → T3 | -5.28% | -5.22% | -5.62% |
| darkmoon / Opening moon | Prior FOUR → T2 | -9.66% | -6.51% | -3.65% |
| darkmoon / First laboratory | Prior FOUR → T2 | -15.40% | -4.95% | -6.23% |
| darkmoon / Ghost / moon | Prior FOUR → T2 | -4.81% | -3.05% | -3.22% |
| darkmoon / Second laboratory | Prior FOUR → T2 | 1.46% | 2.07% | 2.68% |
| xenoblade / Opening cinematic | T1 → T2 | 13.08% | -7.49% | -3.17% |
| xenoblade / Battle cinematic | T1 → T2 | -4.46% | -0.26% | 1.50% |
| xenoblade / Stationary gameplay | T1 → T2 | -10.53% | -10.93% | -10.11% |
| xenoblade / Opening cinematic | T2 → T3 | -6.42% | 0.97% | -4.06% |
| xenoblade / Battle cinematic | T2 → T3 | 36.37% | 4.64% | -0.06% |
| xenoblade / Stationary gameplay | T2 → T3 | 7.38% | 12.20% | 13.21% |

Changes in this table are relative percentages of the observed speeds. The raw scene scores and exact percentage-point deltas remain in the CSV. No collector- or camera-adjusted scores are inferred.

**Guest 3D architecture investigation**

# Guest3D: preserve the faster guest path and remove proven unused work

**The1% setting changes the game's rendering workload. Both eye controls currently miss that workload. There is a plausible CPU-and-GPU optimization opportunity, but the retained evidence does not yet prove which extra draws can be removed.** The correct next change is a bounded target-region/consumer analysis feeding an earlier draw decision, together with an immediate cheap fix for rejected-eye bookkeeping. Do not force0%, drop alternate lists, or assume two similar passes are interchangeable.

## What is established

Guest3D0%/1% becomes0.0/0.01 in HID and the shared page. The guest game reads that value and chooses its own rendering path. Host mono display is separate; it can display one eye while the game generates stereo work. No universal slider/eye bit is added to translated shader keys: the same shader/state can reuse compiled code while running more geometry or pixels. Cheaper effects, different antialiasing/downsampling, projection or pass structure at1% remain possible explanations for the faster maps; **none is established as the specific cause**. HID (source not included in this compact archive), shared page (source not included in this compact archive), shader configuration (source not included in this compact archive).

The latest FEA evidence now separates increased guest work from merely doing more work per wall second:

| Same-endpoint measure | THREE0% →1% | FOUR0% →1% |
|---|---:|---:|
| Map command lists/top-screen submission |5→6|5→6|
| Battle command lists/submission |11→15|11→15|
| Map observed vertex lower bound/submission, T2→T3 |25,445→52,212|25,937→54,958|
| Battle observed vertex lower bound/submission, T2→T3 |97,220→150,141|98,914→150,015|

Thus map vertices roughly double, but command lists do **not**: they rise20% in maps and36% in battle. A list is not an eye or a constant-sized packet. These are the engineer's exact common-census-endpoint ratios, not a new reduction;3–8% missing census reports leave observed vertex lower bounds, and a top-screen submission is not universally one system frame. The recurring direction across THREE/FOUR supports a real guest-path change despite camera/attack differences. Current FEA diagnostic extraction (source not included in this compact archive).

Do not generalize the doubling to Xenoblade. Its latest T2→T3 stationary draws per delivered callback are321.10→329.34 and submitted vertices94,266→95,040; battle479.78→473.68 draws and86,661→85,003 vertices. Those callback-normalized counts are not guest-frame counts, but they do not show the same doubling. Its gains also occur with zero suppressed lists/transfers/draws. Xeno comparison (source not included in this compact archive).

## Why both controls miss it

**Original Disable Right Eye Render:** `GPU::SubmitCmdList` asks the heuristic before `ProcessCmdList`. It guesses viewport height and color-buffer base, remembers a top-screen transfer from that exact base, and suppresses a later matching list/transfer only in that ordering. The guess scans at most256 commands and a few properties; it is not a complete read/write or eye analysis. The reviewed FEA/Xeno runs evaluate many lists but skip none. Whole-list rejection is genuinely early enough to avoid host parsing, vertex execution and drawing, but it also bypasses all state commands in that list; extending its guesses without state/dependency analysis would be unsafe. It cannot undo game CPU work already spent constructing the list. Heuristic (source not included in this compact archive), submission (source not included in this compact archive), skip branch (source not included in this compact archive), limited guess (source not included in this compact archive).

**Early right-eye suppression:** this accepts only a whole direct right-scanout target, matching format/stride/size, distinct from left buffers, with no depth/stencil writes, guest geometry shader, shadows, texture feedback or overlapping ranges. It has no subrectangle input. Its `Disabled` classification combines mono-left, original-setting, title-compatibility and representation checks. Latest FEA sessions3/4 and Xeno T3 startup records show render3D OFF, mono-left, original-right ON and compatibility allowed; under those recorded stable settings, the remaining failed Disabled operand is `matching_layout`—render-target format mapping and/or LCD stride. The exact per-draw failed operand is not recorded. Dark Moon is different: it has an explicit title veto, which must remain respected. Exact classifier (source not included in this compact archive), input construction (source not included in this compact archive), title permission (source not included in this compact archive).

This experiment is also later than its name suggests. CPU draws reach it only after vertex shader execution/assembly. Ready-GPU draws have already performed input analysis/uploads and shader preparation. Deferred CPU packets bypass it entirely. Consequently an accepted CPU-path omission would retain CPU vertex work; zero omissions produce no draw savings at all. Requested ON still performs descriptor/range bookkeeping before Disabled, with no isolated cost timer. CPU draw order (source not included in this compact archive), accelerated preparation (source not included in this compact archive), late omission (source not included in this compact archive).

**Prepare displayed eye only** is already a third, separate optimization. It omits preparation of an unconsumed final screen texture and keeps valid descriptors for presentation; it does not stop the game, PICA lists or offscreen rendering. Both active windows and screenshots have their own consumer requirements. Presentation preparation (source not included in this compact archive).

## The concrete missing link: regions, transfers and actual eye consumers

The earlier full Xeno census already found two equal-count480×800 host regions in one512×2048 attachment (guest256×1024), sharing color/depth and differing only in stacked viewport/scissor. Display transfers read offsets within that attachment and write separate scanout buffers. That explains why a whole-target direct-scanout check or exact-base transfer guess misses them. It does **not** prove which region is right-eye-only, that both regions are always eyes, or that a shared effect/depth consumer can be discarded. Retained target and transfer evidence (source not included in this compact archive).

A bounded next-build implementation can track this without a full application-wide render graph:

1. At `GPU::SetBufferSwap`, version the actual left/right destination addresses, active index, format and stride. Track both rotating buffers and both active display consumers. Reuse those real fields; there is no semantic eye identifier in `SubmitCmdListCommand`.
2. At existing display/texture-copy hooks, link destination **content version** to the exact source region and transfer representation/scaling. Convert tiled/rotated/offset source ranges using existing surface/transfer rules; a byte offset is not automatically a linear rectangle. Unknown conversion, overlap, format reinterpretation or partial write means unknown lineage.
3. At draw triggers, attach list/draw ordinal, current color/depth surface/subresource/version and viewport/scissor to that lineage. Record dependency reads from textures, blending/logic, depth/stencil, copies/readbacks and guest CPU accesses. Clears/fills, aliases, writes, address reuse and buffer/layout switches advance or invalidate versions. A stable address alone is not a stable image.
4. Keep a fixed small table for current candidate targets/rotating scanout versions, using existing generation/identity values rather than fresh full-state hashing. Emit compact bounded changes and aggregate refusal/coverage counters; optional sampled target/PSO/cost joins suffice. Avoid per-draw text and another huge per-callback log stream. Overflow/unknown lineage must fall back to normal rendering, never evict a live dependency silently.

This is the minimum useful provenance that current census lacks. Current descriptors aggregate counts, have limited texture slots and no ordered draw-to-consumer/resource-version join; repeating the same census cannot establish the missing proof. A reason split (`TitleVeto`, `NotMonoLeft`, `OriginalOff`, `FormatMismatch`, `StrideMismatch`, `NotDirectTarget`, `UnknownRegion`) should be cheap and accompany the tracker. Actual LCD fields (source not included in this compact archive), transfer hooks (source not included in this compact archive), submission payload (source not included in this compact archive), current census schema (source not included in this compact archive).

## Where to eliminate work, and what makes that safe

The useful early hook is **inside `PicaCore::DrawArrays`, after guest delay accounting and before ready-GPU preflight, `TryDeferredVertices`, loader/JIT setup and `LoadVertices`**. It can inspect already-decoded registers cheaply. Continue processing every command/register/uniform/LUT upload and list jump in order; omit only a draw proven unobservable. Preserve simulated GPU delays and completion interrupts rather than manufacture speed by removing guest waiting. This reaches CPU workers, uploads, optional shader/pipeline admission and GPU execution earlier than the current rasterizer hook. Hook location (source not included in this compact archive), deferred preparation (source not included in this compact archive), IRQ reconciliation (source not included in this compact archive).

Start with complete independent no-GS triangle work; refuse immediate draws, partial Strip/Fan assembly, pending winding, debug observers and unknown memory effects. Even complete CPU lists retain a trailing vertex pair, and deferred packets publish it in order. Skipping CPU execution must either prove that state unobservable or preserve/reconstruct it. An empty assembler alone is not proof; Reset even retains its vertex buffer. Already-published work cannot simply be canceled or reordered. Persistent state (source not included in this compact archive), tail adoption (source not included in this compact archive).

Most importantly, **absence of a read so far is not proof of no future read**. Current late hazard detection explicitly says already-omitted output cannot be recovered. To move beyond a narrowly proven complete workload, use bounded deferred materialization: preserve immutable input/state/resource versions, execute in original order before an unexpected read, completion/observable boundary, screenshot or capacity limit, and discard only an entire dead region version that is overwritten without any consumer. Reuse packet/ownership machinery where its contract fits, but current `CpuDrawPacket` already computes vertices and is not this proof by itself. Capturing snapshots costs time/memory; an early boundary forcing immediate materialization may yield no benefit. Keep this a narrow candidate with explicit fallback, not a promise of universally halving rendering.

An alternative is a title/version-specific proven pass contract validated against the actual current command/state sequence. It must not be a prior-frame address guess, an every-second-list rule or an empirical “no hazard happened last time” permission. If neither a bounded future-consumer proof nor reversible materialization is feasible at reasonable cost, retain the ordinary draw.

## Ranked next-build recommendation

| Priority | Concrete change | Acceptance / stopping condition |
|---|---|---|
|1|Early-return known Disabled reasons before descriptor work; skip empty omitted-range scans until the first omission.|Same output/state/timing; no cost claim beyond sampled evidence. Preserve every hazard check after any omission.|
|2|Compact versioned LCD→transfer→target-region tracker plus reason counters and selected route/cost joins.|One matched FEA map/enemy-attack0/1% workload identifies actual consumers, extra passes and quality changes without excessive log loss. Unknown remains visible.|
|3|Prototype proven-region admission at the early DrawArrays hook, initially shadow decisions, then narrowly enabled execution.|Actual avoided CPU invocations/uploads/draws plus intact state, depth/shared consumers and completion order. Retained1% map gain and better demanding-section intervals; fallback on ambiguity.|
|4|Independently trial wider compatible pass area for the observed stacked regions while preserving exact per-draw viewport/scissor.|First establish area-only boundary eligibility; maintain attachment validity, LOAD/STORE, hazards and queue cadence. This can reduce pass cost without discarding an eye, but larger areas can also hurt.|
|5|Only if guest CPU command construction remains dominant: targeted game-code patch preserving the1% quality/effect path but omitting a proven extra view.|Exact title/version/code identity, opt-in and visual/guest-state validation. Generic GSP HLE lacks that game-level semantic boundary.|

The fourth candidate reuses the existing finite pass-area review (source not included in this compact archive); total PassChange counts do not prove area-only cause. It does not replace the independently justified shader-residency/direct-SPIR-V cost work.

To avoid the extra **game CPU** camera/object/list-construction work, renderer/HLE rejection is necessarily too late: GSP receives a list the game has already built. A game-specific patch can retain the1% branch while suppressing a known second-view call, but no generic “right eye” bit exists at that submission boundary. Returning0% or toggling the slider per scene is not equivalent and risks losing the faster map path or changing image quality.

The desired outcome is useful and technically coherent: preserve the guest1% mode's favorable work choices and stop executing genuinely unused output. We have found the right boundary and the reason current controls miss it; we have **not** yet proved a new FEA/Xeno region safe to drop. The immediate low-risk bookkeeping fix and bounded provenance are implementation-ready; actual early draw elimination follows the stated proof/fallback contract.

Measurement remains as specified in METRIC_ARCHITECT_REVIEW (source not included in this compact archive): Averages Low/Overall/High are measured multi-second speeds; brief-low % is an explicitly nominal cadence estimate with milliseconds; precise brief Top % is unavailable. The10s/120s retention change, thermal clocks, sparse battle overlap and internal optional omissions stay attached to performance comparisons. No new device run, source edit or build was performed for this review. All15 inspected source files match published .57; hashes and exact evidence pointers are in GUEST_3D_ARCHITECTURE.json (source not included in this compact archive).


**Evidence and capture limits**

The older selected-profile batch used approximately 120-second growing-native copies; the initial right-eye follow-up used roughly 10-second copies/hashes. The collector was repaired during the owner THREE sequence: THREE T1 used 10-second checkpoints, THREE T2 crossed a startup copy burst, and THREE T3 plus all FOUR runs used 120-second growing checkpoints. Recorded native-copy events overlap one of two THREE T1 strict map windows and one of two strict battle windows; no other owner scored map/battle window has direct copy overlap. FOUR T1 also had a startup copy burst outside its scored windows. Actual overlap events, not nominal cadence alone, must qualify the comparisons. No estimated copying cost has been subtracted from scores. The interrupted automated Fire Emblem THREE/T2 run hit the native-file cap and is excluded from completed comparisons. The six owner runs have complete ordinary start/end evidence below the cap, but optional internal census records still dropped. Full file retention does not recover those missing records.

Metric/source review (source not included in this compact archive) · Collector overlap audit (source not included in this compact archive) · Exact owner collector timeline (source not included in this compact archive) · THREE completeness audit (source not included in this compact archive) · FOUR completeness audit (source not included in this compact archive)

Fire Emblem scene CSV (source not included in this compact archive) · Fire Emblem all-core/resource CSV (source not included in this compact archive) · Xenoblade/Dark Moon scene CSV (source not included in this compact archive) · Xenoblade/Dark Moon all-core/resource CSV (source not included in this compact archive) · Xenoblade/Dark Moon deltas (source not included in this compact archive) · Xenoblade/Dark Moon diagnostics (source not included in this compact archive)
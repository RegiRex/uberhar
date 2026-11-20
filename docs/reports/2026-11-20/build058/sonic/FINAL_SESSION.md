<!-- CodexAstraLocal: Final performance report, sanitized for the owner-authorized public archive. -->
> Public report copy, archived November 20, 2026. Measurements and historical qualifications are retained. Private paths, device identifiers and operational closeout details are omitted; raw captures were deleted during the authorized storage cleanup. References to retained raw evidence describe the original analysis, not its present availability. This archive does not authorize device operations or settings restoration.

# Sonic .58 and historical chart report

<!-- CodexAstraLocal: Captured evidence, metric scope and owner-requested track/resolution separation. -->

Completed 2026-11-20 UTC. Capture is stopped. No settings were restored, no new tests/builds run, and no sleep action taken.

The full chart books contain 210 non-Samba 2× observations, 60 non-Samba 4× observations, and 37 Samba observations on separate resolution/measurement pages. They are overlapping scene observations, not independent race counts. The raw table also preserves one historical 1× point outside these chart books.

[Complete 2× charts](../../charts/Sonic_2x_all_results.pdf) · [Complete 4× charts](../../charts/Sonic_4x_all_results.pdf) · [Separate Samba charts](../../charts/Sonic_Samba_all_results.pdf) · [All data CSV](../../history/SONIC_ALL_RESULTS.csv) · Full data/provenance JSON (source not included in this compact archive)

All speed values are percent of normal speed. Averages use complete approximately five-second windows: Low / Overall / High. Actual retained one-second extrema appear separately. Historical HUD and whole-band measurements are labelled separately in the full books; they are not substituted for missing one-second measurements.

Historical profiles, scene coverage, driver, cache, background load, temperature and diagnostics differ. Those rows are performance context rather than isolated build-effect estimates. Details and unresolved track aliases are retained in history notes (source not included in this compact archive).
## 2× — all current original-track settings runs

A identifies the earlier .58 capture; B identifies the final .58 capture. FOUR = Typed primitive append + Adaptive CPU workers + Prepare displayed eye only + Optimize generic shaders. FIVE = FOUR + render-thread delay100µs. THREE omits Typed primitive append. Bank8 and Guest3D0 are defaults unless specified. “State profile” = Reuse unchanged PICA state + Typed primitive append + Adaptive CPU workers + Optimize generic shaders + delay100µs + bank16; displayed-eye preparation OFF.

| Run | Renderer | Driver | Profile | Road laps1–2: Low / Overall / High | Final water: Low / Overall / High |
|---|---|---|---|---|---|
| A5 | Native | Stock | Baseline | 56.7 / 64.5 / 79.6 | 59.1 / 65.5 / 76.7 |
| A6 | Native | v27 | Baseline | 57.6 / 68.3 / 81.9 | 63.4 / 79.2 / 103.7 |
| A3 | Combo CPU Vertices OFF | Stock | THREE + delay100 | 94.7 / 103.4 / 113.1 | 99.1 / 111.5 / 132.5 |
| A4 | Combo CPU Vertices OFF | Stock | Baseline | 92.8 / 104.9 / 127.0 | 97.9 / 112.3 / 122.5 |
| A7 | Combo CPU Vertices OFF | v27 | Baseline | 100.9 / 109.7 / 118.4 | 102.7 / 112.0 / 124.6 |
| A8 | Combo CPU Vertices OFF | v27 | FIVE | 101.9 / 112.7 / 120.1 | 105.4 / 113.5 / 122.5 |
| A9 | Combo CPU Vertices OFF | v27 | FOUR | 97.3 / 108.7 / 117.7 | 105.0 / 111.4 / 115.4 |
| A10 | Combo CPU Vertices OFF | v27 | FOUR + slider1% | 94.2 / 99.7 / 107.8 | 98.4 / 105.2 / 108.8 |
| A11 | Combo CPU Vertices OFF | v27 | FIVE + slider1% | 95.0 / 101.4 / 109.6 | 102.0 / 109.2 / 120.4 |
| A12 | Combo CPU Vertices OFF | v27 | FIVE + bank16 + slider1% | 97.1 / 102.9 / 110.7 | 105.0 / 106.9 / 110.7 |
| A13 | Combo CPU Vertices OFF | v27 | FIVE + bank16 | 103.5 / 113.0 / 123.6 | 119.9 / 123.5 / 128.8 |
| A14 | Combo CPU Vertices OFF | v27 | FOUR + bank16 | 104.2 / 113.8 / 124.3 | 116.1 / 124.6 / 129.3 |
| A15 | Combo CPU Vertices OFF | v27 | FOUR + bank16 + Mono guest1% | 94.9 / 100.4 / 110.0 | 102.5 / 105.5 / 107.4 |
| A16 | Combo CPU Vertices OFF | v27 | All performance controls + census | 94.5 / 104.4 / 114.0 | 101.5 / 109.8 / 118.1 |
| A17 | Combo CPU Vertices OFF | v27 | State reuse + Typed + Adaptive + OGS + delay100 + bank16 | 106.1 / 117.8 / 128.0 | 119.0 / 119.9 / 120.9 |

A17 final-water statistics contain only two complete five-second windows. All these runs have pass timing ON; A16 alone has workload census ON. Full actual launch state is in the earlier capture manifest, linked through each row source.

## 2× — selected historical references

All retained alternatives are in the full chart book; this selection includes major milestones and all renderer blocks. “Earlier cohort” means the saved historical racing selection, not an exact match to today’s scene bounds.

| Build / run | Renderer | Driver | Scope | Low / Overall / High | App cores | GPU busy % |
|---|---|---|---|---|---|---|
| 0.1.0 original-Sonic | Native | Unrecorded | selected racing cohort 2x | 44.5 / 53.3 / 62.5 | — | — |
| 0.1.38 manual038-sonic-native-01 | Native | Unrecorded | post-marker race; owner break excluded | 50.0 / 70.4 / — | — | — |
| 0.1.43 owner43-Native | Native | Unrecorded | owner43-Native-pre-final | 42.5 / 51.7 / 59.8 | — | — |
| 0.1.48 s8 | Native | Unrecorded | s8 | 60.7 / 74.9 / 89.7 | 1.32 | 52.1 |
| 0.1.49 s3 | Native | Unrecorded | s3 | 55.9 / 70.7 / 102.6 | 1.33 | 53.2 |
| 0.1.50 G1.S4 | Native | Unrecorded | G1.S4 | 53.1 / 66.2 / 89.8 | 1.30 | 82.4 |
| 0.1.51 native_2x | Native | Unrecorded | native_2x_confirmed_later_racing | 61.0 / 73.2 / 105.5 | 1.28 | 86.9 |
| 0.1.52 S5 | Native | Unrecorded | visually_confirmed_race_interior | 59.4 / 74.0 / 86.7 | 1.32 | 52.3 |
| 0.1.55 S4 | Native | Stock | race | 59.1 / 74.4 / 112.0 | 1.30 | 46.8 |
| 0.1.56-r2 s17 | Native | Stock | race | 56.8 / 71.0 / 90.0 | 1.31 | 45.4 |
| 0.1.57 R1 | Native | Stock | Road (laps1–2) | 58.5 / 71.1 / 83.3 | 1.30 | 45.2 |
| 0.1.48 s13 | Combo CPU Vertices OFF | Unrecorded | s13 | 96.1 / 109.3 / 131.5 | 1.20 | 65.2 |
| 0.1.49 s7 | Combo CPU Vertices OFF | Unrecorded | s7 | 93.4 / 105.3 / 122.4 | 1.21 | 59.9 |
| 0.1.50 G1.S16 | Combo CPU Vertices OFF | Unrecorded | G1.S16 | 81.7 / 95.4 / 124.8 | 1.19 | 81.7 |
| 0.1.51 combo_cpu_off_2x | Combo CPU Vertices OFF | Unrecorded | combo_cpu_off_2x_confirmed_interior | 98.3 / 105.9 / 117.2 | 1.15 | 88.8 |
| 0.1.48 CPU-only S1 | Combo CPU Vertices OFF | Unrecorded | visually_confirmed_race_interior | 104.5 / 114.9 / 129.1 | 1.19 | 70.6 |
| 0.1.52 S2 | Combo CPU Vertices OFF | Unrecorded | visually_confirmed_race_interior | 104.8 / 118.2 / 141.8 | 1.20 | 68.1 |
| 0.1.53 Sonic-53-owner | Combo CPU Vertices OFF | Unrecorded | Sonic race | 102.1 / 115.0 / 127.8 | 1.20 | 71.3 |
| 0.1.56-r2 s13 | Combo CPU Vertices OFF | v27 | race | 111.0 / 124.9 / 163.8 | 1.18 | 56.8 |
| 0.1.57 R3 | Combo CPU Vertices OFF | Stock | Road (laps1–2) | 104.6 / 113.2 / 122.5 | 1.20 | 64.6 |
| 0.1.57 R4 | Combo CPU Vertices OFF | v27 | Road (laps1–2) | 112.6 / 119.3 / 128.6 | 1.17 | 56.5 |
| 0.1.48 s11 | Combo CPU Vertices ON | Unrecorded | s11 | 61.9 / 72.2 / 88.3 | 1.31 | 52.5 |
| 0.1.49 s6 | Combo CPU Vertices ON | Unrecorded | s6 | 51.9 / 66.4 / 87.2 | 1.33 | 52.8 |
| 0.1.50 G1.S14 | Combo CPU Vertices ON | Unrecorded | G1.S14 | 48.8 / 61.7 / 82.4 | 1.30 | 80.8 |
| 0.1.51 combo_cpu_on_2x | Combo CPU Vertices ON | Unrecorded | combo_cpu_on_2x_confirmed_interior | 55.9 / 69.6 / 89.0 | 1.28 | 85.8 |
| 0.1.55 S5 | Combo CPU Vertices ON | Stock | race | 58.5 / 73.1 / 90.9 | 1.30 | 46.3 |
| 0.1.56-r2 s15 | Combo CPU Vertices ON | Stock | race | 58.8 / 68.7 / 88.8 | 1.32 | 41.6 |
| 0.1.57 R2 | Combo CPU Vertices ON | Stock | Road (laps1–2) | 56.8 / 71.0 / 83.5 | 1.31 | 45.5 |

Earlier .1 has a whole-band mean65.102%; .10 has whole-band means86.632% Native and125.054% historical Combo. Those are not scene five-second means. The .46/.47 accelerated-band records contain near400% highs and mixed coverage; they remain in the complete book without being treated as clean racing peaks.

## 4× — selected historical references and latest original-track race

| Build / run | Renderer | Driver | Scope / profile | Low / Overall / High | App cores | GPU busy % |
|---|---|---|---|---|---|---|
| 0.1.55 S3 | Native | Stock | race | 52.6 / 67.1 / 94.1 | 1.28 | 78.9 |
| 0.1.48 Original .48 | Combo CPU Vertices OFF | Unrecorded | Original .48 | 64.6 / 90.6 / 99.5 | 1.08 | 92.1 |
| 0.1.48 CPU-only S2 | Combo CPU Vertices OFF | Unrecorded | CPU-only .48 | 68.4 / 89.5 / 107.3 | 1.05 | 93.5 |
| 0.1.52 .52 OFF old | Combo CPU Vertices OFF | Unrecorded | .52 OFF old | 86.7 / 93.9 / 108.9 | 1.14 | 87.7 |
| 0.1.52 .52 ON | Combo CPU Vertices OFF | Unrecorded | .52 ON | 76.8 / 95.2 / 108.6 | 1.13 | 88.0 |
| 0.1.52 .52 OFF fresh clean | Combo CPU Vertices OFF | Unrecorded | .52 OFF fresh clean | 75.1 / 91.6 / 98.5 | 1.13 | 89.0 |
| 0.1.52 .52 OFF stress | Combo CPU Vertices OFF | Unrecorded | .52 OFF stress | 76.7 / 92.7 / 108.9 | 1.16 | 89.4 |
| 0.1.54 s08 | Combo CPU Vertices OFF | Stock | race_context | 76.4 / 92.1 / 104.7 | 1.13 | 89.3 |
| 0.1.54 s23 | Combo CPU Vertices OFF | T28-toasted · PurpleVK 26.1.99 | race_context | 85.5 / 94.2 / 102.1 | 0.99 | 89.6 |
| 0.1.54 s24 | Combo CPU Vertices OFF | v26.3.0-R6 · Turnip 26.2.99 | race_context | 83.2 / 93.7 / 107.2 | 1.06 | 82.5 |
| 0.1.54 s25 | Combo CPU Vertices OFF | v27 | race_context | 93.0 / 106.3 / 122.2 | 1.08 | 77.2 |
| 0.1.54 s26 | Combo CPU Vertices OFF | T30-toasted · PurpleVK 26.2.99 | race_context | 83.6 / 94.3 / 116.9 | 1.07 | 84.0 |
| 0.1.55 S1 | Combo CPU Vertices OFF | Stock | race | 91.2 / 102.7 / 122.8 | 1.13 | 90.4 |
| 0.1.55 S6 | Combo CPU Vertices OFF | Stock | race | 90.9 / 97.2 / 106.2 | 1.13 | 88.7 |
| 0.1.55 S7 | Combo CPU Vertices OFF | v27 | race | 105.7 / 118.5 / 142.2 | 1.13 | 84.1 |
| 0.1.55 S8 | Combo CPU Vertices OFF | v27 | race | 94.5 / 109.4 / 132.8 | 1.07 | 77.2 |
| 0.1.56-r2 s12 | Combo CPU Vertices OFF | v27 | race | 99.3 / 110.4 / 131.8 | 1.16 | 79.0 |
| 0.1.57 R6 | Combo CPU Vertices OFF | v27 | Road (laps1–2) | 99.7 / 109.5 / 117.0 | 1.15 | 79.0 |
| 0.1.57 R6 | Combo CPU Vertices OFF | v27 | Final-lap water | 101.9 / 111.6 / 121.5 | 1.15 | 80.7 |
| 0.1.58 B1 | Combo CPU Vertices OFF | v27 | Original track; FOUR + bank16 | 98.8 / 109.0 / 140.1 | 1.15 | 78.7 |
| 0.1.55 S2 | Combo CPU Vertices ON | Stock | race | 54.7 / 66.7 / 87.7 | 1.28 | 79.2 |

B1 road: 98.8 / 106.1 / 115.0; final water: 103.7 / 107.2 / 110.1. The whole-race high includes additional racing sections outside these two conservative interiors.

## Samba Studios — independent chart, resolutions kept separate

All below are Combo CPU Vertices OFF with v27. The .55 historical settings/conditions differ.

| Resolution | Run | Profile | Averages: Low / Overall / High | Actual1s: Low / High | App cores | GPU busy % |
|---|---|---|---|---|---|---|
| 2× | 0.1.58 B5 | Baseline | 94.2 / 109.6 / 139.7 | 80.1 / 155.0 | 1.20 | 56.4 |
| 2× | 0.1.58 B3 | FOUR + bank16 | 93.8 / 109.5 / 137.9 | 79.9 / 146.0 | 1.21 | 54.0 |
| 4× | 0.1.55 S12 | Historical reference | 91.6 / 103.6 / 150.8 | Unavailable | 1.15 | 88.6 |
| 4× | 0.1.58 B6 | Baseline | 82.8 / 93.7 / 116.6 | 74.6 / 128.6 | 1.18 | 83.4 |
| 4× | 0.1.58 B2 | FOUR + bank16 | 83.7 / 95.7 / 119.6 | 74.7 / 125.3 | 1.17 | 82.4 |

Samba alternates indoor driving, canal, western road and space/boat sections within laps. Its detailed scene-family and legacy overlay results are in the separate Samba book. Branch differences and short interiors remain explicit. A brief B4 startup/restart is preserved but excluded from performance scoring.

## All-Core utilization — latest five races

Whole-device per-core busy percentages include Uberhar, Android, drivers and capture. App CPU equivalents are measured separately; do not claim device-core activity is all emulator-worker utilization.

| Run | Resolution | Profile | CPU0 | CPU1 | CPU2 | CPU3 | CPU4 | CPU5 | CPU6 | CPU7 | App cores |
|---|---|---|---|---|---|---|---|---|---|---|---|
| B1 | 4× | FOUR + bank16 | 4.2 | 4.0 | 8.5 | 44.6 | 43.8 | 43.0 | 42.7 | 97.3 | 1.15 |
| B5 | 2× | Baseline | 4.8 | 12.5 | 11.2 | 43.8 | 43.5 | 42.2 | 42.4 | 98.2 | 1.20 |
| B3 | 2× | FOUR + bank16 | 4.9 | — | — | 43.6 | 43.5 | 42.0 | 42.1 | 98.2 | 1.21 |
| B6 | 4× | Baseline | 5.0 | 7.1 | 4.1 | 47.2 | 46.9 | 45.5 | 45.8 | 97.6 | 1.18 |
| B2 | 4× | FOUR + bank16 | 4.7 | 3.1 | 8.1 | 46.7 | 46.0 | 45.0 | 45.0 | 97.3 | 1.17 |

B3 CPU1/2 are unavailable after invalid-accounting rejection, not zero. Standard profile was confirmed; no Wi-Fi background-load warning occurred. Named internal CPU sensors reached approximately96.2°C, which is not a surface temperature or independent proof of throttling.

| Run | Actual1s low | Actual1s high | CPU7 at5s low | GPU at5s low | CPU7 at5s high | GPU at5s high |
|---|---|---|---|---|---|---|
| B1 | 91.1 | 157.6 | 95.7 | 78.7 | 97.9 | 84.0 |
| B5 | 80.1 | 155.0 | 98.5 | 50.7 | 98.0 | 62.4 |
| B3 | 79.9 | 146.0 | 98.4 | 43.8 | 97.9 | 55.1 |
| B6 | 74.6 | 128.6 | 98.4 | 81.3 | 97.7 | 86.0 |
| B2 | 74.7 | 125.3 | 97.5 | 77.5 | 96.1 | 81.2 |

## Findings

1. On the original track, bank16 remains useful within the .58 FOUR/FIVE comparisons. FOUR+bank16 improves road mean by4.08 points and water mean by12.66 points over today’s v27 baseline. FIVE+bank16 has the stronger five-second water low; State profile has the strongest road mean but short water coverage.
2. Samba does not inherit the same gain: FOUR+bank16 versus baseline is effectively tied at2× (109.53 versus109.64% overall). At4× it improves overall by2.06 points/+2.2% relative, with almost unchanged real one-second lows (74.72 versus74.61%).
3. History still contains stronger results. .55 v27 original-track reference averages118.49%, compared with108.998% in latest4× B1; .55 Samba averages103.64%, compared with95.71% in B2. These are meaningful recovery targets, but unmatched settings/conditions prevent attributing the full difference to one code change. The .57 v272× baseline road/water means119.26/132.71% also exceed today’s FOUR+bank16 means113.76/124.64%.
4. CPU7 remains approximately97–98% busy. Samba2× lows occur with CPU7≈98.4% and GPU busy43.8–50.7%;4× adds substantial GPU pressure while CPU7 remains busy. More total threads alone does not establish useful offload of the limiting work.
5. Preserve FOUR+bank16 and FIVE+bank16 as original-track candidates; do not promote them as universal Sonic winners. Keep the v27 Samba baseline as the primary comparator at each resolution. Full-matrix and Mono guest1% additions did not improve the earlier original-track results.

## Coverage and reproducibility

Latest capture: five scored races plus one short restart. All229 compact records,1,112 one-second sections and six finals reconcile with zero compact sequence/drop/overflow losses.207 screenshots were available for the original analysis and were subsequently deleted during cleanup.120 optional ordinary diagnostic records were omitted; therefore the compact performance record is complete but not every optional diagnostic detail is present. Workload census was OFF in all six latest launches. Each launch recorded an empty application Vulkan cache; driver-private caches remain unknown.

Capture audit (source not included in this compact archive), manifest (source not included in this compact archive), scene bounds (source not included in this compact archive), scene notes (source not included in this compact archive), scored data (source not included in this compact archive), historical validation (source not included in this compact archive). Original analysis used make_charts.py, prepare_metrics.py and score_scenes.py; those inputs/scripts are not included in this compact archive.

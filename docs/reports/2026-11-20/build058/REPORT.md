<!-- CodexAstraLocal: Final performance report, sanitized for the owner-authorized public archive. -->
> Public report copy, archived November 20, 2026. Measurements and historical qualifications are retained. Private paths, device identifiers and operational closeout details are omitted; raw captures were deleted during the authorized storage cleanup. References to retained raw evidence describe the original analysis, not its present availability. This archive does not authorize device operations or settings restoration.

# Uberhar 0.1.58 — completed device matrix and historical review

<!-- CodexAstraLocal: Consolidation of frozen scored evidence and retained historical reports. No new device test, raw re-extraction, production edit or inferred missing metric. -->

Device testing and capture are complete. Dark Moon and Xenoblade completed their authorized matrices; Fire Emblem completed42unique configurations (41normal receipts and1qualified Native reference). Navigation recovery continued the original launches; no repeat tests were added after the owner’s latest instruction. Capture ended after the final normal game exit, with caught_up=true and all recorder processes stopped. Actual saves were preserved; temporary private captures were subsequently removed during the authorized storage cleanup.

**Overall decision:** these results support selective, game-specific optimization and concrete repairs, not broad enablement of all new controls. They do not establish recovery of the stronger historical .48 Dark Moon performance. The useful mechanisms are exercised fast conversion, downstream triangle rejection and capacity-dependent specialization. Upload/indexed-output eligibility is a concrete implementation gap; vertex-result lookup often costs more than its low reuse supports.

Current Dark Moon first-lab observations are below retained .48 figures in all three renderers. Current GPU busy is also lower in that comparison, so a GPU-saturation-only explanation is inadequate. Host preparation/submission, waits, specialization availability and diagnostic overhead need attention alongside GPU code efficiency. The old/new scene definitions, flags, clocks and diagnostics differ: this is a strong direction for investigation, not an isolated causal measurement of a particular commit.

Historical .10 Dark Moon and Sonic measurements were located, as recorded in the November19 correction below; exact .20 gameplay remains unlocated. Older .9/.17 runs were not relabeled. .30/.40/.46/.48/.52 evidence is included below. No historical Xenoblade measurement was located in those reviewed milestones. Later .58 Sonic testing is published separately in this archive.

## How to read the metrics

All speed values are percent of normal emulation speed. Five-second **Averages: Low / Overall / High** remain separate from genuine retained one-second Low/High. Sparse scenes may have no complete five-second window. Whole-device CPU0–7 is not application-worker attribution; app core-equivalents are shown separately. GPU busy is a sampled device value, not measured app-exclusive utilization or DRAM bandwidth. Null/invalid readings remain unavailable.

Stock baselines are primary, unchanged previously selected profiles secondary. Renderer, driver, resolution and saved/effective Guest3D are separate dimensions. “Selected profile” is not a claim of a universal optimum. Current diagnostics were ON, so no diagnostics-OFF gain is inferred.

## Current Dark Moon and Xenoblade analysis

# Uberhar .58 — Dark Moon and Xenoblade completed findings

<!-- CodexAstraLocal: Existing frozen .58 evidence only. FEA and historical comparisons are outside this draft. No reruns, new reductions, rescoring or device operations. -->

The useful outcome is a set of scene-specific candidates and concrete implementation repairs. Neither game's previous selected profile is a universal optimum. Keep matching clean baselines primary, the unchanged selected profile secondary, and renderer, driver and saved slider as separate factors. Do not add single-control gains together to predict an untested combination.

CHAT_TABLES.md (source not included in this compact archive) contains paste-ready scene Averages Low/Overall/High, actual1s Low/High, app-core equivalents, GPU snapshots and representative CPU0–7 values. It uses each frozen comparison's own windows; driver and renderer context tables are not interchangeable with later control references. The full Dark Moon charts (source not included in this compact archive) and Xenoblade chart report (source not included in this compact archive) retain all reviewed contexts and sources.

## Dark Moon

**Keep clean stock Native/slider0 as the baseline, with Fast CPU vertex conversion as a useful standalone candidate.** Against the refreshed clean reference, conversion improves the four scene means by 3.7/2.7/3.8/1.9 percentage points. Its first-lab and second-lab actual1s lows nevertheless lose 1.2 and 8.1 points. The original FOUR bundle lowered all 16 means against its exact driver/slider baselines in the earlier matched profile block. It should remain a secondary comparison, not the default presumed best setting. FOUR means **Optimized Generic Shaders; Typed primitive append; Adaptive CPU Workers; Prepare displayed eye only**. Native profile decision (source not included in this compact archive), completed control summary (source not included in this compact archive).

**Native FOUR + Full pipeline capacity: 8 / 16 is the strongest measured combined mean candidate, with material qualifications.** Relative to the refreshed clean baseline its means improve by 11.7/6.5/6.9/3.3 points; relative to unchanged FOUR the gains are 11.6/7.6/6.8/5.0. This was real capacity use: 16 full plus eight partial owners, with full-specialized selection among eligible requests rising from 59.91% to 74.45%. It does not enlarge the partial/static working set. The run also has higher sampled prime clocks and cooler GPU conditions following recovery, and two actual1s lows remain below clean. Treat the capacity result as a promising interaction, not an isolated estimate of the toggle's benefit. Capacity report (source not included in this compact archive), post-recovery context (source not included in this compact archive).

**Combo CPU Vertices OFF is the practical stock/slider0 throughput reference for the later scenes.** In the capacity-matched renderer comparison it wins the first-lab, ghost/moon and second-lab means and five-second lows, while ON wins opening. ON/slider1 remains a distinct alternative with stronger actual1s lows in all four scenes against the fresh OFF comparison, at the cost of both lab means. The subsequent control series uses the late OFF baseline, retaining early OFF only as drift context. Clean + conversion improves all four means against late OFF; adding FOUR does not improve its Combo means over conversion alone. Clean triangle rejection is most useful in second lab; FOUR + rejection retains small later-scene mean gains over late clean. Native FOUR + capacity16's result does not transfer to Combo: Combo FOUR + capacity16 loses every actual1s low against FOUR. Renderer selection (source not included in this compact archive), late-anchor comparisons (source not included in this compact archive), closed stage (source not included in this compact archive).

**Driver findings remain separate.** Stock is the stronger clean Native baseline; Turnip v27.0.0-R1 lowers every matched Native scene mean at slider0 and substantially worsens ghost/moon lows. For clean Combo OFF, v27 improves lab means and sustained lows but loses opening; its slider0 second-lab actual1s low is a real retained 31.4% dip despite the higher mean. Slider1 changes that low tradeoff and is not pooled with slider0. FOUR/v27 also remains a profile/driver-specific tradeoff rather than a universal improvement. Native driver report (source not included in this compact archive), Combo driver report (source not included in this compact archive), driver values (source not included in this compact archive).

The measured work narrows the next changes:

| Area | What actually executed or was avoided | Next implementation |
|---|---|---|
| Fast CPU vertex conversion | Hundreds of millions of fast conversions; Native FOUR also uses shared-stride conversion. | Retain the simpler beneficial path and investigate its lab brief-low regressions; do not assume FOUR is required. |
| Reject proven invisible CPU triangles | About 65–66% of inspected triangles rejected after guest shading and initial packing. | Preserve proven downstream elimination, reduce/check packing and rejection overhead, and investigate opening tradeoffs. Logical bytes avoided are not measured DRAM bandwidth. |
| Full pipeline capacity: 8 / 16 | More full-specialized eligibility/selection, with changed compilation/retirement behavior. | Improve admission and working-set/churn behavior rather than applying the larger bank globally. Inspect the separate partial/static-TEV availability loss. |
| Reuse independent vertex results | Low current hit rates: Native FOUR 0.660%; Combo clean 0.312%, with many occupied-slot replacements. | Reduce lookup/planning overhead or gate unsuitable workloads; useful reuse elsewhere remains possible. |
| Worker CPU vertex upload / Indexed CPU output | Zero acquisition/copy/publication or indexing work in these paths. | Repair the reviewed guest/host topology admission mismatch before evaluating the intended algorithms. Rejected-path cost is not isolated. |
| Prune unused specialized shader work | Direct SPIR-V builds succeed; removed instructions and texture reads are unmeasured. | Add removed-code-volume and exclusive-cost evidence before making a saved-work claim. |
| Mono guest 1% path | Saved0→effective1 override works; transfer-call ratios roughly halve as guest behavior. Presentation preparation is omitted by the existing FOUR setting. | Keep as an input/interaction tool. No command lists, guest draws, GPU vertices or eye-suppression transfers were removed; do not claim a broad work-halving optimization. |

The topology review (source not included in this compact archive) supplies the actionable eligibility defect. The unchanged-baseline drift review (source not included in this compact archive) identifies a partial/static-TEV availability shift: ghost generic-route share rises 3.15%→11.01% and static-module hit rate falls 86.96%→73.19%, while full-module hit rate is nearly unchanged. These observations are more specific than inferring efficiency from GPU busy alone. Overlapping timers and differently denominated request/route shares cannot be summed into a total cost budget.

Dark Moon closed with **59 normal completions and two shutdown-ANR gameplay prefixes**, plus 16 superseded unperformed plan entries. The ANR rows are original Combo OFF and Native FOUR + triangle rejection; neither has normal run-end/compact FINAL. No feature-specific shutdown cause was established. The private stop/wait repair was compile-checked but not installed in the tested APK. Native reference drift and early→late Combo drift remain visible; clocks are sampled conditions, not normalized away or asserted to be the sole cause. CPU1/2 accounting anomalies stay masked. Closure receipt (source not included in this compact archive), closed-stage qualifications (source not included in this compact archive).

## Xenoblade

**Combo CPU Vertices OFF is the strongest broad stock renderer reference in the frozen three-renderer block.** It has the highest mean in all four scenes. In battle, OFF exceeds ON by 6.4 points in mean, 7.8 in five-second low and 3.5 in actual1s low. Native retains stronger decoy lows; ON retains a small stationary actual1s-low advantage. The short decoy has only two or three complete five-second windows in this renderer comparison. These observations select a renderer reference without declaring every associated setting optimal. Renderer report (source not included in this compact archive).

The unchanged selected profile adds **Disable Right Eye Render; Early right-eye suppression; Prepare only displayed eye; Typed primitive append; Adaptive CPU Workers; Optimized Generic Shaders; Render-thread delay 100µs** to the matching baseline. Saved slider0 and slider1 remain separate. On stock, the bundle improves useful Combo decoy performance but loses opening speed; Native slider1 loses all four means against its matching clean baseline. This profile remains secondary, with exact controls preserved, rather than being called “optimal.” Stock profile report (source not included in this compact archive).

**Native triangle rejection supplies the clearest demonstrated downstream-work reduction.** Alone, it improves battle/decoy means by 7.7/7.4 points over baseline. Added to the selected profile it improves those means by 2.5/2.7 against the unchanged profile, rejects 33.20% of inspected triangles, and improves decoy actual1s/five-second lows against both references. Original guest vertex execution and initial packing still occur. Memo reuse and conversion have localized benefits with planning/brief-low costs: conversion's profile-relative stationary mean gain of 4.7 points accompanies an actual1s-low loss of 10.1. Native capacity16 increases full selection without producing a broad profile mean gain. Native baseline controls (source not included in this compact archive), Native profile controls (source not included in this compact archive).

**Combo OFF triangle rejection and capacity16 are the most useful profile additions.** Rejection's battle mean is +3.8 against stock and +3.6 against the unchanged profile; decoy is +8.8/+5.1. Its 33.41% rejected triangles avoid 44.14 GB of logical packed output, not measured physical memory traffic. Capacity16 improves all four profile-relative means and useful decoy/stationary lows, although battle five-second low falls 6.1 points versus profile. It reaches 16 full plus eight partial entries while normalized churn rises. Memo really reuses 64.85 million of 560.33 million checked invocations, but battle five-second low falls 8.7 points against profile. Preserve these scene tradeoffs rather than combining the improvements arithmetically. Completed Combo profile controls (source not included in this compact archive), rejection/capacity source report (source not included in this compact archive).

**Clean Turnip Gen8 V38 Combo OFF/slider0 improves every matched scene mean, five-second low and actual1s low over stock/slider0.** Adding the old selected profile on V38 then gives back substantial opening, battle and stationary speed while helping decoy. Native has no equivalent complete clean V38 baseline: original g0 completed under background contamination; original g1 was interrupted and has no benchmark score; repaired g0 admits only the cinematic prefix, with stationary unavailable. Do not transfer Combo's driver conclusion to Native or relabel any qualified Native row as clean. V38 Combo pair (source not included in this compact archive), V38 profile/reference qualifications (source not included in this compact archive).

Next implementation priorities follow the actual work:

1. Retain triangle rejection and improve its remaining overhead/brief-low behavior. Tune full-pipeline admission and churn around capacity16's measured Combo benefits.
2. Repair upload/indexed-output eligibility before tuning the switches. Both still execute zero eligible attempts; the latest Combo indexed run reports 8.423 million ineligible cases. Its observed speed changes cannot be credited to indexing.
3. Reduce memo planning/check cost while preserving genuine reuse; investigate conversion regressions with the original-input denominator retained.
4. Add pruning removed-code-volume and exclusive-cost diagnostics. Module generation succeeds, but all four Combo profile-relative pruning means decline.
5. Keep Mono saved0→effective1 separate from directly saved1. Its guest lists/transfers/draws omitted remain zero; selected-profile presentation-preparation savings already existed. Further eye-work removal needs identity/dependency evidence, not an assumption of duplicated work.

Clock shifts remain material and can be workload/governor responses; no clock-only cause or normalized score is asserted. Native pruning's decoy contains a background update-app burst, and Native Mono opening includes the recorded stop intervention. Baseline Native memo is only the scored pre-shutdown prefix, followed by ANR with FINAL absent and unknown unflushed tail; memo causation is unproven. Original V38 Native g0's CPU7 background load excludes it from clean driver ranking. New-control conclusion (source not included in this compact archive), Native final controls (source not included in this compact archive), Native memo qualification (source not included in this compact archive).

## Reporting limits carried forward

Speed is percent of normal emulation speed; deltas are percentage points. Averages are qualified five-second Low/Overall/High, while actual1s Low/High are distinct retained one-second measurements. A single accepted five-second window can have equal Low/Overall/High without proving stable pacing. Interval peaks are not substituted for either metric.

App-core equivalents are process-specific; CPU0–7 percentages are whole-device utilization and do not identify individual app workers. GPU busy is sampled system activity, not app-exclusive GPU time or measured memory bandwidth. Missing/invalid values remain missing. Diagnostics were ON, so these results do not quantify diagnostics-OFF performance. Frozen scores, windows and qualifications are unchanged; no .57→.58 claim, FEA conclusion or additional test is included here.


## Fire Emblem completed findings

**Retain the existing ComboOFF profile and keep its eight new additions OFF on this FEA evidence. Native fast conversion is the strongest remaining implementation candidate, with visible scene tradeoffs.** No new control earns a universal default across renderers/profiles.

CodexAstraLocal — four-stage synthesis from completed summaries only. The 32 single-switch controls were added separately to clean Native/ComboOFF baselines or their unchanged four-setting profiles. Settings were system driver, 4×, Linear Filtering ON, saved Guest3D 0%; mono additionally forces effective 0.01. The profile enables adaptive workers, displayed-eye preparation, generic optimization and original right-eye suppression. No combined benefit of the eight new controls was measured.

| Stage / useful comparison | 1s weighted cinematic mean (%) | Separate 5s cinematic Overall (%) | Matched map 1s mean (%) | Practical reading |
|---|---:|---:|---:|---|
| Native baseline, reference→fast, partial 14–34 | 148.43→246.80 | 148.56→unavailable | clean reference unavailable; fast 150.81 | Strong observation, qualified history and inactive-control gains prevent causal attribution. |
| ComboOFF baseline, reference→fast, full 14–38 | 176.00→200.95 | 170.72→194.97 | 158.16→139.42 | Cinematic tradeoff; map worsens. |
| Native profile, unchanged→fast, full 14–38 | 172.85→189.34 | 167.51→188.80 | 145.90→153.07 | Mixed positive candidate; cinematic 1s low 152.48→146.74, map low 130.80→134.76. |
| ComboOFF profile, unchanged→best added cinematic(bank16) | 200.71→195.29 | 202.78→186.13 | 163.36→144.37 | Every added cinematic and every valid added map mean is lower. |

These rows retain their exact full/partial contracts; they are not a pooled ranking. Full low/mean/high and counts for both 1s and 5s are in the four stage tables. Native 0 clean map is missing, so its controls cannot claim a primary map gain. Final Combo-profile mono map is also unavailable after recovery at 34.

**What the mechanisms establish.** Fast conversion executed in large volumes and survives the Native within-profile comparison, but its Combo profile direction reverses. Memo’s roughly 0.3–1.4% reuse and substantial collision counts are weak support for unconditional lookup; its favorable clean-Native opening does not survive profile comparisons. Upload and indexed paths performed zero transformations, even when timings improved. Clipping removed roughly 54–57% of inspected triangles and large logical payloads, but current rejection is after guest shader/packing work and loses profile cinematic/map speed. Bank16 changes residency/selection but does not deliver broad profile gains; raw retirement counts must not replace request-normalized churn. Pruning lacks removed-instruction/exclusive-cost evidence. Mono changes guest input and shows zero early-eye/heuristic skips.

**Conditions that change the conclusion.** Application cache files were cold at launch; driver-internal cache was unverified. Qualified Native 0 hit the original 100 MiB ordinary cap and had an attract prelude/long waits: compact scores remain valid, but removing waits from scoring cannot remove specialization or thermal history. Profile and final-mono recovery holds remain explicit. The final Combo mono map contract broke; no score was filled in.

Battle stills establish the intended route/enemy/terminal but not identical combat work: hits 19, MISS, sword 20 and visible Robin 2 differ across rows, with some ally sequences unknown. Native baseline mixes broad 5–13 and active 5–10; Native profile uses active 5–10; Combo uses active 5–11. One-section baseline/profile examples make battle lows descriptive, not reliable distribution estimates. Do not rank those windows together.

CPU 0–7, app-core estimates, retained clock endpoints and sparse GPU points remain source-bound context. No speeds were normalized to clocks. Higher clocks can coexist with slower scenes; lower GPU busy/time can reflect pacing, work mix, sampled dependencies or longer reference lifetimes. Sampled plan/pool/owner/join categories overlap, and no exclusive feature cost is inferred from their sum.

**Next implementation directions**

1. **Native fast conversion:** Carry the exercised Native conversion path forward as a limited candidate, preserving semantic checks and cinematic-low/battle tradeoffs. Do not enable it globally for Combo.
2. **Zero-eligibility upload/indexing:** Diagnose refusal reasons and whether the intended packed/coherent route is reached; wire a real eligible path or avoid paying/advertising an inactive optimization. Preserve ownership and eligibility guards.
3. **Clipping stage placement:** Explore conservative rejection before packing/upload when correctness permits, so removed triangles can avoid earlier work. Retain per-stage work and exclusive-cost evidence.
4. **Memo and pipeline admission:** Prefer reuse/cost-aware admission and low-hit bypass over unconditional cache lookup or larger full-bank capacity. Keep full/static selection and request-normalized churn distinct.
5. **Pruning and mono semantics:** Require removed instruction/code/stage-cost evidence for pruning. Expose mono as a guest-input policy, not proof of renderer work elimination.

Sources: Native baseline (source not included in this compact archive), ComboOFF baseline (source not included in this compact archive), Native profile (source not included in this compact archive), ComboOFF profile (source not included in this compact archive). Their JSON/CSV artifacts retain full precision, actual settings, scene/resource coverage and pair-report source hashes. This conclusion introduced no raw-log reread, extraction, rescoring, device test or production edit.


## Current scene and all-core tables

# .58 paste-ready scene and resource tables

<!-- CodexAstraLocal: Formatting existing scored rows only; no new windows, reductions, device operations or historical comparison. -->

Speeds are % of normal. **5s = Averages Low / Overall / High; actual1s = genuine retained one-second Low / High.** App cores are process core-equivalents; GPU busy is sampled system activity. n5/n1 are accepted window counts. A single n5 can produce equal Low/Overall/High without stable pacing. “—” stays missing, never zero.

Within each block, clean baseline is primary, unchanged selected profile is secondary, then the candidate. Each block retains its own frozen matched windows; do not subtract figures across different blocks. All control blocks use stock driver. Driver comparisons appear separately.

**Dark Moon FOUR:** Optimized Generic Shaders; Typed primitive append; Adaptive CPU Workers; Prepare displayed eye only. **Xenoblade selected profile:** Disable Right Eye Render; Early right-eye suppression; Prepare only displayed eye; Typed primitive append; Adaptive CPU Workers; Optimized Generic Shaders; Render-thread delay 100µs.

## Dark Moon Native — saved/effective slider0

Capacity16 is Full pipeline capacity: 8 / 16. Its stronger sampled prime clocks/cooler GPU conditions remain a material qualification.

| Configuration / scene | 5s Low / Overall / High | Actual1s Low / High | App cores | GPU busy % | n5 / n1 |
|---|---:|---:|---:|---:|---:|
| Baseline — Opening moon | 58.6 / 58.6 / 58.6 | 49.1 / 67.0 | 2.13 | 38.1 | 1 / 9 |
| FOUR — Opening moon | 58.7 / 58.7 / 58.7 | 47.4 / 62.4 | 2.08 | 30.3 | 1 / 9 |
| FOUR + capacity16 — Opening moon | 70.3 / 70.3 / 70.3 | 52.1 / 77.7 | 2.11 | 36.9 | 1 / 8 |
| Baseline — First lab | 51.5 / 54.5 / 56.7 | 41.6 / 63.5 | 1.87 | 61.6 | 7 / 31 |
| FOUR — First lab | 49.9 / 53.4 / 56.4 | 40.7 / 62.9 | 1.84 | 57.9 | 8 / 32 |
| FOUR + capacity16 — First lab | 55.1 / 61.0 / 64.9 | 38.9 / 71.4 | 1.85 | 54.4 | 6 / 28 |
| Baseline — Ghost/moon | 50.3 / 54.0 / 56.6 | 36.8 / 62.1 | 1.95 | 63.1 | 4 / 18 |
| FOUR — Ghost/moon | 50.0 / 54.1 / 56.1 | 40.7 / 69.2 | 1.90 | 56.7 | 4 / 20 |
| FOUR + capacity16 — Ghost/moon | 60.1 / 60.9 / 62.2 | 44.1 / 74.6 | 1.97 | 65.0 | 3 / 16 |
| Baseline — Second lab | 50.7 / 52.5 / 53.4 | 46.2 / 61.0 | 1.78 | 60.0 | 3 / 14 |
| FOUR — Second lab | 47.6 / 50.7 / 53.7 | 36.0 / 59.3 | 1.80 | 59.2 | 3 / 15 |
| FOUR + capacity16 — Second lab | 53.9 / 55.7 / 57.5 | 37.7 / 66.0 | 1.79 | 45.5 | 2 / 15 |

Source: native-profile-new-controls/full_pipeline_bank_16/REPORT.md (source not included in this compact archive).

## Dark Moon Combo OFF — saved/effective slider0

Triangle rejection is Reject proven invisible CPU triangles. Late baseline is not pooled with early OFF.

| Configuration / scene | 5s Low / Overall / High | Actual1s Low / High | App cores | GPU busy % | n5 / n1 |
|---|---:|---:|---:|---:|---:|
| Late baseline — Opening moon | 48.1 / 48.9 / 49.8 | 35.5 / 67.2 | 2.32 | 43.8 | 2 / 11 |
| FOUR — Opening moon | 44.8 / 46.3 / 47.9 | 31.1 / 57.9 | 2.29 | 32.3 | 2 / 12 |
| FOUR + triangle rejection — Opening moon | 46.6 / 47.9 / 49.3 | 31.5 / 63.5 | 2.19 | 37.8 | 2 / 12 |
| Late baseline — First lab | 49.8 / 54.5 / 58.7 | 36.4 / 66.3 | 1.80 | 80.6 | 7 / 31 |
| FOUR — First lab | 48.4 / 55.3 / 58.6 | 36.4 / 67.9 | 1.80 | 84.9 | 7 / 30 |
| FOUR + triangle rejection — First lab | 47.7 / 55.0 / 58.6 | 40.4 / 69.5 | 1.77 | 80.3 | 7 / 31 |
| Late baseline — Ghost/moon | 46.3 / 49.9 / 53.0 | 37.7 / 63.8 | 2.00 | 75.1 | 5 / 22 |
| FOUR — Ghost/moon | 47.4 / 48.4 / 49.6 | 34.1 / 55.3 | 1.99 | 70.7 | 4 / 22 |
| FOUR + triangle rejection — Ghost/moon | 46.8 / 50.0 / 53.8 | 39.7 / 63.3 | 2.00 | 55.7 | 4 / 21 |
| Late baseline — Second lab | 50.2 / 52.5 / 54.7 | 39.2 / 64.5 | 1.68 | 83.2 | 2 / 15 |
| FOUR — Second lab | 54.7 / 55.5 / 57.2 | 42.6 / 64.4 | 1.74 | 87.8 | 3 / 14 |
| FOUR + triangle rejection — Second lab | 51.9 / 53.4 / 55.0 | 43.0 / 70.6 | 1.70 | 77.3 | 2 / 15 |

Source: combo-off-profile-new-controls/cpu_clip_reject/REPORT.md (source not included in this compact archive).

## Dark Moon Combo ON — saved/effective slider0

This is the capacity-matched ON reference. The renderer decision uses its own fresh OFF overlap; the later control baseline above is not an interchangeable comparator.

| Configuration / scene | 5s Low / Overall / High | Actual1s Low / High | App cores | GPU busy % | n5 / n1 |
|---|---:|---:|---:|---:|---:|
| Current baseline — Opening moon | 55.0 / 57.9 / 60.8 | 37.7 / 67.7 | 2.10 | 41.8 | 2 / 11 |
| Current baseline — First lab | 50.9 / 54.6 / 56.3 | 39.7 / 63.8 | 1.87 | 61.4 | 7 / 32 |
| Current baseline — Ghost/moon | 52.0 / 53.6 / 55.1 | 41.7 / 63.3 | 1.92 | 58.6 | 4 / 20 |
| Current baseline — Second lab | 50.4 / 51.4 / 52.4 | 43.3 / 59.2 | 1.76 | 64.4 | 3 / 15 |

Source: capacity-matched renderer context (source not included in this compact archive).

## Xenoblade Native — saved/effective slider1

Saved1 is kept separate from saved0 and Mono saved0/effective1. These are observed candidates, not a universal optimum.

| Configuration / scene | 5s Low / Overall / High | Actual1s Low / High | App cores | GPU busy % | n5 / n1 |
|---|---:|---:|---:|---:|---:|
| Baseline — Opening Titans | 94.9 / 146.7 / 239.2 | 58.9 / 262.0 | 1.14 | 93.1 | 9 / 40 |
| Selected profile — Opening Titans | 85.9 / 136.2 / 202.0 | 52.0 / 267.3 | 1.27 | 90.5 | 9 / 40 |
| Selected + triangle rejection — Opening Titans | 81.7 / 140.5 / 199.6 | 61.7 / 231.1 | 1.39 | 87.9 | 9 / 39 |
| Baseline — Battle cinematic | 44.7 / 81.9 / 114.3 | 44.3 / 145.6 | 1.34 | 90.1 | 38 / 155 |
| Selected profile — Battle cinematic | 48.2 / 79.7 / 110.1 | 43.9 / 146.8 | 1.39 | 85.6 | 39 / 158 |
| Selected + triangle rejection — Battle cinematic | 47.8 / 82.2 / 108.0 | 40.7 / 144.2 | 1.41 | 81.9 | 38 / 156 |
| Baseline — Final decoy | 38.4 / 45.0 / 54.5 | 35.5 / 60.0 | 1.39 | 91.5 | 4 / 16 |
| Selected profile — Final decoy | 40.9 / 43.5 / 48.5 | 33.3 / 69.4 | 1.37 | 83.2 | 3 / 15 |
| Selected + triangle rejection — Final decoy | 43.1 / 46.2 / 49.2 | 37.2 / 114.3 | 1.53 | 83.4 | 3 / 16 |
| Baseline — Stationary gameplay | 77.8 / 81.1 / 87.5 | 55.0 / 97.0 | 1.47 | 80.3 | 5 / 21 |
| Selected profile — Stationary gameplay | 79.0 / 80.7 / 83.2 | 64.9 / 94.0 | 1.52 | 80.2 | 5 / 22 |
| Selected + triangle rejection — Stationary gameplay | 76.7 / 80.0 / 83.6 | 56.6 / 93.6 | 1.47 | 77.4 | 5 / 22 |

Source: native-profile-new-controls-second-three/REPORT.md (source not included in this compact archive).

## Xenoblade Combo OFF — saved/effective slider1

Saved1 is kept separate from saved0 and Mono saved0/effective1. These are observed candidates, not a universal optimum.

| Configuration / scene | 5s Low / Overall / High | Actual1s Low / High | App cores | GPU busy % | n5 / n1 |
|---|---:|---:|---:|---:|---:|
| Baseline — Opening Titans | 99.8 / 156.7 / 219.6 | 71.6 / 279.9 | 1.35 | 88.6 | 8 / 36 |
| Selected profile — Opening Titans | 100.8 / 151.5 / 201.5 | 77.2 / 263.5 | 1.39 | 87.1 | 8 / 37 |
| Selected + capacity16 — Opening Titans | 102.2 / 154.8 / 203.9 | 86.0 / 254.7 | 1.43 | 85.0 | 8 / 35 |
| Baseline — Battle cinematic | 47.1 / 82.0 / 119.3 | 43.5 / 160.9 | 1.43 | 87.1 | 37 / 151 |
| Selected profile — Battle cinematic | 55.8 / 82.2 / 115.9 | 43.9 / 161.7 | 1.45 | 85.8 | 37 / 150 |
| Selected + capacity16 — Battle cinematic | 49.7 / 83.9 / 127.1 | 46.1 / 157.7 | 1.46 | 82.1 | 36 / 152 |
| Baseline — Final decoy | 38.9 / 39.2 / 39.4 | 31.3 / 84.1 | 1.31 | 94.1 | 3 / 18 |
| Selected profile — Final decoy | 37.6 / 42.8 / 49.2 | 33.0 / 78.7 | 1.37 | 87.7 | 4 / 18 |
| Selected + capacity16 — Final decoy | 45.3 / 47.3 / 49.6 | 37.7 / 62.9 | 1.44 | 82.5 | 4 / 17 |
| Baseline — Stationary gameplay | 78.1 / 83.3 / 88.4 | 61.8 / 97.8 | 1.50 | 77.1 | 4 / 23 |
| Selected profile — Stationary gameplay | 80.6 / 82.8 / 84.4 | 67.0 / 99.4 | 1.52 | 75.8 | 5 / 23 |
| Selected + capacity16 — Stationary gameplay | 82.8 / 86.9 / 92.7 | 67.9 / 101.5 | 1.50 | 72.9 | 5 / 23 |

Source: combo-off-profile-new-controls-middle-three/REPORT.md (source not included in this compact archive).

## Xenoblade Combo ON — saved/effective slider0

This ON baseline uses the three-renderer comparison window, including its shorter decoy. Do not use its values to subtract from the selected-profile blocks above.

| Configuration / scene | 5s Low / Overall / High | Actual1s Low / High | App cores | GPU busy % | n5 / n1 |
|---|---:|---:|---:|---:|---:|
| Baseline ON — Opening Titans | 91.3 / 140.0 / 198.3 | 71.0 / 250.3 | 1.34 | 91.9 | 9 / 40 |
| Baseline ON — Battle cinematic | 47.4 / 74.4 / 110.8 | 42.1 / 137.8 | 1.35 | 86.1 | 40 / 167 |
| Baseline ON — Final decoy | 37.1 / 38.9 / 42.1 | 31.2 / 88.8 | 1.38 | 93.5 | 3 / 15 |
| Baseline ON — Stationary gameplay | 80.5 / 82.7 / 85.3 | 69.8 / 92.4 | 1.55 | 78.0 | 5 / 20 |

Source: three-renderer shared overlap (source not included in this compact archive).

## Representative whole-device all-core utilization

Dark Moon uses first lab; Xenoblade uses battle cinematic. These match the corresponding scene rows above. CPU0–7 percentages are whole-device values, not app-worker attribution; do not add them to app core-equivalents.

| Game / renderer | Configuration | Scene | CPU0 | CPU1 | CPU2 | CPU3 | CPU4 | CPU5 | CPU6 | CPU7 |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Dark Moon Native | Baseline | First lab | 9.1 | — | — | 46.1 | 44.8 | 43.9 | 43.9 | 92.7 |
| Dark Moon Native | FOUR | First lab | 7.2 | 6.1 | 5.3 | 45.6 | 44.1 | 43.8 | 44.0 | 91.0 |
| Dark Moon Native | FOUR + capacity16 | First lab | 5.3 | — | — | 45.9 | 45.4 | 44.3 | 44.1 | 91.3 |
| Dark Moon Combo OFF | Late baseline | First lab | 7.3 | 7.8 | 8.8 | 54.9 | 53.5 | 52.5 | 52.1 | 92.3 |
| Dark Moon Combo OFF | FOUR | First lab | 7.3 | 9.9 | 7.5 | 55.0 | 54.1 | 53.1 | 52.9 | 92.0 |
| Dark Moon Combo OFF | FOUR + triangle rejection | First lab | 7.8 | 10.6 | 8.0 | 54.4 | 53.2 | 51.7 | 51.2 | 91.5 |
| Dark Moon Combo ON | Current baseline | First lab | 7.4 | 6.4 | 5.6 | 46.0 | 44.1 | 42.9 | 43.1 | 92.9 |
| Xenoblade Native | Baseline | Battle cinematic | 7.9 | 9.6 | 7.2 | 48.7 | 47.7 | 46.2 | 46.3 | 86.9 |
| Xenoblade Native | Selected profile | Battle cinematic | 8.2 | 9.8 | 6.3 | 51.6 | 49.8 | 49.2 | 48.8 | 88.8 |
| Xenoblade Native | Selected + triangle rejection | Battle cinematic | 6.4 | 8.4 | 10.8 | 56.5 | 55.3 | 54.6 | 54.2 | 90.6 |
| Xenoblade Combo OFF | Baseline | Battle cinematic | 8.1 | 7.7 | 9.6 | 52.3 | 50.9 | 50.2 | 49.7 | 88.9 |
| Xenoblade Combo OFF | Selected profile | Battle cinematic | 8.3 | 10.4 | 6.7 | 54.2 | 53.3 | 52.2 | 51.7 | 89.7 |
| Xenoblade Combo OFF | Selected + capacity16 | Battle cinematic | 7.2 | 8.2 | 11.7 | 59.4 | 58.4 | 57.3 | 57.0 | 91.0 |
| Xenoblade Combo ON | Baseline ON | Battle cinematic | 8.3 | 10.1 | 7.0 | 49.9 | 49.0 | 48.2 | 47.5 | 87.1 |

## Driver comparisons — clean settings, saved/effective slider0

These are earlier driver-specific matched windows, not the refreshed control baselines. Driver remains separate from selected profile. Each cell retains highs.

### Dark Moon Native — Stock versus Turnip v27.0.0-R1

| Scene | Stock 5s L / Overall / H | v27 5s L / Overall / H | Stock actual1s L / H | v27 actual1s L / H |
|---|---:|---:|---:|---:|
| Opening moon | 65.8 / 65.8 / 65.8 | 63.3 / 64.0 / 64.6 | 48.7 / 76.5 | 48.9 / 70.6 |
| First lab | 58.6 / 61.2 / 64.0 | 54.7 / 58.6 / 63.5 | 48.1 / 69.5 | 44.1 / 70.5 |
| Ghost/moon | 57.8 / 60.4 / 64.0 | 46.7 / 56.3 / 62.2 | 47.9 / 68.2 | 33.4 / 72.2 |
| Second lab | 56.9 / 57.5 / 58.2 | 54.8 / 55.0 / 55.1 | 42.8 / 67.0 | 47.5 / 65.4 |

Source: frozen driver plot data (source not included in this compact archive).

### Dark Moon Combo OFF — Stock versus Turnip v27.0.0-R1

| Scene | Stock 5s L / Overall / H | v27 5s L / Overall / H | Stock actual1s L / H | v27 actual1s L / H |
|---|---:|---:|---:|---:|
| Opening moon | 48.7 / 49.9 / 51.1 | 43.9 / 45.9 / 47.9 | 36.7 / 71.2 | 34.3 / 60.6 |
| First lab | 52.4 / 58.0 / 60.8 | 57.4 / 64.6 / 70.7 | 40.2 / 71.2 | 46.5 / 80.2 |
| Ghost/moon | 52.2 / 54.6 / 57.1 | 49.3 / 54.0 / 60.8 | 40.7 / 65.8 | 37.4 / 75.5 |
| Second lab | 53.1 / 56.2 / 59.1 | 57.8 / 61.8 / 64.0 | 41.7 / 67.1 | 31.4 / 76.3 |

Source: frozen driver plot data (source not included in this compact archive).

### Xenoblade Combo OFF — Stock versus Turnip Gen8 V38

| Scene | Stock 5s L / Overall / H | V38 5s L / Overall / H | Stock actual1s L / H | V38 actual1s L / H |
|---|---:|---:|---:|---:|
| Opening Titans | 99.3 / 156.5 / 227.5 | 136.3 / 189.3 / 232.3 | 75.1 / 244.8 | 81.6 / 265.2 |
| Battle cinematic | 55.2 / 80.8 / 118.8 | 65.9 / 100.1 / 140.7 | 45.6 / 162.5 | 47.4 / 164.1 |
| Final decoy | 36.3 / 43.1 / 52.8 | 48.4 / 52.2 / 56.0 | 30.4 / 71.7 | 46.6 / 94.6 |
| Stationary gameplay | 81.8 / 83.7 / 85.2 | 95.2 / 98.3 / 103.9 | 68.1 / 98.5 | 73.4 / 118.5 |

Source: V38 clean Combo pair (source not included in this compact archive).

**Qualifications to paste with the tables:** Native Dark Moon capacity16 conditions differ; late/early baselines are not pooled. Original Dark Moon Combo OFF and Native FOUR + triangle rejection are separate ANR prefixes, not the normal rows selected above. Xenoblade baseline Native memo is a pre-shutdown ANR prefix, FINAL absent. Original V38 Native g0 is background-contaminated; g1 is interrupted and unscored; repaired g0 is cinematic-only, with stationary unavailable. No complete clean Native V38 driver table can be supplied.

All scene scores and resource cells were copied from already scored reports/data, rounded only for display. Full raw precision, all controls, secondary deltas, slider1/Mono contexts and same-window extreme resources remain in the existing chart/report sources. No scores, masks or matching bounds were recomputed.


## Early historical milestones

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


## Later historical milestones

# Historical .46 / .48 / .52 evidence

**CodexAstraLocal — retained report synthesis only.** No new device work or raw-log reprocessing. These values are emulation speed percentages. Low/average/high below are **five-second window averages**, not .58 one-second minima/maxima. Original .48 and CPU-only .48 are separate builds.

**How to use this evidence.** Match renderer, resolution and scene first. The saved summaries below do not independently identify the loaded driver package, so driver matching is unresolved rather than assumed. Older FEA pooled map/battle rows and .46 opening-interior rows remain historical context; do not subtract them from .58 isolated scenes. Sonic has not been tested on .58 in this campaign.

## .46 retained observations

The `candidate047` directory contains a report explicitly identifying these as **.46**. All four cover 20 complete windows, Vulkan 2×, normal cap 100%, simulated GPU timings ON. The Dark Moon cohort covers valley/lab interior rather than a complete opening or individually segmented scenes.

| Game / renderer | Low / average / high | App cores | GPU busy |
|---|---:|---:|---:|
| Dark Moon — Native | 62.50 / 68.58 / 74.30 | 1.947 | 79.85% |
| Dark Moon — Combo CPU vertices OFF | 44.70 / 62.90 / 72.70 | 1.730 | 91.86% |
| Dark Moon — Combo CPU vertices ON | 61.68 / 67.61 / 72.99 | 1.969 | 75.97% |
| Sonic — Combo CPU vertices OFF | 98.55 / 99.88 / 100.18 | 1.107 | 47.81% |

Sonic near 100% here was capped; that is not evidence of its uncapped ceiling. .46 FEA and Xenoblade performance rows are unavailable in the reviewed reports.

## Dark Moon: original .48 and .52, 2×

Rows are grouped by renderer. Cells show **low / average / high · app cores · GPU busy** for each named scene. .52 baseline means its historical all-enabled default, not “all features OFF” and not a match to .58 baseline. Native full-fragment OFF and all 15 .52 variants are retained in JSON.

| Renderer / build / retained case | Opening moon | First lab | Ghost / moon | Second lab |
|---|---:|---:|---:|---:|
| Native / 0.1.48 / S6 | 78.55 / 81.77 / 86.57 · 2.18 · 75.6% | 72.35 / 75.29 / 80.34 · 1.98 · 79.2% | 61.78 / 68.39 / 75.13 · 1.94 · 89.3% | 61.94 / 67.05 / 72.99 · 1.73 · 90.5% |
| Native / 0.1.52 / native-a | unavailable | 75.17 / 75.50 / 75.82 · 1.94 · 80.9% | 33.91 / 46.75 / 59.72 · 1.19 · 99.3% | 49.92 / 49.92 / 49.92 · — · —% |
| Combo CPU vertices OFF / 0.1.48 / S12 | 53.13 / 64.98 / 71.07 · 2.59 · 72.7% | 53.74 / 65.64 / 73.07 · 1.78 · 91.4% | 54.64 / 66.36 / 76.41 · 1.95 · 85.2% | 43.45 / 53.46 / 61.16 · 1.19 · 97.2% |
| Combo CPU vertices OFF / 0.1.52 / combo-off-a | 74.01 / 74.66 / 75.31 · 2.34 · 81.3% | 55.46 / 61.55 / 66.06 · 1.55 · 97.6% | 49.94 / 64.01 / 78.40 · 1.78 · 98.5% | 37.37 / 45.45 / 50.42 · 0.94 · 97.5% |
| Combo CPU vertices ON / 0.1.48 / S10 | 75.31 / 78.26 / 80.85 · 2.23 · 79.0% | 67.78 / 72.23 / 76.11 · 1.99 · 78.8% | 61.81 / 66.31 / 71.22 · 1.99 · 83.5% | 61.72 / 64.70 / 69.18 · 1.78 · 85.8% |
| Combo CPU vertices ON / 0.1.52 / combo-on-a | — / — / — · — · —% | 68.70 / 72.12 / 74.26 · 1.95 · 78.1% | 32.38 / 48.02 / 65.29 · 1.22 · 98.8% | 55.29 / 59.94 / 66.70 · 1.59 · 96.4% |
| Combo CPU vertices ON / 0.1.52 / combo-on-b | 78.32 / 78.32 / 78.32 · — · —% | 65.74 / 69.72 / 73.72 · 1.96 · 81.0% | 33.58 / 45.12 / 58.65 · 1.24 · 99.2% | 49.04 / 57.53 / 65.58 · 1.59 · 88.3% |
| Combo CPU vertices ON / 0.1.52 / Reuse fragment texture samples OFF | 69.80 / 73.30 / 76.80 · 2.15 · 72.2% | 64.83 / 70.15 / 74.41 · 1.94 · 74.7% | 55.97 / 61.85 / 71.03 · 1.82 · 76.7% | 60.43 / 62.06 / 63.15 · 1.77 · 83.2% |

The .52 all-enabled Combo ON ghost/moon low was **32.38–33.58%**, versus **61.81%** in the retained original .48 scene. Turning **Reuse fragment texture samples OFF** in .52 raised that low to **55.97%**, with GPU busy dropping from approximately **99% to 77%**. That supports a costly GPU-path contribution in that older implementation; it does not establish the cause of current .58 differences. Original .48 scene/resource coverage and .52 scenes are available in the JSON for explicit matching.

## Fire Emblem Awakening: 4× historical pooled map + combat

These are selected map-plus-combat cohorts, **not** isolated battle/cinematic rows. They must not be used as .58 per-scene deltas. The original .48 Combo OFF/ON resource mapping is the corrected session join. Existing finer aligned comparison is copied into JSON without rescoring.

| Renderer | Build | Low / average / high | App cores | GPU busy |
|---|---|---:|---:|---:|
| Native full ON | Original .48 | 80.82 / 119.12 / 144.86 | 0.916 | 98.22% |
| Native full ON | CPU-only .48 | 79.98 / 120.95 / 142.59 | 0.766 | 98.85% |
| Native full ON | .52 | 59.61 / 75.21 / 90.64 | 0.522 | 99.21% |
| Combo CPU vertices OFF | Original .48 | 70.86 / 113.30 / 149.52 | 0.840 | 92.77% |
| Combo CPU vertices OFF | CPU-only .48 | 73.82 / 112.70 / 150.75 | 0.677 | 98.13% |
| Combo CPU vertices OFF | .52 | 61.88 / 84.00 / 103.44 | 0.587 | 98.74% |
| Combo CPU vertices ON | Original .48 | 73.87 / 118.56 / 142.33 | 0.981 | 94.49% |
| Combo CPU vertices ON | CPU-only .48 | 72.78 / 117.43 / 152.29 | 0.768 | 99.70% |
| Combo CPU vertices ON | .52 | 56.74 / 75.10 / 101.39 | 0.548 | 97.91% |

The retained .52 FEA cohorts are materially slower than original .48, with GPU busy near saturation and lower app CPU. CPU-only .48 preserves broadly similar speeds with lower process CPU in these mixed cohorts. This is evidence of a useful direction, not proof of equal-work CPU efficiency: coverage differs and one strict CPU-ON resource subset lacks combat.

## Sonic: 2× historical race context

Post-marker/post-onset cohorts are shown for continuity; JSON also preserves their narrower confirmed-race subsets. Different driving and race boundaries prevent exact replay comparisons.

| Renderer | Build / session | Low / average / high | App cores | GPU busy |
|---|---|---:|---:|---:|
| Native full ON | Original .48 / 8 | 60.73 / 74.92 / 89.69 | 1.317 | 52.07% |
| Native full ON | CPU-only .48 / 6 | 58.57 / 75.00 / 111.14 | 1.317 | 50.61% |
| Native full ON | .52 / 5 | 57.61 / 74.65 / 100.49 | 1.320 | 51.86% |
| Native full ON | .52 / 6 | 60.38 / 73.20 / 90.41 | 1.317 | 51.14% |
| Combo CPU vertices OFF | Original .48 / 13 | 96.08 / 109.30 / 131.51 | 1.201 | 65.15% |
| Combo CPU vertices OFF | CPU-only .48 / 1 | 99.48 / 114.90 / 130.49 | 1.190 | 70.24% |
| Combo CPU vertices OFF | .52 / 2 | 97.78 / 119.25 / 165.12 | 1.196 | 67.28% |

## Sonic: 4× Combo CPU vertices OFF

| Build / reuse condition | Low / average / high | App cores | GPU busy |
|---|---:|---:|---:|
| Original .48 | 64.58 / 90.62 / 99.54 | 1.079 | 92.15% |
| CPU-only .48 | 68.36 / 89.53 / 107.28 | 1.046 | 93.48% |
| .52 OFF old | 86.74 / 93.91 / 108.95 | 1.137 | 87.67% |
| .52 ON | 76.81 / 95.25 / 108.58 | 1.130 | 87.99% |
| .52 OFF fresh clean | 75.08 / 91.61 / 98.54 | 1.127 | 89.04% |
| .52 OFF stress | 76.73 / 92.65 / 108.87 | 1.160 | 89.45% |

All rows use Vulkan 4×, simulated GPU timing, overlap and parallel decode ON, normal/turbo caps 100/400, application-cache cold. The original strong .52 reuse-OFF low was not reproduced by the later clean OFF run; do not turn that earlier result into a guaranteed .52 gain or a proven reuse-ON regression. The incident-heavy run remains a separate partial cohort.

**Xenoblade:** no .46/.48/.52 benchmark was found in the reviewed retained reports. No fabricated comparison to .58 is supplied.

**Source files and provenance**

- summary.json (source not included in this compact archive)
- REPORT.md (source not included in this compact archive)
- HISTORICAL_DARKMOON_TABLE.json (source not included in this compact archive)
- CONTROLS_SUMMARY.json (source not included in this compact archive)
- OWNER_GAMES_DATA.json (source not included in this compact archive)
- INVENTORY.json (source not included in this compact archive)
- CORRECTION_ORIGINAL048_RESOURCE_MAPPING.md (source not included in this compact archive)
- REPORT.md (source not included in this compact archive)
- FINAL_REPORT.md (source not included in this compact archive)

All selected metrics, available old all-core/resource context, exact source paths and SHA-256 pins are in LATER_VERSIONS.json (source not included in this compact archive). Historical driver identity, missing scenes and five-second-only limitations are explicit.


## Recommended next implementation focus

1. Repair the concrete host-versus-guest topology admission error that prevents Worker CPU vertex upload and Indexed CPU output from executing. Keep correctness/ownership bounds and require actual eligible work before judging throughput.
2. Retain Fast CPU vertex conversion and conservative triangle rejection as game/path-specific candidates. Move valid rejection earlier where it avoids packing, rather than paying most of the pipeline before discarding output. Fire Emblem profile regressions prevent blanket defaults.
3. Reduce low-hit vertex-reuse overhead and make specialization admission/churn cost-aware. A larger bank improves coverage but is not a universal speed fix; preserve full and partial/static shader availability separately.
4. Investigate host submission/preparation/waits and diagnostic hot-path cost alongside GPU efficiency. Current Dark Moon’s slower scenes and lower GPU busy do not support treating all remaining loss as GPU saturation. Existing overlapping timers cannot provide an additive exclusive-cost budget.
5. Fix the recorded PipelineCache shutdown join before the next broad unattended campaign. Three prior DM/Xenoblade exits hit that ANR; no feature-specific cause was established.

Mono remains a separate guest-input experiment; the logs do not show it eliminating command lists or guest eye draws. Shader pruning likewise needs removed-code and exclusive-cost evidence before claiming saved GPU work. No production fix or new build was submitted in this testing task.


# Retained Thor mode-4 evidence — October 7, 2026 recovery review

<!-- CodexAstraLocal: Independently recheck retained temporal evidence and complete the interrupted mode-4 analysis without operating the device or changing the original report. -->

**Moon flashing persists in 0.1.23 Combo Generic mode 4. Ghost corruption remains
unresolved, and 2x normal-speed qualification has not passed.** Consecutive moon
frames show repeated segment brightness reversals and changing fine stripes.
The ordinary log records active ready GPU vertices, zero optional fragment
requests/modules, and zero computed rectangles. Neither optional specialized
fragments nor an executed compute-rectangle route is necessary for this observed
moon symptom. This does not identify the responsible draw or shader behavior.

This is an offline review of the completed October 6 run, not another device run
or a new candidate. The root coordinator remains the sole device operator. The
original `docs/UBERHAR_DEVICE_TEST_2026-10-06.md` and private source artifacts were
not edited. The owner's correction governs interpretation: Native has no observed
moon glitch; Combo flashes. Shared facets in individual stills did not test that
temporal symptom.

## Run identity, scope and conditions

<!-- CodexAstraLocal: Bind statistics to the effective logged configuration and actual scene boundaries; distinguish verified settings from unobserved hardware conditions. -->

The selected run is **0.1.23, build
`99fe4a8851ff696219dddda58fb154fbf8ce1149`, session 2** in
`final-023-mode4-log.txt`, SHA-256
`50fe59360a54a98f280c73eb71da1032eb8203d85466ac9e9a099952eb01a581`.
The installed-version record identifies version code **33972593**. This report
does not revalidate the current device state or repeat release/signature checks.

- Effective start/final settings: **ComboGeneric mode 4, Vulkan, fixed 2x,
  enabled 100% limit, CPU clock 100%, JIT and accurate multiplication enabled**.
  `ready_gpu_vertex_policy=independent_lists_v2` and
  `ready_gpu_fragment_policy=generic_control`; no observed settings changes or
  temporary-speed frames occur in the selected windows. Generic optimization
  remains disabled, while optional ready shader policy is
  `optimized_background_v1` with its optimizer enabled.
- Application title-cache inventory starts empty: zero generic, driver and
  specialized files. Final generic reuse is 0 hits / 3 misses. This does not
  establish the state of the driver's private cache. The prior operator recorded
  title-specific cache preparation and an empty in-game File 1; this review
  changes no save, state, cache or app data.
- Opening boundary: **22:19:19–22:23:20 EDT on October 6**. Full log windows fall
  within 22:19:19.693–22:23:16.815. The earlier launch/menu interval beginning at
  22:14:17 is setup. The first Back input supplies the conservative endpoint;
  logged frontend pause begins at 22:23:24.608 and lasts **120.932 seconds**.
  All paused exit time is excluded. Normal native return is 22:25:25.714.
- Recorded video intervals: **22:19:40–22:19:52** for the moon and
  **22:22:20–22:22:32** for ghosts. Capture start times are operator boundaries,
  not exact guest-frame timestamps.
- Retained readings show AC power, battery 80%, power saver off, battery
  **26°C before/early run, 27°C later gameplay and 28°C after return**. Android
  thermal status is 0, but the thermal HAL is unavailable and thermal headroom
  is unknown. Hardware performance/fan policy and GPU clock are unobserved.

The lifecycle clock anchors disagree by only 0.277 ms across this session.
External capture timestamps mark the start of sequential commands, so their
memory values are not atomic snapshots at that exact time.

## Temporal moon result and separate ghost result

<!-- CodexAstraLocal: Use ordered decoded frames to test flashing, retain Native as the owner's reference, and avoid clearing ghost corruption from a mismatched still. -->

The retained contact sheets were visually re-inspected with `view_image`.
Independent `ffprobe` metadata checks preserve decoded frame indices and relative
presentation timestamps from the original clips. The following observations are
about ordered sequences, not isolated facet appearance.

| Evidence | Reviewed sequence | Observation |
| --- | --- | --- |
| Full Combo `moon-023.mp4` | Frames 0–15, 0.000–0.622 s; frames 60–75, 2.909–3.655 s | Individual upper/right/lower facets reverse brightness between successive frames while the camera and surrounding scene advance gradually. The changes recur beyond the initial flare. |
| Mode 4 `uberhar-023-mode4-moon.mp4` | Frames 60–75, 2.707–3.428 s; frames 120–135, 5.490–6.218 s | Segment instability persists. Narrow dark stripes appear, disappear and change placement across consecutive right/lower facets; broad brightness changes also recur. |
| Native `moon-023-native.mp4` | Frames 155–170, 7.329–7.999 s; frames 232–247, 10.932–11.712 s | Principal facet relationships evolve coherently with scene motion. No matching recurring Combo-style reversal pattern is identified in these reviewed intervals. |

Native frame 165 contains one rectangular block-like disruption absent from the
neighboring frames. Its origin cannot be assigned between capture/encoding and
rendering from this file. It does not establish recurring Native moon flashing
or override the owner's direct observation. These lossy variable-frame-rate
recordings do not support a calibrated flashing frequency, exact guest-frame
alignment, or hardware-perfect reference claim.

The **ghost finding remains separate**. The prior full-Combo
`candidate-opening-04/screen.png` shows dense irregular near-black patches on
the right book-holding ghost's crown, face and upper body. In mode 4,
`mode4-opening-06/screen.png` shows continuous green/transparent shading on that
ghost, but the camera, head pose, animation and foreground book differ.

The retained full-Combo ghost clip centers on E. Gadd and a background ghost;
the mode-4 clip covers a later close view near the chalkboard. Mode-4 frames
45–60 at 4.222–5.450 s show coherent shading on the visible rear ghost, and the
retained wider scene sequence provides context. This is useful positive local
evidence but **does not temporally clear the earlier wide book-holder defect**
or prove specialized fragments caused it. Broad bands aligned with visible
background geometry are compatible with transparency and should not be scored
as the same dense irregular corruption.

## Normal-limit timing

<!-- CodexAstraLocal: Complete the pending aggregation with wall-weighted window statistics, explicit video exclusion, and limits on stall/distribution claims. -->

Two offline computations agree. Only complete approximately five-second normal
windows wholly inside the opening boundary are included; menu/setup, mixed
settings, temporary speed and exit pause are excluded. `speed_percent` is the
existing emulator guest-time/wall-time metric, not an inference from the visible
FPS overlay or physical display cadence.

| Statistic | All complete scene windows | Excluding any video overlap | Video-overlapping windows only |
| --- | ---: | ---: | ---: |
| Window count | 47 | 40 | 7 |
| Observed wall seconds | 237.123 | 201.943 | 35.180 |
| Weighted achieved speed | **19.517%** | **18.713%** | 24.134% |
| Wall-weighted p05 window speed | 13.737% | 13.646% | 16.619% |
| Minimum window speed | 13.454% | 13.454% | 16.619% |
| Maximum window speed | 35.182% | 35.182% | 34.053% |
| Seconds in windows averaging below 95% | 237.123 (100%) | 201.943 (100%) | 35.180 (100%) |
| Game submissions | 2,764 | 2,256 | 508 |
| Game submissions per second | 11.656 | 11.171 | 14.440 |
| Largest logged interval | 230.112 ms | 230.112 ms | 105.211 ms |

The slowest complete window is **22:21:56.011–22:22:01.105**, averaging 13.454%
(log line 4260). The largest observed interval occurs in
**22:19:24.728–22:19:29.742** (line 3284). These are logged emulator intervals;
there is no per-frame instantaneous-speed distribution or display-latency trace.
The below-95 duration and p05 describe window averages only.

Video-overlap exclusion removes entire intersecting windows, so 24 seconds of
nominal recordings exclude 35.180 seconds of window data. The scene mix changes,
and screenshots/readings remain; the difference cannot measure recording
overhead. These longer, differently phased mode-4 observations also cannot
establish a controlled regression or speedup against the earlier 22.165%
full-Combo result. **Sustained 100% at 2x is clearly unqualified.**

## Memory and effective routes

<!-- CodexAstraLocal: Keep overlapping counters separate, distinguish the paused dialog from recovery, and inspect actual work rather than nominal route labels. -->

Values below are MiB. RSS and system availability accompany the selected KGSL
sample; they are not independent extrema. Availability consistently uses
`/proc/meminfo`. KGSL, CPU-mapped KGSL, RSS and Vulkan counters overlap and must
not be added.

| Sample, EDT | Own-process KGSL | Process RSS | System available |
| --- | ---: | ---: | ---: |
| External prelaunch, 22:14:03.468 | 44.12 | 412.61 | 11,656.40 |
| Before native run, 22:14:17.464 | 12.73 | 416.66 | 11,663.39 |
| Highest scene sample, 22:22:47.535 | 2,783.80 | 988.35 | 8,333.40 |
| Highest whole-session KGSL, paused exit, 22:23:47.544 | 2,789.23 | 1,001.29 | 8,281.02 |
| Normal native return, 22:25:25.714 | 10.14 | 492.07 | 11,381.97 |
| Verified post-return capture, 22:25:52.880 | 11.45 | 476.28 | 11,517.93 |

`mode4-after/screen.png` still displays the **Close Game** confirmation and its
memory reading is not recovery. `mode4-after-verified/screen.png` shows the title
panel over the library after the logged normal return. Teardown reports zero
live stream bytes, VMA allocations, descriptor pools and command-buffer capacity;
one 64 MiB VMA block remains before allocator destruction. Stream peak is
604.125 MiB and VMA sampled block peak is 224 MiB. Recovery is observed, but one
short run and brief plateaus do not establish repeated-content/full-game bounds.

Whole-session routes include setup and exit:

- **1,076,965 ready GPU-vertex batches**, 25 successful ready pipelines; zero
  failed draw attempts/key mismatches. Cumulative ready driver build time is
  16,576.543 ms, maximum 1,443.016 ms. These asynchronous worker totals are not
  direct frame-blocking or GPU execution measurements.
- **Zero optional fragment requests, modules or builds.** All 25 ready pipeline
  detail records mark optional specialization false.
- **Zero eligible/computed rectangles and zero compute pixels.** The 6,266,190
  CPU-path batches considered for virtual rectangle rendering are unsupported
  for that route. The nominal mode retaining rectangle capability therefore
  does not mean this run executed it.
- Quaternion-interpolation recovery accounts for 2,174,101 GPU input fallbacks.
  There are no missing/unconfigured/short-mapped vertex attributes or escaped
  indices in the reported counters. No lit accelerated draw snapshot is retained;
  bounded snapshots do not identify the moon or ghost draw.
- Five error-level startup records describe three filesystem delay-generator
  fallbacks and two absent other-profile save files, all before the scene. No
  error/critical record occurs inside the selected opening; this does not certify
  rendering correctness. Generic failure/unavailability counters are zero.

## Pipeline-associated memory hypothesis

<!-- CodexAstraLocal: Recompute attachment-population joins from raw records and separate a useful aggregate fit from unsupported driver-allocation ownership claims. -->

Independent joins reproduce the earlier full-Combo comparison: 0.1.22 and 0.1.23
each end with **32** ready GPU pipelines, and all 32 displayed tuples match on
shader identities, stages, optional-specialization flag, topology, binding and
attribute counts, and color/depth formats. This is not equality of every pipeline
descriptor field or compiled SPIR-V. Both populations contain 18 color-attached
and 14 color-absent pipelines.

The earlier aggregate hypotheses, **96/72 MiB per color-attached/absent pipeline
for 0.1.22** and **72/48 MiB for 0.1.23**, leave similar late-scene residuals
around 1,080 MiB. At 32 completed pipelines the sampled KGSL difference is
772.492 MiB, close to the model's 768 MiB population difference. Agreement also
appears at intermediate matched populations. This supports investigation of
pipeline-associated driver resource cost; it is not a measured allocation size
or proof that optimization caused the difference.

Mode 4 has **25** ready pipelines: 14 color/depth, 1 color-only, 4 depth-only and
6 with neither attachment. Only **6 displayed tuples** match the earlier
0.1.23 full-Combo population, so its lower sampled maximum cannot be attributed
to removing optional fragment specialization at otherwise equal populations.
Applying the same 72/48 MiB hypothesis illustrates the limit:

| Mode-4 completed population | Color / absent | Observed KGSL, MiB | Model residual, MiB |
| --- | ---: | ---: | ---: |
| 23, 22:20:17–22:21:25 | 13 / 10 | 2,638.35–2,639.23 | 1,222.35–1,223.23 |
| 24, 22:21:47–22:22:17 | 14 / 10 | 2,711.39–2,711.70 | 1,223.39–1,223.70 |
| 25, 22:22:47–22:23:17 | 15 / 10 | 2,783.80 | 1,223.80 |

Successive one-color-pipeline additions accompany roughly 72 MiB increases,
while the late mode-4 residual is about 146 MiB above full Combo's value at
24 pipelines. Earlier menu/opening residuals differ again. A single fixed
intercept across menus, modes and scene phases is inappropriate. Completion
records may follow allocation, asynchronous jobs can overlap a sample, and
the driver does not expose allocation ownership. No memory cure or persistent
post-exit leak is established.

## Next informative checkpoint and evidence custody

<!-- CodexAstraLocal: Recommend bounded discriminating work within Combo qualification and leave raw game-derived data private. -->

For the next candidate, retain 2x Vulkan/100% and score **moon and ghost
separately**. Capture the moon immediately after its initial flare fades, while
the crescent is large, through the pullback. Inspect consecutive frames rather
than requiring identical static facet brightness. Capture the **earlier wide
laboratory shot with the right book-holder**, through its face/book rotation;
the existing mode-4 later chalkboard clip does not replace that checkpoint.
Use visible cues rather than a fixed delay after Start because achieved speed
differs substantially. Keep timing intervals and recording intervals explicit.

Mode-4 persistence narrows the moon investigation toward GPU vertex output or
other remaining Combo differences. The absent compute-rectangle execution
removes that executed route as an explanation for this run. If source review
cannot tie a proven discrepancy to these draws, the handoff's default-off,
bounded successful-draw capture and matched CPU/GPU replay is more informative
than another unmatched image or a blanket output-register-default change.
Preserve within-batch state and indexed invocation order. For ghosts, first
obtain matched temporal evidence of the wide book-holder before promoting the
optional-fragment hypothesis to a cause or a fix. No new Native run is needed
for this evidence review; the standing scope requires both modes if Native's
process changes.

Raw sources remain in ignored `build/device-testing/20261006-thor/`. Reviewed
images are the `temporal-moon/` contact sheets and the specifically named
checkpoints above. New private derivatives are under
`build/device-testing/20261007-recovery/analysis/`:

- `mode4-existing-analyzer.json/.txt`: original offline analyzer rerun into new
  output paths with explicit scene/video bounds; historical attribution retained.
- `recovery_analysis.py` and `mode4-independent-summary.json`: independent
  window aggregation, raw pipeline/health joins, clip hashes and decoded-frame
  timestamp metadata. The script never contacts ADB and refuses an existing
  output path. Re-run with a fresh `--output` path to preserve prior artifacts.

No raw images, clips, game-derived shader payloads, device identifiers or storage
paths are embedded in this report. The source log hash above and line references
make the derived findings auditable without publishing private artifacts.

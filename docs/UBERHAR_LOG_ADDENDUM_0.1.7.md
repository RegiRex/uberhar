# Supplemental 0.1.7 evidence — retain 0.1.8 for device testing

<!-- AstraPro: Evidence-only update, not a renderer change, new build or full-review reset. -->

## Decision

Keep released 0.1.8 (`79bb8bc780480bb5843bd69eafc6d5d510d7256f`) unchanged for its first device comparison. Do not publish 0.1.9 from this additional 0.1.7 log alone. The new evidence strengthens the GPU-vertex experiment and identifies independent logging/measurement follow-up; it does not measure the new route or demonstrate a new-build correctness blocker.

The owner explicitly authorized either a new build or an evidence-only update. This is the latter. No executable source, version file, signing configuration, shader guards or workflow gates change.

[0.1.8 Actions run 36772998677](https://github.com/RegiRex/uberhar/actions/runs/36772998677) completed successfully, including Android, shader/pixel and publication jobs. The full-fragment color/depth gate passed in CI; the earlier interrupted local comparison remains an incomplete local result. The [0.1.8 ARM64 prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.8) was published on September 30, 2026 at 20:45:35 UTC. This establishes APK availability, not Thor GPU-vertex pixel parity or a device speedup.

## Evidence identity and extraction

<!-- AstraPro: Keep app-process identity, cache context and speed bands separate. -->

New source: `uberhar_log_9_30_1227_RER_LCUTCB_FEA_SASRT.txt`, process start September 30 at 12:27:06 -04:00. Six complete sessions, all Native/Vulkan at 2x, all build `e8b53b12db77b7b8f0345cbcb81038947782baf7`. This precedes the previously supplied 13:37 process. Together there are 13 distinct 0.1.7 sessions across nine titles. Identify a session by input-file identity plus process-local session ID; do not merge matching numeric IDs across logs.

Use final normal/fast bands independently. A complete capacity window requires min=max=400 and temporary_frames=frames. Weight windows by observed_wall_ms. Do not sum cumulative progress records with totals, identify gameplay merely from fast-forward, infer thermal causality, or fabricate frame-time percentiles from five-second means. Recognized pauses are already excluded; unmarked idle time is not guessed. There are 2,211.905 seconds of observed emulation in this additional file.

| Session | Test, all 2x Native | Normal speed / seconds | Commanded 400% speed / seconds |
| --- | --- | --- | --- |
| 1 | Resident Evil: Revelations, empty application cache at start; mixed encountered module reuse | 96.602% / 545.587 | 391.964% / 22.263 |
| 2 | LEGO City Undercover: The Chase Begins, cold | 99.424% / 17.063 | 238.677% / 257.185 |
| 3 | Fire Emblem Awakening, cold | 98.197% / 72.950 | Not sampled |
| 4 | Fire Emblem Awakening, warm | 98.320% / 12.359 | 258.159% / 30.480 |
| 5 | Sonic Racing Transformed, cold longer run | 56.110% / 704.767 | 62.733% / 275.406 |
| 6 | Sonic Racing Transformed, warm | 53.135% / 258.561 | 299.096% / 14.719 |

These bands contain different scene mixtures and unequal exposure. They are not causal comparisons of enabling the limiter and not controlled version-to-version benchmarks. Speed is emulation speed, not physical display FPS.

Audit: run headers at physical source lines 140, 73764, 75276, 75826, 76266, 81362. Final speed bands: 73622–73623, 75118–75119, 75688, 76116–76117, 81221–81222, 82819–82820. Some viewers include a final empty line beyond the 82,841 nontrailing physical lines.

## New conclusions

<!-- AstraPro: These observations support design choices without certifying 0.1.8. -->

### FEA 2x baseline is present

Cold normal is 98.197%. After the first two complete windows, eleven complete windows cover 55.151 seconds at a weighted 99.735%; retain the full-run average alongside this explicitly selected post-startup result. Cold generic waits total 925.738 ms, maximum 255.126 ms, and the largest system-frame interval is 570.249 ms. Warm generic waits fall to 63.695 ms total, maximum 6.370 ms. Five complete warm fixed400 windows average 248.385%, range 179.323–318.075%. Near-real-time behavior is supported, but perfect pacing and sustained 400% are not certified. No repeat of 0.1.7 is needed merely to supply the missing 2x baseline.

### LEGO supports broader throughput work

The 257.185-second accelerated band is a materially longer capacity sample than the earlier brief exposure. Its 49 complete fixed400 windows span 245.363 seconds, average 236.793%, and range 150.526–388.749%. Scene phases remain Unknown.

CPU vertex stage: 136.567 seconds, 49.786% of observed session wall; 895.718 million CPU vertex shader invocations; zero legacy-input invocations in the no-GS transport. Specialized recovery: 42.401% of bound draws, including 792,598 Shadow2D and 151,760 SPIR-V-incompatible exclusive first-failing reasons. These are draw counts, not pixel/time weighting or all-feature census. Keep specialized fragments available while testing GPU vertices.

### Sonic exposes costs beyond shader discovery

The longer cold run's 51 complete fixed400 windows span 255.917 seconds at a weighted 55.323%; 50 of 51 are below 100%. A single 339.722% window does not outweigh sustained slow windows, whose lowest mean is 38.263%. The warm run's 299.096% fast band lasts only 14.719 seconds near its beginning and does not characterize later demanding work.

The longer run records 46.083 million bound draws, 2.829 billion CPU vertex shader invocations and 470.074 seconds inside the vertex stage (47.939% of observed wall). Warm: 711.612 million invocations, 123.758 seconds in the stage (45.279%). Both have full fused no-GS input coverage, no specialized fragment recovery, and no scheduler pipeline waits. Warm generic waiting totals only 60.141 ms, maximum 6.070 ms. Repeated long compilation cannot account for the sustained slowdown by itself.

Stage spans include descheduling/stalls; these are not CPU utilization, GPU execution time, complete critical-path profiles or speedup predictions. Do not label the remainder GPU-bound by subtraction. Actual Combo admission and total scene speed must decide whether to expand coverage or profile command recording, GPU work and waits.

### Resident Evil reveals diagnostic pollution

Only two complete fixed400 windows qualify, at 399.941% and 399.970%. This proves limiter saturation for those intervals, not every combat scene.

There are **70,924 identical SRV::PublishToSubscriber warnings for notification 0x216, flags=0**, all in this session: 85.615% of the supplemental file's lines. Source range 456–73587. The inherited service still emits these without bounded sampling in 0.1.8. This establishes log volume, not measured logging time or an FPS improvement from suppression.

A 10.103-second system-frame interval overlaps software-keyboard validation activity around log seconds 40–45 and has zero submissions in its worst-frame record. Keep it in raw totals and flag the context; it is not established to be a renderer hang. Instrument actual modal/applet entry and exit rather than removing all no-submission intervals.

The 975 CPU JIT programs are not equivalent to optional GPU VS/pipeline cache demand. Do not raise the new GPU cache caps merely by comparing unlike keys/counts; inspect actual cap/reuse diagnostics in 0.1.8.

## Coverage and device caveats

All six new sessions have short_mapping=0 and legacy_vertices=0 in the no-GS fused-input path. Index rescue has no observed opportunity in these runs; prior Kid Icarus/MH4U evidence still motivates that change. The old log cannot prove GPU topology eligibility, ready-dependency coverage or numerical parity. Its pipeline counters report zero skipped draws and zero fallback compilation failures, not exhaustive image correctness.

The long Sonic run excludes 1,115.084 seconds of recognized pauses from its 980.571 observed seconds. FEA cold separately excludes 83.184 seconds. Do not count those pauses as slow gameplay.

The final warm Sonic health sample is 2% battery, unplugged, battery sensor 36 C (source line 82761). Earlier Resident Evil samples are 26–30 C. Thermal status is zero and headroom unknown; these records lack reliable SoC thermal/clock/host-mode evidence. This is a reason to standardize future comparisons, not a diagnosis that heat or low battery caused the slowdown.

## Conditional 0.1.9 work

<!-- AstraPro: Proposed implementation order; none of these tasks is claimed delivered by this document. -->

1. Address any 0.1.8 GPU-route visual/lifecycle regression before broadening admission. Preserve Native as the control.
2. Bound repeated notification warnings with per-service/per-key counts, limited detail and suppressed totals. Preserve every IPC response, existing service behavior and the special 0x203 shutdown action. Test repeated calls and resets. Measure logging cost before claiming a performance gain. Improve actual applet/modal timing boundaries separately so user interaction does not contaminate renderer-stall attribution.
3. Use actual GPU acceptance, topology/dependency/upload/cap rejection counts and total matched-scene speed to choose further performance work. Low coverage can justify one exact extension; high coverage without benefit calls for CPU/command/GPU/wait attribution. Do not remove Shadow2D/AddSigned/gas guards to make coverage look better.

These are candidates rather than a promise to fit every item into 0.1.9. No new renderer build is started here. 0.1.8 remains successor two after the full 0.1.6 architecture review; default review after 0.1.10 evidence, allowed after 0.1.9–0.1.11, before 0.1.12 work.

## Next test

Do not repeat this 0.1.7 batch. Test released 0.1.8 on a cooled Thor with fixed host performance/fan settings, consistent charging and adequate battery. Keep the short FEA 2x Native regression and a matched Dark Moon Native/Combo pair. Kid Icarus remains the index-rescue/touchscreen case. Add one short Sonic or LEGO capacity pair when useful, not an all-title marathon.

Compare like cache states and scenes; hold 400% for capacity segments, use 100% for ordinary responsiveness, and keep 4x/200% usability separate. Verify actual GPU-vertex promotions, not rectangle-compute counts. Unmarked phases remain Unknown. The maintained roadmap and owner evidence package contain the full per-window extraction and LEGO plot.

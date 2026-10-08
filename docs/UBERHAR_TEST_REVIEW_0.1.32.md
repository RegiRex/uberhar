# Uberhar 0.1.32 Thor test review

<!-- CodexAstraLocal: Separate verified delivery and one cold control per method from product qualification. -->
The four requested Dark Moon opening controls completed on the Thor Max with
normal returns. This report covers released source
`6486198d52df144e184165133c716ffa01a05f26`, version **0.1.32 / 33984206**.
All 22 shader, Android, signing and publication checks passed in
[run 37750392228](https://github.com/RegiRex/uberhar/actions/runs/37750392228).
Exact release source, APK checksum, package and signing compatibility were
verified before a data-preserving update. APK SHA-256:
`bee332a5504a9323609f5bb7557e16319fb0c70f4772391fdb6b2be108bc040a`.
The original protected .22 state remained unchanged; no uninstall was used.

Each method used a fresh app process, UI reset of Dark Moon File 1 and title
Vulkan-cache deletion. Empty/Start were inspected and all three application
cache inventories were complete and zero at launch. Settings remained Vulkan,
2x, normal 100% limiter, CPU 100%, accurate multiplication enabled. No saved
application shader cache was available; driver-private cache contents are unknown.

<!-- CodexAstraLocal: Report whole-window timing and its observer/scene limits rather than claiming controlled speedup. -->
## Performance

| Method | Mean normal speed | p05 window speed | Worst interval | Accepted frame-window seconds | Inclusive CPU vertex wall |
| --- | ---: | ---: | ---: | ---: | ---: |
| Calculated | 20.957% | 15.946% | 310.737 ms | 201.613 | 83.665% |
| Native | 20.964% | 16.042% | 285.255 ms | 201.623 | 83.831% |
| Full Combo | 23.185% | 19.986% | 387.873 ms | 201.345 | 76.079% |
| ComboGeneric | 19.455% | 13.791% | 345.083 ms | 201.797 | 58.407% |

Each opening spans approximately 206 seconds from actual Start input to first
Back. Forty complete reporting windows are accepted per method; setup and later
exit navigation are excluded. Every accepted window averages below 99%. The
fifth percentile is wall-weighted over window averages, not individual frames.
Complete-window inclusion uses retained clock-offset bounds with unmeasured
log-emission delay. Recording and
observation overlaps are separately classified in retained analysis; different
scene mixtures prevent causal mode/version or observer-overhead conclusions.
These results remain near .31 and do not qualify sustained 99%, 4x or 0.2.0.

The CPU vertex percentages use separate report windows. `LoadVertices` includes
preparation, fetching, shader execution, FIFO/output/assembly and scheduling,
and ends before `DrawTriangles`. It is neither exclusive shader CPU time nor
GPU time; its residual is not a GPU measurement. Optional log omissions and
missing census summaries remain explicit in the retained per-method analyses.

<!-- CodexAstraLocal: Distinguish actual route use, raw state counts and lifecycle totals from scene-specific benefit. -->
## Rendering coverage

Calculated prepared its compute pipeline, but all **6,646,279 lifecycle attempts**
failed effective state admission. Geometry/format qualification, eligible draws,
compute draws and compute pixels were all zero. Delivered raw-mask details still
contain at least 1,126,850 six-vertex ColorWrite/Blend cases; their enabled
channels, equations/factors and endpoint/TEV subreason are not recorded. The
host-validated .32 extension did not improve this title's calculated coverage.
Five complete delivered opening census windows contain 1,239,203 attempts, all
state-rejected; an omitted summary prevents a complete opening histogram.
Lifecycle totals include menus and exit and must not be presented as opening counts.

Full Combo selected ready CPU-fragment pipelines **607,569** additional times
between two bounded opening interior records, including **404,775 lit** draws.
The interval adds 42 creation attempts, retirements and destructions while the
physical bank stays within the eight-owner cap. Its lifetime 64-attempt limit is first observed
after first Back; the evidence does not prove opening exhaustion. Selection
counts establish use beyond menus, not actor identity, pixel benefit or speedup.
Native and ComboGeneric retain their intended disabled optional fragment routes.
All four lifecycle route totals close with zero reported skipped draws, required
fallback faults or compute dispatches. That is bounded log evidence, not a
proof of all-game correctness.

<!-- CodexAstraLocal: Keep consecutive-frame event counts and unequal scene coverage separate from correctness or comparative-frequency claims. -->
## Visual evidence

All eight retained clips across the four controls received independent
consecutive-frame review.

| Method | Moon frames / isolated events | Ghost frames / abrupt block events |
| --- | ---: | ---: |
| Calculated | 345 / 0 | 580 / 4 |
| Native | 348 / 2 | 576 / 2 |
| Full Combo | 347 / 0 | 609 / 0 |
| ComboGeneric | 344 / 2 | 437 / 0 |

Each identified moon event occupies one decoded frame and clears in the next;
none reaches the owner's escalation threshold. The separate ghost events in
Calculated and Native are isolated single-frame chalkboard-ghost blocks with
clear neighbors. The prior dense irregular ghost patches were absent in covered
near, foreground, book-holder and electrical/rear cores. Combo's near core is
only a short clip tail; it alone reaches the later close electrical/ghost phase,
with bright effects limiting visibility. Generic reaches only the early rear
phase. Post-shock and remaining late/broom scenes are unqualified.

Different scene mixtures prevent comparative event-frequency claims. Decoded
frames and presentation timestamps do not establish guest-frame duration,
physical panel persistence or whether the renderer or capture caused an event.

<!-- CodexAstraLocal: Preserve ordinary sampled conditions without reviving routine exit-memory qualification. -->
## Conditions and memory

| Method | KGSL kernel allocations at +40 → +165 seconds | RSS at +40 → +165 seconds | Battery temperature |
| --- | ---: | ---: | ---: |
| Calculated | 1073.73 → 1073.59 MiB | 887.28 → 887.00 MiB | 30 C |
| Native | 1074.27 → 1074.14 MiB | 874.84 → 875.40 MiB | 30 C |
| Full Combo | 2518.81 → 2955.17 MiB | 970.82 → 966.52 MiB | 29 C |
| ComboGeneric | 2493.67 → 2716.81 MiB | 966.70 → 986.67 MiB | 29 C |

Battery was 80%, external AC power reported, saver off and thermal status zero.
Battery temperature is not SoC/GPU temperature; snapshots do not prove clock
residency. AYN Standard/Smart fan/120 Hz is the last verified configuration,
not a freshly measured vendor-mode guarantee. GPU growth during these short
openings does not establish either an endless leak or a sustained bound. Routine
post-exit memory sweeps were omitted as requested. Every method exited normally;
both panels are now off and the original 60-second timeout is restored.

Raw logs, screenshots, videos, exact action boundaries, analysis scripts and
hash manifests remain private and preserved. See the separate
[architecture review](UBERHAR_ARCHITECTURE_0.1.32.md) and
[cleanliness review](UBERHAR_CLEANLINESS_0.1.32.md). The requested review-and-stop
boundary does not qualify broader scenes, long gameplay or the retained Fire
Emblem Awakening beta baseline.

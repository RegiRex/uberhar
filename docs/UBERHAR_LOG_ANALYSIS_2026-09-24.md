<!-- AstraEH: Analysis of owner-supplied logs; raw logs and personal file paths are not published. -->
# Awakening device log analysis — 2026-09-24

Both attached logs identify source `d005385bd59ab55152af06362b1593f9c64e63d6`
(Uberhar 0.0.3), Adreno 740 with Qualcomm driver 512.676.53, Vulkan, 2x resolution,
100% frame limit, asynchronous shaders and presentation enabled, SPIR-V shader
generation enabled, and its optimizer disabled. Hybrid TEV is on in both.

| Capture | Verified evidence | Limit |
| --- | --- | --- |
| `azahar_log.txt` | Force TEV on; an 8314 KB driver cache was loaded; older VS/FS entries were regenerated. | Capture ends at 45.396 s without shutdown totals. It is not a verified cold run or complete forced-mode benchmark. |
| `azahar_log Force_Off.txt`, first launch | Explicit Vulkan cache deletions precede launch; Force TEV off; cold-run shutdown totals are present. | Measures a particular scene/settings/driver, not all game behavior. |
| Same file, second launch | Force TEV remains off; a 9767 KB driver cache is loaded. The owner reports perfect play. | Capture ends about 13.39 s after launch without second-run totals. No complete numerical warm-run comparison is possible. |

## Verified cold-run totals

| Counter | Value |
| --- | ---: |
| Draw requests | 195,671 |
| Specialized pipeline pending observations | 721 |
| Fallback draws selected | 78 |
| Fallback warming observations | 93 |
| Fallback unavailable observations | 489 |
| Deferred observations (subset of unavailable) | 470 |
| Skipped draws | 0 |
| Fallback fragment families / pipelines | 28 / 33 |
| Scheduler waits | 121 |
| Scheduler wait sum | 18,221.986 ms |
| Maximum scheduler wait | 1,083.535 ms |
| Waits at least 50 ms | 68 |
| Forced-fallback wait time | 0 ms |
| Fallback fragment preparation total | 451.706 ms |
| Fallback pipeline build wall time | 14,260.207 ms |

These are draw observations, not unique shader counts or frame counts. Pipeline
build wall time includes shader dependency waits and may overlap scheduler waits;
it must not be added to the 18.22 s total. Driver time cannot be separated from
vertex/geometry dependencies using the 0.0.3 counters. Missing saves and blacklisted
Vulkan extensions are not evidence that they caused these shader stalls.

## Source findings and response

The 0.0.3 implementation chooses its pipeline while enqueuing the draw. If the
fallback is still warming, the command worker later waits solely for the
specialized pipeline even if that fallback finishes first. It also calls the
specialized cache-only pipeline probe on the render thread. That probe prevents
compilation but does not guarantee absence of driver lock waits.

Version 0.0.5 selects the first ready compatible pipeline on the command worker,
tracks the actual bound handle, moves on-demand hybrid pipeline creation to
workers, and requests reduced optimization only for temporary fallbacks. The
single-fallback admission bound is retained to avoid flooding compiler queues.
No shader semantics/support gate is expanded in this iteration.

New diagnostics separate worker queue time, VS/FS/GS dependency waits, and the
`vkCreateGraphicsPipelines` wall time. Driver-call time may still include internal
locks and is not a pure compiler CPU measurement. Slow-build records carry a
pipeline key; long scheduler waits carry both the waited-for and chosen keys.

`pending_stages`/`alternative_stages` use bits **1=VS, 2=FS, 4=GS**. Build phases
are **0=queued/not started, 1=shader dependencies, 2=driver, 3=complete**. These
atomic snapshots can change immediately; they identify the observed state at the
start of a wait, not exclusive attribution of the entire wait duration.
`first_ready_waits` counts waits where neither candidate was initially ready.
`late_fallback_draws` counts draws that used a fallback which was unfinished when
that draw was enqueued. Normal `fallback_draws` now counts actual scheduler
selections. Progress fields can straddle concurrent completions; drained final
totals are the authoritative complete-run counters. Specialized build totals can
include startup disk-cache reconstruction. Fallback job queue time is reported
separately; its pipeline build queue value is zero because its one job prepares
the fragment and calls Build directly.

The 0.0.4 log exporter already flushes buffered entries before snapshotting.
Periodic 0.0.5 summaries add resilience when a copied log lacks final totals.
The next controlled cold/warm retest uses the supplied settings and 0.0.5's
**Current session** export; no new forced-mode run is required.

## Input identity

These hashes identify the analyzed attachments without including their contents:

```text
azahar_log.txt
 e934571cdb1187bdb41313b4db33ebc33747a06a7cca68b23e43005f44fea488
azahar_log Force_Off.txt
 479792ff7013527db31a1261bd04d82e9519957474174b062285b0a5e921536a
```

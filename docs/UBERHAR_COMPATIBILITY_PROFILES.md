# Future per-title compatibility profiles

<!-- AstraEH: Owner-requested post-1.0 roadmap, not an active workaround database. -->

Recorded 2026-09-27 after 0.1.0 Thor testing. Target **after 1.0.0** unless a small,
well-supported rule becomes worthwhile earlier. The shader-system release should
not wait for every inherited Azahar compatibility problem. No automatic override
or new compatibility setting is implemented in 0.1.1.

## Intended behavior

On launch, look up the actual title ID (including regional variants), game revision
where material, and applicable emulator/backend conditions in a bundled, reviewed
rule table. Apply a confirmed recommendation before machine initialization.
Preserve explicit user choices, record which rule applied and why, and provide an
accessible per-game override. Do not silently rewrite global settings or infer
compatibility from the title's display name, a low frame rate or a one-time crash.
The initial design needs no network service or extra Android permission.

Represent console model and emulated CPU clock as distinct decisions. A hypothetical
physical-cache flag must not be invented for a cache that the emulator does not
simulate. New 3DS-exclusive applications must never be automatically switched to
Original 3DS. Rule matching must account for newer app versions/fixes, and removing
an obsolete rule must not destroy the user's saved preference.

## Sonic candidate: investigate, do not activate

| Evidence | Consequence |
| --- | --- |
| Owner remembers forced L2 hard locks on hacked New 3DS hardware; Luma issue #742 contains a similar historical report | Preserve L2/clock distinction in research. It does not prove emulator model selection has the same effect. |
| Current source reads the title L2 request but does not simulate that physical cache | There is no equivalent forced-L2 switch to turn off as a quick fix. |
| Azahar model selection changes reported platform, core count and memory behavior | Original/New comparison may reveal a real game-specific emulator interaction. |
| Sonic CPU clock is 100%; all racing draws use supported generic shaders, with builds finished before sustained slowdown | Neither forced CPU overclock nor unsupported shader fallback is established as the cause. |
| Dense racing draw/vertex workload; newer vertex-stage improvements help other titles | Keep performance investigation separate from compatibility overrides. |

Known test ID: USA `00040000000B3500`. A published MMJ configuration also lists
Europe `000400000008FC00`; verify each affected revision/region before applying a
rule. Source links and qualifications are in the [0.1.0 analysis](UBERHAR_LOG_ANALYSIS_0.1.0.md).

## Evidence required before shipping a rule

1. Reproduce the symptom with version, backend, model, CPU clock, driver and scene
   recorded. Keep the image issue separate from low speed if they do not track together.
2. Change one setting, cold boot the title, and repeat the same section with warm
   graphics caches. Preserve normal in-game saves; do not compare states captured
   under incompatible console configurations.
3. Confirm the improvement and absence of new correctness/boot/save regressions.
4. Add a narrow rule with provenance, region/revision bounds, user override and
   regression coverage. Review it when upstream behavior changes.

The framework is manageable; reliable rules are the hard part. Start from proven
cases, rather than claiming automatic discovery of unknown incompatibilities.

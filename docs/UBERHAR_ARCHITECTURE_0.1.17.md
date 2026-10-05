# 0.1.17 source and architecture review

<!-- CodexAstraUlt-2: Review completed October 5, 2026; 0.1.18 implementation authorized by owner. -->

Reviewed source: `fe7d1b8a0b395229ca34ab79e14a005cedd4411e` on
`uberhar/hybrid-shaders`. The 0.1.17 ARM64 workflow completed successfully at
[run 37258237078](https://github.com/RegiRex/uberhar/actions/runs/37258237078).
This review traces AstraEH/AstraPro code, existing tests and committed device
analyses. The owner reports stock Ayn Thor Max/Vulkan: Dark Moon Native around
30% speed with correct images; Combo below half that speed, graphical faults and
eventual crashes. The installed version was not independently established, and
no fresh raw logs or physical-device execution were available during review.

## Evidence and source findings

<!-- CodexAstraUlt-2: Distinguish source reproduction from title-specific attribution. -->

- A host fixture with 96 vertices, ordinary position input and zero-stride color
  input reads `(1, 0.25, 0.5, 1)` through production Native input transport but packs
  `(0, 0, 0, 1)` through extracted production GPU input code. Existing promotion
  admits the layout. Mocked device plumbing does not establish Dark Moon coverage.
- Mandatory fragment compilation can publish completion after null module creation;
  mandatory pipeline jobs lack failure containment. A later driver error can escape
  a worker instead of reaching the frontend's typed error boundary. Any repair must
  also wake producer/presentation waits and avoid canceled GPU-tick waits.
- A production-queue reproduction confirms log flushing can block acquiring its
  insertion lock before its five-second completion timer starts. Full queue plus
  a stalled producer is sufficient; occurrence on the Thor has not been proved.
- Interrupted-log preservation copies/syncs full text on the startup thread.
  Frontend callers require synchronous directory initialization; merely moving
  that entire call to a thread would create readiness races. Incident staging
  before rotation is the focused alternative selected for 0.1.18.
- Earlier pending-uniform, fixed-attribute reservation and generic-worker input
  snapshot/drain corrections are present. The review did not reclassify those
  already-corrected issues as new open defects.

The committed [0.1.13 review](UBERHAR_ARCHITECTURE_0.1.13.md) records Dark Moon
Native at 29.121%, with CPU vertex processing consuming 170.551 of 213.906 seconds.
Combo records 13.092%, with CPU vertex work down to 40.599 of 342.896 seconds.
The [0.1.14 analysis](UBERHAR_LOG_ANALYSIS_0.1.14.md) records windows around 9–12%
after optional pipeline creation ends. Compilation alone cannot explain sustained
slow execution. System available-memory declines are a diagnostic lead, not proof
of process/GPU leakage or an OOM kill. These are earlier recorded analyses, not
new measurements recomputed from raw exports in this review.

## Architectural limits and alternatives

<!-- CodexAstraUlt-2: Keep the first-playthrough goal separate from current partial coverage. -->

Native executes vertices through the CPU JIT and uses generic fragment families.
Combo promotes eligible complete lists only after GPU shaders/pipelines are ready,
and may use GLSL-specialized fragments. Ready means available and keyed correctly;
it does not prove device output equivalence or better performance for that draw.
The compute route is a solid-rectangle subset, not general textured/depth-tested
rasterization. Missing generic families/pipelines are still created on demand and
waited on; CPU JIT first encounters can also compile.

| Option | Decision and gate |
| --- | --- |
| Reject the reproduced divergent input layout | Implement 0.1.18, preserving the complete CPU draw; measure incidence using bounded logs. |
| Catch worker exceptions only | Insufficient: completion, submission cancellation, presentation waits and no-throw teardown also require validation. |
| Make every draw wait synchronously to simplify errors | Rejected as an unnecessary foreground cost; retain normal asynchronous scheduling. |
| Move all Android initialization to a thread | Defer broad frontend lifecycle changes; preserve logs by rename/marker staging and copy on the existing worker. |
| Raise shader/pipeline cache limits or add eviction | Defer until memory attribution and in-flight ownership are measured; current count caps do not measure driver allocation size. |
| Separate GPU vertex and fragment routes for diagnosis | Useful next controlled experiment after 0.1.18 evidence; do not change several rendering variables silently. |
| Startup-ready generic coverage / GPU interpreter | Required research toward zero first-use waits, after correctness; account for driver pipeline creation and unsupported state. |
| General compute rasterization / wider topology coverage | Deferred; exact output, depth, blending, order, winding and continuation remain prerequisites. |

## Goals, next order and cadence

<!-- CodexAstraUlt-2: Acceptance requires device evidence rather than host-test counts. -->

1. Ship the focused 0.1.18 fixes only through existing host/shader/Android/signing
   gates. Test injected failures without silent draw skipping or teardown hangs.
2. Obtain a short matched Dark Moon scene comparison and any existing crash evidence.
   Establish whether the input guard fires, whether images improve, and whether an
   actual process crash becomes an explicit rendering stop or remains unexplained.
3. Preserve previously working games. Compare normal speed separately from turbo,
   with matched resolution, scene and cache context; quantify CPU/GPU costs before
   selecting broader optimizations.
4. After image correctness, measure zero omitted draws and zero gameplay compilation
   waits against cold/new-effect coverage. Host rendering tests do not satisfy this
   device acceptance gate. Keep earlier display/model goals and per-game settings
   by 2.0 in the roadmap.

This full source/evidence/alternatives review advances the anchor to **0.1.17**.
Count 0.1.18 as successor one only after publication. Default next review follows
0.1.21 evidence, allowed after 0.1.20–0.1.22, before starting 0.1.23. Review earlier
if Dark Moon remains corrupted, recovery regresses, or evidence invalidates the
architecture. A failed same-version build retry does not advance the count.

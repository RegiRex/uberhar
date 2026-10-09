# Architecture review ledger

<!-- CodexAstraLocal: Count the delivered .33 successor while retaining the explicit .34 review boundary and the failed scratch attempt. -->
**Current batch status:** .33 passed all release gates, was installed with data
preserved and completed one strict diagnostic plus one Native and one full Combo
opening. It is the first released successor to the .32 review anchor. Strict
Calculated performs zero computed draws and no graphics fallback; its independent
scratch request failed preparation before work. The .34 input-recipe candidate
now includes the narrow compiler-preamble correction and a production-compiler
regression. Its release, device tests and both final review tracks remain pending.
The earlier paused snapshots below are history. Stop after .34 tests and reviews.

<!-- CodexAstraLocal: The October 8 owner instruction advances a bounded two-build batch and explicitly requires a review sooner than the standing three-build cadence. -->
**Current owner scope:** deliver and test .33 and .34, then complete another
results/architecture and separate cleanliness audit and stop for owner inspection.
Calculated's .33 test route must have Native/graphics fallback disabled, with
unsupported work explicit and incomplete output excluded from performance claims.
The .32 review remains the completed anchor, zero delivered successors at batch
start. The prior pause below is historical and was explicitly lifted.

<!-- CodexAstraLocal: The owner's latest instruction fixes the next review and stopping boundary without prematurely counting unfinished builds. -->
The 0.1.31 adaptive CPU fragment-cache candidate and 0.1.32 Calculated extension
are delivered and tested. The systematic architecture/correctness/performance
review and separate code-cleanliness/purpose-comment audit are complete.
**Development is paused for owner inspection**, as requested.
Gather each method's evidence in **one run per method per build**; do not repeat
ordinary openings to fill every observation gap. This replaces the earlier
open-ended continuation toward 0.2.0 and aligns with three successors to .29.

<!-- CodexAstraLocal: Reset cadence only after all three delivered successors, four .32 controls and independent full-review tracks have closed. -->
## Current review anchor — released 0.1.32

The [architecture review](UBERHAR_ARCHITECTURE_0.1.32.md),
[separate cleanliness audit](UBERHAR_CLEANLINESS_0.1.32.md) and
[four-method Thor review](UBERHAR_TEST_REVIEW_0.1.32.md) are complete against
**0.1.32, `6486198d52df144e184165133c716ffa01a05f26`**. This closes the three
successful successors to .29: .30, .31 and .32. The new anchor has **zero
successful release successors**. Documentation publication and six local
comment-only lines do not count as another build; those comments are separate
from the tested APK and retained for the next authorized source build.

No new blocker was identified within the reviewed delivered-change scope.
Inherited correctness/ownership debts and observer limits remain in the review.
Mean normal speed remains 19–23%, and Calculated performs zero compute draws.
The existing required fallback preserves complete draws; neither independent
calculation, sustained 99%, 4x, long-game memory nor the retained FEA beta baseline
is qualified. Moon events remain below the owner's escalation threshold;
separate ghost events and missing scene coverage remain explicit.

Next work is proposed in the architecture review but does not begin before owner
inspection. Once resumed, audit after three successful successors or earlier
when contradictory evidence requires it. No .33 build is scheduled.

<details><summary>Completed .29 anchor and its three successors, retained as history</summary>

<!-- CodexAstraLocal: Reset cadence only after the three delivered builds, separate source/cleanliness tracks and required cold controls are integrated. -->
## Previous review anchor — released 0.1.29

The [full 0.1.27–0.1.29 review](UBERHAR_ARCHITECTURE_0.1.29.md) is complete
against released **0.1.29, `0136f89d429eab2b25f626703b2158b9ebe5091d`**.
It integrates architecture/correctness, a separate 62-file post-0.1.22
cleanliness/purpose audit, exact release verification, both cold Native/Combo
controls and independent consecutive-frame evidence. The final comment-only
patch is integrated and matches reviewed token/AST proofs. No new blocking
correctness defect was found in the delivered changes.

There are **three successful release successors** to this anchor: 0.1.30 passed all
shader/Android/signing/publication gates, exact-asset verification and compatible
installation, followed by gated 0.1.31 and 0.1.32 delivery and compatible installation.
The .30 and all four .31 controls completed normal return and bounded temporal
review. All four .32 controls have now returned normally; the mandatory full
review and final temporal synthesis are closing. Unobserved scenes remain
unqualified; the anchor is not reset until both reviews are complete.
Audit again after three successful successors or earlier
when evidence contradicts the architecture. Native/Combo bounded means remain
27.869%/21.892%, far below sustained 99%. Moon and ghost coverage remains limited,
with the brief Native rear-ghost disturbance still tracked. Audit closure does
not qualify 0.2.0, 4x, longer gameplay or the retained FEA baseline.

The integrated 0.1.30 CPU-ready-fragment candidate passed actual host ABI/pixel,
selection, state, failure and lifetime proofs, native integration and the full
host probe. Delivery and bounded device controls are complete. Native's fresh
process repeat did not recover the earlier speed; cause remains unassigned.
Routine work continues to the explicit .32 review/stop boundary above.

<!-- CodexAstraLocal: Focused candidate reviews and host checks do not increment delivered-build cadence or reset the full-audit anchor. -->
The 0.1.31 adaptive ownership/census release passes independent correctness,
separate cleanliness and merged-hook review, fresh native integration/CTest and
the full host probe. All 22 mandatory release steps passed in run 37738256467,
followed by exact-source/APK/signing-identity verification and compatible install.
Its one-run-per-method ordinary device evidence is complete. The .29 full-audit anchor
and mandatory .32 systematic review remain unchanged.

</details>

<details>
<summary>Completed 0.1.26 anchor and its three successors, retained as history</summary>

<!-- CodexAstraLocal: Close the three completed successors with an integrated source/evidence audit while keeping product qualification separate. -->
## Previous review anchor — released 0.1.26

The [full 0.1.24–0.1.26 architecture review](UBERHAR_ARCHITECTURE_0.1.26.md)
is complete against released **0.1.26, `e83e8f4f1`**, after the three completed
successors to the 0.1.23 audit: 0.1.24, 0.1.25 and 0.1.26. It integrates
independent renderer/contracts, diagnostics/Android/delivery, performance and
temporal-evidence reviews, including actual .26 release gates and two cold Thor
runs. No new release-blocking defect was found; recorded diagnostic and inherited
ownership/failure debts remain explicit in the audit.

<!-- CodexAstraLocal: Count the actual successful .27 release as one successor; focused delivery review does not reset the full audit anchor. -->
The new source-audit anchor is **0.1.26**. Gated **0.1.27 is successor one**.
<!-- CodexAstraLocal: Count verified gated delivery without treating focused review or incomplete device acceptance as a full architecture audit. -->
Gated **0.1.28 is successor two**: all shader, Android, signing and publication
checks passed, published assets were verified, and the compatible data-preserving
upgrade installed. Cold Native and Combo controls completed at 28.424% and
21.799% mean normal speed; unlike scene mixtures cannot prove a speedup. The
owner-requested calculated baseline completed at 27.337%, with zero computed draws
and complete Native fallback. The source-audit anchor remains
0.1.26; focused control/comment reviews do not reset it.
Gated **0.1.29 is successor three**, with its verified compatible upgrade
installed. Separate full architecture/correctness and code-cleanliness audits are
underway alongside cold device controls; the .26 anchor is not yet reset.
<!-- CodexAstraLocal: Record focused .29 review and local gates without prematurely advancing the successful-build audit count. -->
The .29 per-draw shader-call candidate passes independent source review, real
production regression, native integration/CTest and the complete host probe.
Release gates and Native/Combo cold device tests remain separate; the full
three-successor audit stays due after successful .29 delivery.
Continue the owner's audit every three completed successor builds,
or sooner when evidence contradicts an architectural assumption or two candidates
produce neither improvement nor useful discrimination. Existing-build tests,
documentation checkpoints and retries do not increment the build count. The old
four-hour boundary and three-to-five-successor cadence are superseded.

<!-- CodexAstraLocal: Record the repeated bounded visual result and failed throughput gate without claiming a full-game cure, matched speedup or lifetime bound. -->
Both .26 full-Combo/Vulkan/2x openings verified File 1 Empty and zero saved
application shader-cache inventory. Consecutive review of the fully visible moon
interval and three matched ghost intervals in each run found none of the prior
flashing or dense irregular patches. These are separately scored, bounded
observations. The selected complete timing windows average **23.227% and 22.339%
of normal speed**; their differing scene mixtures do not establish a speedup.
Same-process KGSL returns to 13.32 MiB after each verified normal exit, which does
not bound long-game memory. Full correctness, sustained 99%+ speed, 4x and broader
gameplay remain unqualified. The retained
[0.2 beta evidence gate](UBERHAR_ARCHITECTURE_0.1.6.md#roadmap-and-version-gates),
including the 2x FEA baseline, exact output and profiling-supported architecture,
is not waived by audit completion.

<!-- CodexAstraLocal: Advance from completed finite installed-build observations to a narrowly scoped measurement candidate after both external profiling routes proved unusable. -->
The finite installed-.26 thread-counter experiment completed on another cold
opening. Its 34 complete timing windows average 22.863%; main-thread CPU deltas
of 9.80 and 6.43 seconds were observed over approximately ten-second intervals.
Endpoint-query skew and uncertain scene boundaries prevent precise rates or
function attribution. A one-second idle scheduler-trace check then hit a fatal
vendor-event parser incompatibility in the stock producer and yielded no events;
the producer restarted. Do not retry unchanged ftrace initialization.

<!-- CodexAstraLocal: Close delivery only after exact-run gates, APK verification and installed-version evidence; device controls remain separate. -->
**0.1.27 passed implementation, focused review, local validation and every
release gate** in
[run 37668250513](https://github.com/RegiRex/uberhar/actions/runs/37668250513)
at `76b307a917cf2098c636492648a601e71079a2a1`. APK checksum/provenance and
compatible signing identity were verified; cryptographic signing passed in CI.
The data-preserving update installed version code **33979207**. It adds default-off
bounded timing of complete contiguous CPU vertex chunks, with boundary-only and
detailed controls, exact coverage and explicit observer limits. It is a
discriminator, not a renderer optimization or achieved speedup. This completed
release increments the successor count without resetting the .26 audit anchor.
<!-- CodexAstraLocal: Integrate completed finite controls without converting tiny perturbed intervals or unlike populations into a bottleneck claim. -->
The request-free, boundary and detailed cold controls have completed normal
return. Their conservative ordinary opening windows average **22.568%, 23.121%
and 23.172%** respectively; unlike scene mixtures prevent a speedup or observer
ratio claim. The request-free recorded moon and three bounded ghost sequences
show none of the prior faults, with broader correctness still open. Both finite
reports pass all 64 chunks, but detailed clock cost is material and selected
populations are mostly different. Focused independent review and regression now
support the Android overlay lifecycle repair and grouped exact CPU output copies
selected for .28. Neither changes the audit anchor or establishes a device gain.
The owner's updated memory priority is continuing gameplay growth/pressure;
repeated post-exit sweeps are optional absent ownership changes or a regression.
The `simpleperf stat` attempt was denied; root restored its internally changed
`security.perf_harden` property from 0 to the original 1 and verified the result
at 08:26:47 UTC. Do not repeat that tool or weaken platform controls.

</details>

<!-- CodexAstraLocal: Preserve every prior checkpoint and author attribution as historical state rather than current instructions or an active audit anchor. -->
<details>
<summary>Historical review and release checkpoints before the completed 0.1.26 audit</summary>

The entries below preserve their status at the time, including pending gates,
earlier anchors and superseded review intervals. The current anchor and next
action are above.

<!-- CodexAstraLocal: Complete the required three-successor audit before the next candidate; this source/evidence review resets cadence but does not qualify the device. -->
The [October 7 recovery architecture audit](UBERHAR_AUDIT_2026-10-07.md) is
complete against released 0.1.23 (`99fe4a885`) and guidance `814de4d60`, including
the recovered candidate, post-0.1.22 change inventory, renderer/output contracts,
ownership, lifecycle/logging, release gates and the retained 0.2 throughput gate.
Independent output-guard review found no blocking correctness issue; candidate
build/device results remain separate. The new anchor is **0.1.23 audited source**.
Version 0.1.24 passed all gates in
[run 37566500834](https://github.com/RegiRex/uberhar/actions/runs/37566500834)
at `970805ddc` and is successor one. Count 0.1.25 as two and 0.1.26 as three
if numbering continues. Audit again after those three,
before starting 0.1.27, or earlier for failed architectural assumptions/two
uninformative candidates. The old three-to-five range below is historical.

<!-- CodexAstraLocal: Focused capture ownership/bounds/replay review does not reset the full architecture-review anchor. -->
0.1.25 source implements the requested default-off bounded vertex evidence and
private replay after 0.1.24 retained both faults. Three parallel roles reviewed
snapshot/lifetime/submission, actual producer serialization and CPU interpreter/Mesa replay.
Focused tests, complete probes, native rebuild and CTest passed. Release
[run 37572128020](https://github.com/RegiRex/uberhar/actions/runs/37572128020)
passed all gates at `9bf0cab4e`; a compatible data-preserving install and first
bounded Android discovery succeeded. Later cold openings collected actual
moon/ghost-window payloads and executed production CPU interpreter/Mesa replay. Independent
review places all eight fourth-run packets wholly beyond the right clip plane
under both host routes, so the selected family supplies no visible ghost
attribution. Fifth-run actual depth packets share the earlier color mesh but
have different cross-run position uniforms; they cannot test same-frame
depth/color equality. Sixth-run selected geometry survives host clipping but
still lacks actor attribution. Both later coordinated moon clips flash, while
their independent ghost clips retain clear/patch/clear corruption. The sixth
initial return attempt left the exit dialog open; only its later verified
return/final log supplies cleanup evidence. These results do not establish
Adreno parity or a fault's cause. Host CPU replay executes the production
interpreter, not the device's enabled CPU JIT. Graphics and sustained-speed
qualification remain incomplete. Same-APK selector experiments are not new builds. This is
completed build successor two, not a new full architecture audit.

<!-- CodexAstraLocal: Keep the completed eight-opening diagnostic series and synthetic arithmetic review separate from release qualification and the full-audit count. -->
Seventh/eighth 0.1.25 openings retained eight completed packets each. The selected
ghost-window geometry is partly clipped and unassigned; the final depth instance
is wholly clipped in both interpreter/Mesa host routes. Temporal ghost defects
and a moon-segment reversal persist independently. Verified normal returns
recover sampled KGSL; the eighth exit dialog required a retry. These are same-APK
diagnostic runs, not new builds or sustained-speed qualification.

Independent review of a separate synthetic x64 JIT/generated-GPU DP4 experiment
confirms mixed-route LEQUAL failures in both route orders and repair by precise
pairwise controls. OpenGL SPIR-V was executed in both optimizer modes; Vulkan
modules were only validated. AArch64/Adreno execution, full arithmetic edge
coverage and title causality remain unproven. This supports scoped 0.1.26
implementation work, whose production regression/review and release/device
gates remain pending. Count 0.1.26 as successor three only after its completed
build; the audit anchor stays 0.1.23, with a full audit required before 0.1.27.


<!-- CodexAstraUlt: The owner's new fixed three-build audit cadence supersedes the older three-to-five range without treating a documentation update as a completed audit. -->
**Owner update: audit every three builds.** The 0.1.20 anchor has three completed
successors (0.1.21, 0.1.22, 0.1.23); a full audit is in progress before the next
candidate publication. Review source/contracts, device temporal evidence, resource
ownership, performance alternatives and the documented 0.2 beta gate. Record the
completed audit separately before resetting the count.

<!-- CodexAstraUlt: Count successful 0.1.23 publication and retain the same source-review anchor during local takeover. -->
0.1.23 [run 37551055615](https://github.com/RegiRex/uberhar/actions/runs/37551055615)
passed shaders, Android/package/signing and publication. It is successor three
after the completed 0.1.20 source review. The new
[local report](device_reports/UBERHAR_DEVICE_TEST_2026-10-06.md) records persistent
visual and throughput limits; it does not reset the review count. The
[local coordinator](UBERHAR_LOCAL_AUTONOMOUS_HANDOFF.md) may review now, normally
reviews after 0.1.24, and must review before a sixth successor (0.1.26 if consecutive).
Earlier pending-publication statements below are historical.

<!-- CodexAstraUlt: Reconcile successful comparison builds with the owner's resumed prerelease route without erasing the completed source review or claiming a new one. -->
<!-- CodexAstraUlt: Replace the conditional third-successor count because 0.1.23's complete release gates now pass; record publication and compatible installation while leaving device acceptance and the 0.1.20 source-review anchor separate. -->
**Current anchor remains the completed 0.1.20 source review.** The successful
0.1.21, 0.1.22 and 0.1.23 builds are successors one, two and three. 0.1.22 passed all shader,
Android and signing/package gates in
[run 37524017429](https://github.com/RegiRex/uberhar/actions/runs/37524017429).
0.1.23 passed shader, Android, signing/package and publication gates in
[run 37551055615](https://github.com/RegiRex/uberhar/actions/runs/37551055615),
was [published as a prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.23)
from `99fe4a8851ff696219dddda58fb154fbf8ce1149`, and is installed on the Thor
as a signing-compatible update preserving app data. The APK checksum matches
the published checksum and asset digest; its certificate matches the installed
0.1.22 certificate and repository pin.
The owner now authorizes normal repository prereleases; changing publication route
does not reset that review count or remove earlier evidence.

The [0.1.22 device log](UBERHAR_LOG_ANALYSIS_0.1.22.md) now places the much larger
live Combo footprint in own-process KGSL accounting, with recovery after normal
exit. It does not demonstrate a persistent post-exit leak. The owner reports
continued ghost and moon glitches, more visible glitches overall, and a
significant perceived speed improvement; these are not matched throughput data.
The 0.1.23 focused follow-up is implemented to optimize
optional background GPU shader compilation, preserving the cold generic and
Custom policies. Source review, local validation and CI passed; device acceptance
remains a separate gate. No memory cure or controlled speedup has been established. The fitted
96/72 MiB increments are associated with GPU pipeline counts grouped by attachment
format, not image size calculations or direct driver allocation-type observations.

<!-- CodexAstraUlt: Record the owner-directed local protocol without turning a Native visual reference or preliminary cleanup observation into a performance or image qualification. -->
<!-- CodexAstraUlt: Replace pending Combo/Native assessment with the completed bounded captures and recovery evidence; unmatched poses and slow playback still prevent image or performance qualification. -->
The [local Thor checkpoint](UBERHAR_DEVICE_TEST_2026-10-06.md) records a 0.1.22
opening through the initial moon and laboratory ghosts, followed by normal exit:
its sampled KGSL peak was 3,824 MiB and returned to 12 MiB. This is separate
from the earlier supplied two-run log. The 0.1.23 full Combo and Native visual
reference openings are now complete, each with a cold application Vulkan cache,
manual in-game File 1 reset, normal return and no recorded skips or failures.
Combo KGSL was 11.06 MiB before the run, peaked at 3,051.37 MiB in the exit
drawer, and returned to 11.57 MiB in external samples. Native mode 1 recorded
zero GPU batches and 43.37 → 1,076.11 → 16.97 MiB in its before-native-run,
sampled-peak and after-native-run records. About six minutes later, an external
sample showed 11.43 MiB KGSL and 11,889.11 MiB system available memory.

Scene-only Combo playback averaged 13.231 game FPS / 22.165% speed; Native
averaged 15.317 / 25.649%. Native is a visual reference, not a playable standard,
and these captures do not establish a matched speedup. Moon facets appear in
both modes. One Combo book-ghost frame has dense irregular near-black patches
not seen in inspected Native frames, but their poses differ: image correctness
remains unqualified and no graphical cause or cure is established. The short
runs show recovery, not bounded full-gameplay memory. Cross-build state loading
was rejected; the owner has discontinued save-state creation/loading for this
comparison. At 21:02:42 EDT the operator restored full Combo, 2×, 100% and the
game list. No emulator source changes, new build or public push were made;
the cloud thread was not visible, so the prepared handoff remains pending.

0.1.23 is now successful successor three. Repeat the full review
after three to five successful successors, normally four (0.1.24 if versions
advance consecutively), or earlier if new evidence invalidates the design.
This focused follow-up does not claim that a full review has been completed.
Use the owner-confirmed `uberhar/hybrid-shaders` release workflow in
`RegiRex/uberhar` only. Literal integration into divergent upstream `master` is
unnecessary for repository prereleases and deferred for separate review.
Previous entries below preserve the publication and evidence state at the time.

<!-- CodexAstraUlt: Record the first successful successor and the new focused correction without falsely resetting the completed full review. -->
0.1.21 comparison CI passed all Android, signing/package and shader gates in
[run 37436727334](https://github.com/RegiRex/uberhar/actions/runs/37436727334),
so it is successful comparison successor one after the 0.1.20 source review.
Its [device evidence](UBERHAR_LOG_ANALYSIS_0.1.21.md) still shows ghost corruption
and system memory pressure outside tracked live allocations. 0.1.22 addresses
the reproduced quaternion-correction admission gap and extends bounded driver
accounting. This focused follow-up does not reset the full review anchor or qualify
device correctness/performance. Broader optimization remains behind those gates.

<!-- CodexAstraUlt: Owner-requested full review resets the comparison lineage's architecture anchor, not another session's shared-release history. -->
The [full 0.1.20 architecture review](UBERHAR_ARCHITECTURE_0.1.20.md), completed
October 6, 2026 against `c75e544f7`, covers current source versus base Azahar,
the supplied device evidence, resource ownership, correctness contracts, active
overhead, alternatives and measurable gates. Its comparison-lineage anchor is
**0.1.20 source**; unpublished candidates and failed retries are not published
alpha successors. Review this lineage again after three to five successful
successor candidates, normally four, or immediately if route/memory evidence
contradicts the plan. Reconcile this source anchor with shared-release numbering
when integrating; the other session's release ledger is not silently rewritten.

0.1.21 selects proven pool/stream ownership repairs, bounded process/Vulkan
measurements and an explicit generic-fragment Combo control. Independent fragment
promotion, broader eligibility and speculative eviction remain gated by image
correctness and bounded working-set evidence. This records a completed source
review, not completed device qualification or APK delivery.

<!-- CodexAstraUlt: New device evidence triggers focused early diagnosis without claiming a completed full architecture review. -->
The [0.1.17/0.1.18 attachment review](UBERHAR_LOG_ANALYSIS_0.1.17_0.1.18.md) confirms
Android-classified low-memory exits and sustained Sonic/Dark Moon limitations.
0.1.20 follows the isolated 0.1.19 comparison with three reproduced input-parity
guards and bounded applet warnings. This is a focused audit, not a reset of the
0.1.17 anchor. Continued Dark Moon corruption calls for early route isolation and
memory attribution before broader performance qualification. Comparison candidates
are not shared published alphas; retain the established review window.

<!-- CodexAstraUlt: Logging-delivery follow-up does not reset the completed 0.1.17 architecture anchor. -->
0.1.18's gated workflow completed successfully. The 0.1.19 follow-up adds explicitly
optional progress-record delivery and omission accounting, while retaining its
renderer/recovery fixes. This is a focused reliability change, not a full review
or new device evidence. Keep the 0.1.17 review anchor and require earlier review if
device results invalidate its assumptions; no Dark Moon correctness gate is waived.

<!-- CodexAstraUlt-2: Completed 0.1.17 source/evidence/alternatives review and accepted 0.1.18 order. -->
The [0.1.17 full review](UBERHAR_ARCHITECTURE_0.1.17.md) was completed October 5,
2026 against `fe7d1b8a0`, using the owner's current Thor symptom report and the
committed device analyses, with fresh host reproductions and no new device claims.
The owner approved 0.1.18 correctness/recovery work. The anchor is **0.1.17**;
0.1.18 counts as successor one only after its publication gates pass. Default next
review: after 0.1.21 evidence; allowed after 0.1.20–0.1.22, before 0.1.23. Continued
Dark Moon faults or failed recovery require earlier review. Previous entries retain
their original anchors and evidence.

<!-- AstraEH: Optional file-only crash evidence follows the owner's explicit clarification. -->
0.1.16 passed all Android, shader and publication gates and is successor three after
0.1.13. 0.1.17 adds an optional bounded Android crash-evidence layer, with reports
available only as files. Count it as successor four only after publication. The default
full review is after 0.1.17 evidence; allowed through 0.1.18 and required before 0.1.19.
This focused reliability change does not reset the anchor. No new device runs or
renderer improvements are claimed; the owner's pending tests remain the next evidence.


<!-- AstraEH: Lean logging is a focused owner-directed follow-up, not a full review reset. -->
0.1.15 has passed Android, shader and publication gates and is successor two after
0.1.13. The owner requires GammaOS-compatible, lean logging without per-launch bundles
or dependence on optional crash services. 0.1.16 implements that simplification and
counts as successor three only after publication. Default full review remains after
0.1.17 evidence, allowed after 0.1.16–0.1.18 and before 0.1.19. No new renderer/device
results are claimed. The prior status snapshots below remain historical.


<!-- AstraEH: Current follow-up status; historical review entries below remain intact. -->
The [0.1.14 device review](UBERHAR_LOG_ANALYSIS_0.1.14.md) identifies an Android
private-log path defect and a rejected per-launch bundle UI. 0.1.15 fixes those
issues without renderer changes. This focused review does not reset the full
0.1.13 anchor: 0.1.14 is published successor one; 0.1.15 counts as two when its
prerelease gates pass. Next default full review remains after 0.1.17 evidence,
allowed after 0.1.16–0.1.18 and before 0.1.19. Dark Moon's large system-memory
decline strengthens the need for focused crash/memory attribution, not an assumed
OOM diagnosis or premature expansion of renderer coverage.

<!-- AstraEH: Build-count workflow requested by the owner, not a timed reminder. -->

Review every **three to five alpha builds**, normally every four. Count published
alpha versions since the version covered by the last review; failed build retries
of the same version do not advance the count. The next review can happen after
the fifth build's evidence arrives, but must finish before beginning a sixth.
Review early for correctness regressions or a changed architectural assumption.

| Review date | Latest version examined | Default next review | Allowed window |
| --- | --- | --- | --- |
| 2026-09-24 | 0.0.6 | After 0.0.10 | After 0.0.9 through 0.0.11; before 0.0.12 work |
| 2026-09-24 | 0.0.9 | After 0.0.13 | After 0.0.12 through 0.0.14; before 0.0.15 work |
| 2026-09-25 | 0.0.12 | After 0.0.16 | After 0.0.15 through 0.0.17; before 0.0.18 work |
| 2026-09-27 | 0.0.15 | After 0.1.3 | After 0.1.2 through 0.1.4; before 0.1.5 work |
| 2026-09-28 | 0.1.3 | After 0.1.7 | After 0.1.6 through 0.1.8; before 0.1.9 work |
| 2026-09-29 | 0.1.6 | After four successor builds | After three through five; before beginning six |
| 2026-10-01 | 0.1.9 | After 0.1.13 | After 0.1.12–0.1.14; before 0.1.15 work |
| 2026-10-03 | 0.1.13 | After 0.1.17 | After 0.1.16–0.1.18; before 0.1.19 work |
| 2026-10-05 | 0.1.17 | After 0.1.21 | After 0.1.20–0.1.22; before 0.1.23 work |

The [0.0.6 review](UBERHAR_ARCHITECTURE_2026-09-24.md) established the earlier
roadmap. 0.0.7 implemented family consolidation; 0.0.8 added broader runtime state,
ready CPU vertex routing and host-pipeline reuse; 0.0.9 corrected bridge coverage.

The [0.0.9 review](UBERHAR_ARCHITECTURE_0.0.9.md) rechecks all four 0.0.9 sessions
and starts explicit primary-generic/native and compute-subset experiments in
0.0.10. The owner's request to compare virtual-PICA designs and essentially
unchanged cold waiting justify reviewing after three builds. This ledger does
not claim unattended development or timed reviews.

For each review, record exact revisions, device evidence, unanswered questions,
accepted/rejected alternatives, validation gates, implementation order and the
new review window. Keep previous entries for comparison.

<!-- AstraEH: The third post-review alpha supplied a successful device baseline. -->
The [0.0.12 review](UBERHAR_ARCHITECTURE_0.0.12.md) rechecks source and the Thor
cold/warm milestone, compares architectural alternatives, and selects runtime
lighting controls for 0.0.13. The subsequent [0.0.13 device analysis](UBERHAR_LOG_ANALYSIS_0.0.13.md)
confirms reduced family counts and retained warm speed; that results analysis is
not another full architectural review and does not reset this ledger.


<!-- AstraEH: Results analysis and beta planning do not restart the review interval. -->
The [0.0.14 analysis](UBERHAR_LOG_ANALYSIS_0.0.14.md) finds smaller family counts but
higher cold driver cost, motivating compact runtime lighting in 0.0.15. This is a
results analysis, not another full review. The default review remains after 0.0.16;
a proposed 0.1.0 promotion should include the final-alpha architectural assessment
within the existing three-to-five-alpha window.


<!-- AstraEH: Beta promotion follows the third post-review alpha and does not hide a build. -->
The [beta-promotion review](UBERHAR_ARCHITECTURE_0.1.0.md) audits 0.0.15 source and
all four Thor sessions before promoting Native to beta. It selects final-vertex
reuse, prepared register transport and sparse CPU-stage samples while preserving
the compact fragment path. Count 0.1.0 as successor build one, 0.1.1 as two,
0.1.2 as three, 0.1.3 as four and 0.1.4 as five. Thus the default next review is
after 0.1.3, allowed after 0.1.2–0.1.4, before starting 0.1.5. The earlier
0.0.16 default above is historical and superseded by this completed review.


<!-- AstraEH: Broader beta evidence and a diagnostics follow-up do not reset the cadence. -->
The [0.1.0 device analysis](UBERHAR_LOG_ANALYSIS_0.1.0.md) adds Ocarina and Sonic,
confirms reduced vertex work per input, and identifies draw-heavy steady-state
costs plus sample exhaustion. 0.1.1 is a focused diagnostics iteration. This is
not another full architecture review: the next default remains after 0.1.3,
allowed after 0.1.2–0.1.4, before 0.1.5. The owner's requested automatic
compatibility profiles are recorded as post-1.0 scope.

<!-- AstraEH: Complete the scheduled four-successor review; this alone resets the interval. -->
The [0.1.3 full review](UBERHAR_ARCHITECTURE_0.1.3.md) rechecks the five latest
runs, prior beta evidence, current source, alternatives and acceptance gates.
It retains Native and selects settings/cache context for 0.1.4, then measured
per-draw bookkeeping reduction. Count 0.1.4 as one, 0.1.5 as two, 0.1.6 as three,
0.1.7 as four and 0.1.8 as five. Next default after 0.1.7 evidence, allowed after
0.1.6–0.1.8 and required before 0.1.9. Earlier windows remain historical.

<!-- AstraEH: The requested early full review is within the three-to-five build window. -->
The [0.1.6 full review](UBERHAR_ARCHITECTURE_0.1.6.md) examines twelve deduplicated
Thor sessions, the exact released source, zero compute coverage and MH4U's
specialized recovery. It selects fused vertex input plus support/recovery
instrumentation for 0.1.7 and revises the owner roadmap without dropping its
scope ledger or prior history. This is a completed full review, not only a log
analysis. Count 0.1.7 as successor one; default next review after four successors,
allowed after three to five, before starting six. If the beta number stays 0.1,
that is after 0.1.10, allowed after 0.1.9-0.1.11, before 0.1.12. A minor promotion
retains the successor count rather than resetting it.


<!-- AstraPro: Expanded evidence and a focused ready-GPU design do not erase the previous review ledger. -->
The [0.1.7 evidence/0.1.8 decision](UBERHAR_LOG_ANALYSIS_0.1.7.md) reviews seven
sessions, host-mode/thermal uncertainty, index-range rejection and sustained CPU
vertex work. It adds index rescue and an opt-in bounded ready-GPU Combo route.
This is a focused extension, not a claimed reset of the full-review cadence.
0.1.7 is successor one and 0.1.8 successor two after the full 0.1.6 review.
Default review remains after 0.1.10 evidence; allowed after 0.1.9–0.1.11, before
0.1.12 if the beta number remains 0.1. Review earlier if GPU visual correctness
regresses or measured route coverage invalidates the selected architecture.


<!-- AstraPro: Eight 0.1.8 sessions identify a narrow admission blocker, not a new architecture. -->
The [0.1.8 device analysis](UBERHAR_LOG_ANALYSIS_0.1.8.md) validates index rescue
and finds all Dark Moon Combo draws excluded by Shader topology. 0.1.9 admits
independent no-GS Shader lists with winding/assembly checks and retains Native
as the control. This focused admission audit does not reset the full-review count.
0.1.9 is successor three; default full review after 0.1.10 evidence, allowed after
0.1.9–0.1.11, before 0.1.12. GPU visual regressions or high promotion without
benefit should trigger earlier architectural reassessment.


<!-- AstraPro: Early full review after three published successors; candidate is not publication. -->
The [full 0.1.9 review](UBERHAR_ARCHITECTURE_0.1.9.md), completed locally on
October 1, 2026, rechecks fourteen deduplicated sessions, changed resolutions,
CPU/GPU routing, shader/pipeline costs, alternatives and correctness/lifetime
constraints. High Dark Moon promotion without benefit justifies the early review.
It selects bounded ready fragment specialization and measurement improvements,
not broader unsafe topology admission. The public ledger is unchanged until the
patch is applied; this local review anchor is 0.1.9. 0.1.10 is NOT counted until
published. With unchanged beta numbers, default review after 0.1.13 evidence,
allowed after 0.1.12–0.1.14, before 0.1.15. Failed same-version retries do not
advance the count. Review sooner for correctness regressions or another failed
performance hypothesis.


<!-- AstraPro: 0.1.10 visual/crash report blocks further performance qualification. -->
0.1.10 passed its publication workflow, but the owner reports Dark Moon Combo
visual corruption and a crash with lost logs. 0.1.11 is an evidence-retention
update only: no renderer correction, full-review reset or device parity claim.
The full-review anchor remains 0.1.9 (0.1.10 successor one, 0.1.11 successor two
once published). The regression requires early source/device review before any
further expansion of the experimental GPU path; obtaining a preserved short
reproduction takes priority over another throughput milestone or suite replay.

<!-- AstraEH: Owner acceptance completes the source, evidence and two-audit review. -->
The [full 0.1.13 review](UBERHAR_ARCHITECTURE_0.1.13.md) rechecks the released
source, eleven complete/partial runs, two external audits and the owner’s scope.
The owner accepted the priority order and resumed builds on October 3, 2026.
0.1.14 introduces independent session retention and narrow correctness changes;
it does not claim a Dark Moon image or performance cure. Count it as successor
one only after publication. Default next review follows 0.1.17 evidence, allowed
after 0.1.16–0.1.18 and required before 0.1.19. Broader performance experiments
remain deferred until at least Dark Moon image correctness is device-validated.
Earlier entries below retain their historical review anchors.


<!-- AstraPro: Matched evidence identifies a narrow retry defect and a working control. -->
The [0.1.12 correctness review](UBERHAR_CORRECTNESS_0.1.12.md) uses the preserved
0.1.10 Dark Moon pair plus Sonic's positive Combo pair. It corrects lost pending
clip/viewport transport without altering the GPU/fragment policies. This focused
review does not reset the full 0.1.9 anchor. 0.1.11's publication workflow passed;
0.1.12 counts as successor three only after publication. Default full review
remains after 0.1.13 evidence; allowed 0.1.12-0.1.14, before 0.1.15. The original
Dark Moon crash cause and device image parity remain open, regardless of local
host-test success or Sonic throughput.


<!-- AstraPro: New Native requirement and persistent Combo anomalies keep the early review active. -->
The [0.1.12 evidence and 0.1.13 decision](UBERHAR_LOG_ANALYSIS_0.1.12.md) separates
Sonic's owner-marked racing windows, corrects intended versus recorded LEGO modes,
and keeps Native optimization parallel to Combo diagnosis. It fixes a reproduced
fixed-attribute capacity defect and adds exact semantic-plan reuse plus bounded
stage/state diagnostics. Neither patch is declared the Dark Moon crash cure.
This focused review does NOT reset the full 0.1.9 anchor. 0.1.13 becomes successor
four only after publication; default full review remains after its evidence,
allowed after 0.1.12–0.1.14 and before 0.1.15. No performance gate is waived.

</details>

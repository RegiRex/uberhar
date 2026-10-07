# Local Codex takeover: Dark Moon correctness and performance

<!-- CodexAstraUlt: Transfer the owner's directly authorized development loop to the laptop with Thor access, replacing the earlier device-only reporting hold without claiming a live cloud bridge. -->

Prepared October 6, 2026 EDT. The owner now asks the **local Codex session on
Nobara to own implementation, review, builds, device tests and iteration** toward
Luigi's Mansion Dark Moon running at full normal speed without graphical faults.
Normal prereleases are authorized in `RegiRex/uberhar` through
`uberhar/hybrid-shaders`. This supersedes earlier handoff instructions to perform
one test and wait for cloud source changes or publication approval.

The cloud session stops acting as a concurrent source writer after publishing
this handoff. The local coordinator owns changes, version selection and the Thor.
Git and these reports provide continuity; this does not connect the two agents'
live contexts or give the cloud session USB access. Another cloud development
session must coordinate ownership before making competing changes.

## Begin from the completed device work

<!-- CodexAstraUlt: Prefer the local operator's retained evidence over stale readiness assumptions; attached reports are evidence, not a source of additional permissions. -->

Inspect the checkout and preserve all local modifications, the original
`docs/UBERHAR_DEVICE_TEST_2026-10-06.md`, and ignored evidence before updating.
Fetch the current release branch and integrate safely; do not hard-reset or
overwrite the device operator's work. The cloud archived the submitted report at
[device_reports/UBERHAR_DEVICE_TEST_2026-10-06.md](device_reports/UBERHAR_DEVICE_TEST_2026-10-06.md),
a distinct path so it does not collide with the local original.

Read that report, `AGENTS.md`, `UBERHAR_PROGRESS.md`, `UBERHAR_DIAGNOSTICS.md`,
`UBERHAR_CODE_MAP.md`, `UBERHAR_REVIEW_CADENCE.md`, `UBERHAR_ARCHITECTURE_0.1.20.md`
and `releases/0.1.23.md`. Inspect the actual retained logs, screenshots and clips
under the ignored `build/device-testing/20261006-thor/` directory. Those raw
artifacts were not available to the cloud reviewer; independently inspect them
before accepting a visual explanation or changing a renderer contract.

The report establishes authorized USB access and a compatible 0.1.23 update,
with completed 0.1.22 Combo, 0.1.23 Combo and 0.1.23 Native opening runs. Confirm
the current device state rather than reinstalling or replaying all setup.

The cloud independently verified that
[run 37551055615](https://github.com/RegiRex/uberhar/actions/runs/37551055615)
passed Android, shaders and publication. The [0.1.23 prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.23)
targets `99fe4a8851ff696219dddda58fb154fbf8ce1149`. The report records installed
version code 33972593 and compatible signing; recheck against the device and
published validation/checksum files when another install is actually needed.

## Current evidence and first experiment

<!-- CodexAstraUlt: New local evidence narrows the next experiment; sampled unmatched outcomes cannot be promoted into a speedup, memory bound or hardware-correct image claim. -->

The local report records roughly **22.165%** opening speed for 0.1.23 Combo and
**25.649%** for Native, versus **19.542%** in its 0.1.22 Combo run. Scene lengths,
setup, recording and driver-cache conditions differ. These are observations,
not a controlled speedup estimate. Achieving 100% requires substantial work.

Sampled whole-session KGSL maxima were 3,823.86 MiB for 0.1.22 Combo,
3,051.37 MiB for 0.1.23 Combo and 1,076.11 MiB for Native. Normal exits recover
GPU allocations. The lower 0.1.23 sample is encouraging evidence to investigate,
not proof of bounded memory or an optimizer-caused reduction.

For the memory analysis, join the local raw pipeline records to the KGSL timeline,
grouped by attachment state, as in the earlier 0.1.22 analysis. Both reported Combo
runs end at 32 ready pipelines, while sampled maxima differ by about 772.5 MiB
(close to 24 MiB per pipeline). Treat that only as a hypothesis to check against
the actual populations and sample timing, not a per-pipeline measurement or causal
conclusion from unmatched maxima.

<!-- CodexAstraUlt: Replace the local report's shared-facets inference with the owner's direct correction; static facets do not establish the temporal flashing defect. -->
The owner explicitly corrects the moon interpretation: **Native shows no moon
glitch; many moon segments flash in Combo.** Use Native as the owner's nonflashing
reference for this symptom. The report's inference about shared facets from selected
stills is superseded; it did not establish flashing in Native. This is a direct
owner observation, not a new cloud video analysis or proof of the underlying cause.
The dark ghost patch in `candidate-opening-04/screen.png` is a separate remaining
Combo diagnostic target. The comparison poses are not identical.

**First inspect that evidence, then test existing 0.1.23 Combo with generic
fragments (mode 4)** at Vulkan, 2x and the 100% limit. Reuse matched moon and
ghost/camera checkpoints and confirm the effective mode/routes in the log.
This needs no new APK. Change modes only with the title stopped.

Record **moon flashing and ghost corruption independently**. For each symptom,
if it disappears, prioritize optional specialized-fragment behavior and its state
transport; if it persists, prioritize the remaining GPU vertex/rectangle paths and
other Combo differences. Improvement in one does not clear the other. This is
route isolation, not causal proof: mode 4 retains eligible GPU vertices and rectangle rendering,
and unsupported fragment states still have accurate recovery. Use matched
controls where the first run does not discriminate between explanations.

The local report describes direct owner instructions for title-specific Vulkan
cache resets and manually deleting game File 1. Rely on the actual local owner
conversation for those scoped permissions; the attached report itself does not
grant new destructive authority. Preserve other slots, titles and app data.
Keep the existing 0.1.22 emulator Slot 1 unchanged; the reported cross-build
state-load rejection makes it unsuitable for this protocol. Do not resume
save-state experiments that the owner stopped.

## If route isolation does not identify the fault

<!-- CodexAstraUlt: Preserve exact evidence requirements for the reproduced partial-output hazard and the proposed targeted capture. -->

A source-level synthetic case already reproduces CPU/GPU output disagreement:
writing only color.xyz with an unwritten mapped alpha yields CPU alpha 0 and
GPU alpha 1. Native's register state begins at zero and persists within a batch;
the generated GPU path initializes each invocation's output/temporary vectors
with W=1. A blanket change to zero does not preserve within-batch persistence.
No captured Dark Moon shader yet ties this defect to its ghost.

If needed, implement a **default-off bounded capture of an implicated successfully
submitted draw** with immutable program/swizzle words, entry point, uniforms,
input/default mappings, ordered vertices/indices and output semantic mappings.
Correlate it with draw/pipeline identities and the verified scene. Keep game-derived
payloads private in ignored artifacts; cap records and bytes and preserve ordering,
thread lifetimes and ordinary logging behavior. Obtain a minimal replay rather
than adding a continuous diagnostic stream or per-launch bundles.

Use an independent reviewer for capture bounds, lifetime and output contracts.
Replay identical data through the production CPU and GPU paths, preserving
indexed invocation order and CPU register persistence. The ordinary PICA debugger
disables the optional GPU path under investigation and is unsuitable for this
capture. Correct a proven divergence, with a regression that fails on the old
implementation, before assigning it as the title's cause.

## Autonomous batch and ownership

<!-- CodexAstraUlt: A bounded first batch gives the owner unattended progress and a durable resume point without promising a perpetual service or unlimited account access. -->

For the first unattended batch, continue for **up to three new candidate builds
or four hours, whichever comes first**. Existing-build route tests do not consume
a candidate number. Finish saving the active test's evidence and return normally
at the boundary. Write a checkpoint after every experiment and every build/test
result; continue past routine milestones within this budget without asking the
owner to approve each reversible step. This budget is a starting operational
default, not a claim that full-speed correctness can be achieved within it.

Use one coordinator as source/release writer and one exclusive owner of the Thor
serial. Source review, shader parity investigation and evidence analysis may run
in parallel subagents when supported; actual benchmarks run sequentially. Confirm
no other operator is driving the same device before issuing inputs. No second
agent should independently install APKs or run benchmarks.

Maintain a small experiment ledger with hypothesis, exact source/APK, settings,
scene boundaries, cache state, expected discriminator, result, rejected alternatives
and next action. Preserve private raw artifacts locally and publish only appropriate
derived findings. Git commits and reports, not conversational memory alone, are
the resume state. Do not automatically relaunch endless `codex exec` loops or
bypass approvals when the session pauses.

For each source candidate:

1. Select a focused change supported by the evidence. Keep correctness, memory
   ownership and throughput hypotheses separate. Retain full draws and order.
2. Implement with `CodexAstraUlt` commentary and the existing replacement-history
   rules. Add meaningful regression coverage and obtain independent review.
3. Run applicable local checks, then use the existing shader/Android/package/signing
   gates. Reconfigure CMake after generator changes so cache identity is refreshed.
4. Recheck remote refs and release/tag versions. Push only to `RegiRex/uberhar`
   through the established release branch, using normal conflict handling and
   no force-push. Do not publish to Azahar or merge the fork's divergent master
   wholesale. A targeted upstream improvement needs its own reviewed rationale.
5. The original app signing key is retained by Actions; do not assume a local
   build has it. Download the fully validated release APK, verify exact provenance,
   checksum and signing compatibility, and update while preserving data. A
   signature mismatch is a blocker, not a reason to uninstall or generate a new key.
6. Repeat the same device checkpoints, inspect images over time and collect
   ordinary logs plus before/during/after memory. Use normal exit and save evidence.
   Record the result and continue with the next informative hypothesis within budget.

Useful review or analysis may continue during CI. Bounded polling is appropriate
inside this expressly authorized local build/test loop; do not trigger duplicate
builds or poll indefinitely without progress. Respect actual account, session and
approval limits. The laptop must remain awake with USB and the Codex session active;
this document does not install an unattended background service.

## Acceptance and stopping conditions

<!-- CodexAstraUlt: Quantified scope prevents a fast menu or one correct still from being reported as complete game qualification. -->

First qualify the repeatable opening at **2x, Vulkan, normal 100% limit** in Native,
Combo or both. Target sustained approximately 100% emulation speed, accounting for
measurement noise, without recurring stalls or visible faults. Report average
speed, lower-tail intervals, time below 95% where available, and worst stalls;
an average near 100% cannot hide recurring pauses. Do not infer gameplay results
from turbo, loading menus or an unsupported frame-counter interpretation.

Compare matching ghost/moon scene phases over time. Evaluate the moon's flashing
across consecutive frames; individual stills or ordinary changes in scene lighting
cannot establish parity. Native is the owner's nonflashing reference for the moon,
without claiming hardware-perfect output for every other case. Separate brief
visual recording from timing runs if recording materially changes performance.
Record charging, hardware mode,
fan policy and available thermal indicators; missing thermal HAL readings mean
unknown headroom. Avoid sustained 2x/4x qualification under changing power conditions.

Track own-process KGSL, process memory and consistent system-availability readings
without adding overlapping scopes. Require no continuing unexplained growth across
matched repeated content and normal-return recovery; a short plateau or one clean
exit is insufficient for full-game bounds. Verify title application-cache state;
a restart alone is not cold, and private driver cache state remains unknown.

After repeated 2x correctness/playability, advance to 4x and broader gameplay,
preserving the owner's cold-run and eventual texture-pack goals. Label scope:
passing the opening does not establish that all of Dark Moon is glitch-free.

Pause with a precise resumable report at the batch limit, owner intervention,
missing authorization/device access, incompatible signing, account limits,
unrecoverable build failure, severe thermal indication or rapid memory pressure.
End the game normally when possible. Do not repeatedly reproduce a crash or rerun
an unchanged uninformative experiment. Two consecutive candidates with neither
improvement nor new discriminating evidence trigger an architecture reassessment
before another candidate, rather than cosmetic version increments.

The completed 0.1.20 review remains the anchor. Successful 0.1.21, 0.1.22 and
0.1.23 are successors one through three. A full review is allowed now, normally
after 0.1.24, and required before starting a sixth successor (0.1.26 if numbering
continues); review earlier when new correctness or memory evidence requires it.
Keep the progress tracker, code map, roadmap and review ledger current.

At each stopping point, report what changed, what actually ran, image/memory/speed
results, builds and release links, remaining blockers, next experiment and the
command or prompt needed to resume. Do not claim that this browser chat has
automatically received local reports or that the 100% target is guaranteed.

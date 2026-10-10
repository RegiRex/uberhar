# Uberhar progress — 0.1.41 released; CPU Software delivery in development

<!-- CodexAstraLocal: Record actual release verification and the owner's subsequent
authorization; older stopping boundaries are historical. -->
**0.1.41 is released and verified.** Source
`ea20992a947facff00ac3b0b0f71f141da0dd16f` passed all 29 required release checks.
APK checksum, source/tag binding, native symbols and signing compatibility with
installed .40 are verified. The first run failed because Ubuntu changed the
Lavapipe manifest filename; a workflow-only correction supports both filenames.
<!-- CodexAstraLocal: Owner feedback is title evidence with a distinct scope;
do not turn an unmatched manual run into a measured causal speedup. -->
The owner tested .41 in Dark Moon and reports acceptable graphics and visibly
better performance: 4x felt better than the earlier 2x experience, and 2x ran
well. Retained logs confirm Combo at Vulkan 2x and a Native session that changed
between 2x and 4x, with simulated GPU timings enabled. These are manual tests,
not a controlled comparison or a demonstrated sustained 99% result. The owner
observed only one or two brief flashes and prioritizes further frame-rate work.

The owner has resumed implementation and authorized the next feature build:
selectable CPU Software graphics at 1x on Android, with one/two/automatic CPU
rendering participants and presentation-only GPU use. Native/Combo also receive
a targeted queue improvement across redundant topology/restart writes.
Calculated integration progresses independently and must not delay delivery of
the usable Software experiment. The owner has installed .41; its exact APK
identity and the existing signing/save-preservation requirements still apply.
Root alone operates the Thor; completed or waiting agents stop until reassigned.
Consolidated hardware tests and a self-contained session report remain required.
The [experiment ledger](UBERHAR_EXPERIMENT_LEDGER_0.1.40.md) separates actual
hardware tests from host-only experiments and untested proposals.

## Completed .40 tests

<!-- CodexAstraLocal: Record actual delivery and the owner-requested timing controls without treating a settings experiment as another release. -->
**0.1.40 is released and installed.** Source
`7bc71c5c9c7e10a39558084392330faa884889ba` passed all 28 required release steps.
Installed bytes match the qualified APK and compatible signing identity. The
static prepared TEV tier actually renders CPU-vertex draws in Native and Combo;
Combo also continues using ready full specialization. No additional guest
vertex work moved to the GPU. Source and separate cleanliness reviews passed.

The existing **Settings → Debug → Simulate 3DS GPU Timings** toggle works in
Native and Combo and is not overridden by Calculated's preset. It delays guest
GPU-completion interrupts; no .41 UI change is necessary. The .41 correction
above addresses a separate cached-buffer ownership obligation.

Three cold Dark Moon controls have observed normal exits: Native off averaged
38.771% (slowest26.345%), Combo off33.564% (slowest18.879%), and Native on68.598%
(slowest57.046%). All use Vulkan2x and normal100%, with File1 reset and empty
application shader caches. Whole-app CPU averages1.919,1.679 and2.031 logical
cores respectively, including the workers. Scene weighting and actual worker
allowances differ; the mean ratios are not exact causal speedups.

The fourth Combo-on run was interrupted by a credit limit. Its finite monitor
stopped after300s; later teardown is recorded but root did not observe the exit
input. Its independently reviewed bounded sample averaged 66.361%, with a slowest
window of 38.256% and 2.050 app logical-core equivalents. It is not a matched
complete opening. No replacement game run was performed. See the
[test review](UBERHAR_TEST_REVIEW_0.1.40.md) for all four scopes.

Native-on's opening moon clip has56 events across78 decoded frames, maximum3;
this crosses the owner's investigation thresholds. Native-off has4 isolated
moon events across its two moon clips and7 isolated early ghost events. Combo-off
has0 counted events in its two actual clips, with later phases unobserved. Faster
scene progression does not qualify correct graphics or sustained99% speed.

The deferred CPU packet queue remains coordinator-only, one packet per wave,
with no observed auxiliary fan-out. Existing same-draw workers are useful but
the portable multicore objective remains unmet. Future architecture must review
guest pacing, resource lifetime/visibility and slot reuse alongside useful CPU
work distribution; the recorded artifacts do not yet establish their cause.
Measured DDR bandwidth remains unavailable, with no clock/pressure proxy.

The owner stopped automated visual review and will judge noticeable artifacts.
Native-on ghost and Combo-on temporal reviews remain unfinished. Architect and
Developer/Engineer are now separate roles; the Calculated agent works solely on
its independent private prototype. Complete Calculated title rendering remains
unqualified. The owner now authorizes implementing concrete architectural corrections and
testing them on hardware, superseding the earlier pause. The next build must
contain a potential issue fix, with a clear experiment and outcome. A proposal
ledger separates untested, host-tested, hardware-tested and shipped work. The
subsequent next-build pause at the top of this document is the current boundary.

## Historical 0.1.39 results

<!-- CodexAstraLocal: Retain earlier observations without labeling them as the currently installed version. -->
One cold Native and one cold Combo .39 Dark Moon opening have completed at Vulkan
2x and the normal 100% limiter. The independently reviewed primary means are
30.476% and 28.763%, with p05 window speeds 17.586% and 17.246%. Slowest/best windows
are 14.591%/68.868% and 16.275%/65.106%; worst frame intervals are 209.405/240.760 ms.
Different scene timing, observers and process history prevent a causal mode/version
comparison. Whole opening-contained telemetry averages 1.551/1.462 app logical-core
equivalents, which already include every worker. Per-core system activity does not
measure app residency or heterogeneous CPU maximum capacity.

<!-- CodexAstraLocal: Consecutive recorded frames qualify only their visible
scene coverage; moon-event thresholds remain separate from ghost review. -->
The .39 Native moon clip contains ten brief events across eleven decoded frames,
maximum two consecutive frames, exceeding the owner's occurrence threshold.
Combo's 743 recorded moon frames contain no identified block/band event.
No abrupt ghost corruption was identified in either run's reviewed post-moon
clips; scene coverage differs and leaves unrecorded gaps, including the later
angry-book phase. This is not full-opening visual qualification or a causal
improvement claim. Private evidence and charts remain outside public artifacts.

The .39 deferred queue executed 129,171 Native and 129,119 Combo packets in its
interior delivered report spans, with zero failures. Every packet ran in a
one-packet wave on the coordinator; no auxiliary fan-out was observed. Its new
route therefore executed useful shader work without demonstrating the intended
multi-packet scaling. Existing same-draw workers remained active. Tiny/light CPU
draws are still excluded, and increasing thread count alone is not a remedy.
The .40 GPU fragment change addresses a different cost hypothesis and does not
qualify the CPU objective as complete.

The owner authorizes .40 implementation and its consolidated tests, then a pause
of all non-architecture work with a self-contained results/interpretation report.
Keep exact graphics review separate from the numeric pass. Best/slowest CPU
observations that only overlap a frame window remain contextual; GPU busy is a
driver window, memory scopes are non-additive and DDR/PMU measurements remain
unavailable. Passive pressure/occupancy readings are not bandwidth utilization.


<!-- CodexAstraLocal: Record the owner's coordinated architecture priority and separate planned diagnostics from measured bandwidth. -->
The current priority is a coordinated portable CPU/GPU execution design. A
dedicated CPU architect is developing bounded immutable draw work with ordered
commits; a GPU coarchitect reviews useful stage placement, ownership and memory
traffic. Worker budgets must respect available logical CPUs, SMT where present,
heterogeneous capacity and other emulator/driver threads. The Thor exposes eight
single-threaded cores with different capacity hints; a portable design must not
assume that topology on other machines. More occupied cores alone are not a gain.

Optional memory-bandwidth diagnostics are being qualified externally for the
next update's tests. The Thor exposes bandwidth-monitor and memory-stall trace
event metadata, but event existence does not establish readable traffic, units,
scope or a percentage of peak bandwidth. Requested bus votes and frequency remain
separate from measured traffic. The installed Perfetto producer crashes while
parsing a vendor kernel event; its empty captures do not measure zero bandwidth.
Direct CPU PMU access was denied. External diagnostics must retain these limits,
and optional profiling must not silently change the device's security policy.

<!-- CodexAstraLocal: Close the scoped implementation search and retain the owner's explicit deferral. -->
**Circle Pad Pro Calibration is low priority after 1.0.0.** A scoped search of
nine pinned public emulator/homebrew source snapshots found no usable calibration
applet to port. The
[Azahar factory](https://github.com/azahar-emu/azahar/blob/0e486b07e987b4c1ceb0b7006a1eb725f2494aca/src/core/hle/service/apt/applet_manager.cpp#L561)
and [Panda3DS factory](https://github.com/wheremyfoodat/Panda3DS/blob/92e53d26b898eb89d865f8407460900a88e7c0fa/src/core/applets/applet_manager.cpp#L17)
do not implement it; the
[devkitPro example](https://github.com/devkitPro/3ds-examples/blob/be2001fee08cf8ec7d3366e095e83f6f75da8942/libapplet_launch/source/main.c#L48)
only launches an existing system applet. Accessory IR calibration packets are not
the missing APT applet lifecycle. Unavailable original Citra/PabloMK7 histories
remain unexamined, so this is not a claim that no implementation exists anywhere.
No speculative HLE replacement is scheduled during the multicore work.

<!-- CodexAstraLocal: Bind actual gated delivery and independently closed single-run numerical/temporal evidence. -->
**0.1.38 is released and installed**, source
`4902aa2333ae536c786168ea4d6f2776ef296f83`. All 27 required steps in
[run 37955914178](https://github.com/RegiRex/uberhar/actions/runs/37955914178)
passed. APK checksum, compatible signer, actual installed identity and symbol
build IDs were verified; the upgrade preserved application data and protected
state. It removes
packed-register temporary arrays, reserves CPU Vulkan vertices before potentially
invalidated pass/pipeline setup, and permits explicit system-shell profiling of
the optimized Uberhar flavor. Focused register gates pass on host and ARM64; the
source-derived stream-order gate passes its finite recording controls. Native
integration and CTest pass, as do independent architecture and cleanliness reviews.
The [release note](releases/0.1.38.md) records exact scope.

One Native and one full Combo cold opening are complete, each with File 1 reset,
verified empty application Vulkan caches, 2x resolution, the 100% limiter and
normal return. Driver-internal cache state remains unknown. Independent numerical
and temporal reviews are complete: Native averages 33.185% normal speed across
36 complete windows; Combo 30.805% across 43. Native's complete CPU capture uses
1.772 app core equivalents, including 0.837 across seven workers; Combo's two
captures use 1.099/1.777 for the app, including 0.448/0.762 across workers. The
missed early Native CPU window stays missing. Unequal scenes and observer coverage
prevent causal speed comparisons. No opening is repeated to fill an evidence gap.
Native has ten moon events/twelve affected frames and one isolated ghost event;
Combo has no comparable event in its captured moon/earlier close-ghost scenes,
with later wide ghost coverage absent. Sustained 99% speed, general graphics
correctness and heavy useful multicore execution remain unmet.

<!-- CodexAstraLocal: Owner-controlled broader testing is an explicit temporary
device-operation change; development agents continue offline. -->
Owner-controlled .38 testing has covered Sonic, Fire Emblem Awakening, LEGO City
and Monster Hunter 4 Ultimate, followed by a live Metal Gear Solid 3 calibration
applet freeze. Cold and warm runs, actual resolution and speed caps remain
separate. Root collects serialized read-only
per-core system CPU, app/thread CPU, aggregate exposed GPU busy/frequency,
ordinary logs, brief screenshots and memory/power/temperature observations.
The owner's first turbo toggle marks gameplay; later stable 400% cap periods
measure headroom separately from normal 100% cap periods and mixed transitions.
No automatic game inputs, installation or standalone benchmark runs during play.
Per-GPU-core utilization is not exposed by the retained driver counters.

Context reuse and typed dispatch were rejected after mixed/slower complete-path
host results. Shared-wake coordination and safe overlap within a draw remain
private experiments. Both openings include one bounded CPU profile. Exact-release
symbols identify the per-worker wake loop as about 11.5–11.8% of the owner's
sampled cycle periods; this is CPU work, not waiting wall time or an available
speedup budget. Large unresolved generated-code candidates remain unassigned.
The finite standalone shared-cohort comparison stopped before any timing because
its eight-processor precondition failed. The exact process context is being
diagnosed; the failed attempt supplies no performance result.

<!-- CodexAstraLocal: Bind current progress to gated delivery and actual finite device evidence, without turning coverage counts into throughput. -->
Released **0.1.37** is `f6e0d88842ff8204e112a7368035e4565848146d`.
All 27 required steps in [run 37939663409](https://github.com/RegiRex/uberhar/actions/runs/37939663409)
passed. APK checksum, signing compatibility and installed identity were independently
verified; installation preserved app data and the protected .22 state. The failed
first workflow remains retained separately: its test harness hid `SYS_futex` before
standard headers. The corrective source changed that control and CI diagnostics,
without changing the candidate runtime.

One cold Native and one full Combo Dark Moon opening are complete, each with UI
File 1 reset, title Vulkan cache deletion, Vulkan 2x, the normal 100% limiter and
normal return. Application cache inventories were empty; driver-internal cache
state is unknown. Both Thor panels are now off and timeout restored to 60 seconds.
No method is repeated to fill missing observations.

Native's predeclared bounded observation through 175.135 seconds contains 34
complete report windows averaging **32.082% normal speed**, with a 204.200 ms worst
reported interval. An operator delay left later dialogue waiting: its full
711.991-second A-to-Back span is descriptive only, not the opening benchmark.
The selected-output proof admits 92.991% of completed attempted CPU vertex work,
but seven workers together account for only about **0.698 and 0.872 cores** in two
20-second activity windows. Whole-process CPU, which already includes workers,
is about 1.484 and 1.938 cores. All eight CPUs were allowed at the observed
scheduling endpoints. This is broad admission, not heavy multicore utilization.
The primary throughput objective remains unmet; unlike scenes and conditions
prevent causal version/mode speed comparisons.

Sparse Native phase samples contain 349.036 ms total: planning 9.212%, pool 72.026%
and ordered submission 18.762%. Owner processing and join are nested inside pool;
its residual is not a wake-syscall measurement. Deterministic samples exclude draw
preparation and other frame work and cannot be multiplied by 257 into a title
budget. Actual per-participant shader-context composition subsequently produced
mixed/slower host costs and was rejected. Shared-wake coordination is the next
private CPU experiment, with explicit registration, lifetime and shutdown controls.
Reusable value-only input layout preparation remains a further hypothesis.

Native's 536 recorded moon frames contain 12 events affecting 13 frames, including
one two-frame event. This crosses the owner's more-than-five-event threshold and
is a priority to investigate. Its 733 ghost frames contain six isolated single-frame
bands. Combo's 548 moon and 639 ghost frames contain no comparable observed event;
earlier close ghost coverage is missing. Frame capture, rendering cause and causal
frequency differences remain unqualified. Independent numerical reviews passed for
both methods. Combo averages **31.978% normal speed** across 42 complete windows,
with a 385.032 ms worst interval; a conservative observer-free supplement averages
31.471%. Its first CPU capture was interrupted and stays incomplete. In its planned
second window workers total about 0.781 cores and the whole process about 1.922.
All seven workers together remain below one fully occupied core in these windows. One post-return ownership check showed KGSL falling from
about 3,171 MiB during the run to 13.34 MiB; this is not a long-play growth bound.

<!-- CodexAstraLocal: Keep secondary correctness prototypes separate from delivered rendering and the active three-build review. -->
The inherited shader main-return sentinel was fixed in .35. A separate guest
byte/halfword callback carrier issue has a private consumer correction and
reproducible archive-overlay proof; no title trigger or speed gain is established,
and it is not shipped in .37. Independent Calculated work has finite original-input,
ordered-triangle and textured Vulkan proofs on a software host driver. Its latest
ETC1/ETC1A4 decoder matches 8,192 texels against two oracles, but filtered rendering,
complete title state and Adreno execution remain pending. No Calculated device run
is authorized by those partial proofs.

Separate full [architecture](UBERHAR_ARCHITECTURE_0.1.37.md),
[cleanliness](UBERHAR_CLEANLINESS_0.1.37.md) and [test](UBERHAR_TEST_REVIEW_0.1.37.md)
reviews close the third-successor boundary since .34 against delivered .37.
Their finite scope does not qualify sustained 99%, 4x, general graphics or beta.
Continue authorized CPU iterations; the larger dispatch/overlap experiments
remain separate from the selected .38 preparation change. A source-derived stream-wrap command-
order control passed independent finite review, with no title-event cause assigned.

<!-- CodexAstraLocal: The owner's latest October 9 follow-up removes the hard two-build limit for the primary multicore upgrade. -->
- Continue beyond .35/.36 when needed to demonstrate meaningful use of available
  CPU cores, improved throughput and no new division-of-work stalls, ordering or
  buffering faults. Use evidence and engineering judgment for a resumable stopping
  point. The earlier two-build boundary is superseded, while periodic progress
  summaries and independent architecture/cleanliness reviews remain required.
- Treat the suggested 70–100% aggregate CPU use as an aspiration, not a correctness
  or performance definition: serial dependencies, synchronization and GPU work can
  limit speed earlier. Report actual per-thread/core work, normal speed and stalls;
  do not consume cycles merely to increase utilization. Root alone operates Thor.


<!-- CodexAstraLocal: Record the owner's new multicore-first batch without changing completed .34 evidence. -->
**Current: .35/.36 development resumed.** Priorities: portable multicore Native/Combo
CPU execution, ARM64 return correctness, independent Calculated rendering. Root
owns integration and exclusive device operations; parallel agents own shader
independence analysis, ARM64 repair and Calculated implementation. Existing .34
release remains installed; no new candidate is built or qualified yet. Test one
Native and Combo opening per build; Calculated testing waits for meaningful
implementation. After two builds complete results and separate audits; additional
regression repairs are authorized after the summary. Historical stop text below
is superseded by this owner instruction, not erased.


<!-- CodexAstraLocal: Close the authorized two-build delivery and Dark Moon audit batch without qualifying the broader performance or gameplay goals. -->
**Released .33 and .34 are built, tested and reviewed. Development stops for
owner inspection; no .35 is authorized.** The completed batch includes one strict
Calculated diagnostic and one Native and full Combo opening per build, normal
returns, systematic architecture/correctness/performance review and a separate
independent code-cleanliness/purpose audit. The added bounded MH4U observation
has also ended normally, but verified gameplay remains unqualified.

The final [test review](UBERHAR_TEST_REVIEW_0.1.34.md),
[architecture review](UBERHAR_ARCHITECTURE_0.1.34.md) and
[independent cleanliness audit](UBERHAR_CLEANLINESS_0.1.34.md) retain the exact
scope, failures, scene coverage and outstanding correctness obligations.

<!-- CodexAstraLocal: Bind the completed .34 delivery to verified source and gates; keep earlier failed diagnostic evidence intact. -->
Released .34 is `6893c10b5dab28bb60135efa4a5ebe07f82bd349`.
[Run 37885064863](https://github.com/RegiRex/uberhar/actions/runs/37885064863)
passed all 23 required release steps. Exact source, APK checksum, package and
signing compatibility were independently verified before the data-preserving
installation of **.34 / 33991465**. The protected .22 state remains unchanged.
Native compilation, CTest with five inherited firmware-dependent skips, the
full host probe (583.330 seconds), production-compiler regression and pixel
checks passed. The four CPU input recipes and narrow benchmark compiler-preamble
correction are delivered; the failed .33 scratch attempt remains a separate
preparation failure, without a performance ratio.

<!-- CodexAstraLocal: Summarize measured work and throughput separately so coverage counts and synthetic timings cannot imply a title speedup. -->
The .34 strict Calculated launch explicitly omitted all **1,032,364** guest draw
attempts for unsupported state, with zero computed guest draws and zero graphics
fallback. Shared CPU vertex processing still ran. Blank output is not a complete
rendering or speed result. Its separate supported-work scratch benchmark completed
all **32 pairs / 64 routes / 2,048 operations**, with full-image checks and finite
timestamps. The four paired compute/graphics median route ratios range from
0.905 to 1.007; they do not measure complete-title throughput.

The single .34 Native and Combo openings averaged **29.333% and 26.241%** normal
speed, respectively; every accepted reporting window remained below 99%.
Actual recipe use covered **86.444% and 84.442%** of retained no-GS CPU input
invocations. These counts establish use, not cost or a causal improvement.
The separate inclusive LoadVertices wall brackets remain about **82.293% and
67.533%**. Different scenes, process ordering, temperature and Combo's roughly
eleven-minute menu preparation prevent a matched mode/version comparison.
Combo's 1,012.491 ms worst reported interval remains explicit.

Consecutive .34 review found five isolated moon and five separate ghost events
in Native, and no comparable events in Combo's retained clips. The moon priority
threshold was not exceeded; unequal and absent phases remain unqualified.
The bounded CPU/GPU activity observations describe running time, sampled driver
windows and endpoint frequencies, not maximum hardware capacity. Longer gameplay
memory growth and full-title correctness remain unqualified.

<!-- CodexAstraLocal: Close the bounded cross-title observation and verified save restoration without treating transition evidence as completed gameplay qualification. -->
**The added bounded MH4U observation ended normally.** It covers loading/cinematic
transitions only; no verified gameplay sample or save confirmation occurred.
After an explicit pause at the save prompt, the run exited without continuing
beyond that boundary. One save file changed despite no confirmation; the guarded
restoration from retained backups completed, and all 15 original title/save/extdata
files match. The screen timeout was restored and the device put to sleep. This
closes the bounded observation, not a complete-gameplay or maximum-capacity
comparison; it does not reopen the completed .33/.34 source audits.

<!-- CodexAstraLocal: Reset the finite audit cadence only after delivered controls and both independent review tracks close, then honor the requested development stop. -->
The new [review anchor](UBERHAR_REVIEW_CADENCE.md) is **released .34, with zero
successors**. No .35 implementation or release is selected. Sustained 99% at 2x,
the longer-term 4x target, independent general Calculated rendering, longer-play
memory behavior and the broader beta qualification remain unmet. Follow-up
proposals and inherited correctness concerns are documented for owner review;
this checkpoint does not resume automatic development toward 0.2.0.

<!-- CodexAstraLocal: Retain prior progress text verbatim as historical checkpoints; its then-current instructions do not override the completed .34 stop above. -->
<details><summary>Earlier .33/.34 preparation and .32/older progress checkpoints — historical</summary>

<!-- CodexAstraLocal: Bind the published .33 source and distinguish the next local candidate from delivered device behavior. -->
The .33 source is published at `2dc50832c4fefa16f15ba86e6cccb76a6b432452`.
[Android/shader prerelease run 37879803076](https://github.com/RegiRex/uberhar/actions/runs/37879803076)
passed all 23 required shader/Android/signing/publication steps. Exact source,
APK checksum, package and signing-compatibility checks passed before a
data-preserving installation of .33 / 33991058. Its single strict
Calculated diagnostic reports 837,453 unsupported draws omitted, zero computed
draws and graphics fallback disabled. The separate scratch request failed shader
preparation before recording work, so it provides no performance ratio. The one
Native and full Combo openings completed normal return, with mean normal speeds
29.099% and 28.310%. Their complete reporting windows all remain below 99%;
different scene/observer/temperature conditions prevent a causal gain claim.
Native records three isolated single-frame moon events and three separate ghost
events in covered later phases; earlier quiet ghost checkpoints are unobserved.
Combo records no comparable moon or ghost block events in its captured regions;
its earlier near-ghost checkpoint is also unobserved. The .34 input recipes and
narrow benchmark compiler-preamble correction are integrated. Final merged native
compilation, CTest (with five firmware-dependent skips) and the full host probe
(583.330 seconds) pass. The compiler regression executes the production Vulkan
compiler: twelve modules validate and the original conflicting preamble fails as
expected. Independent source and cleanliness reviews have closed implementation
findings. Publication, target testing and the requested two-build results,
architecture and separate cleanliness reviews remain open.

<!-- CodexAstraLocal: Resume the owner's next two builds while preserving the completed .32 review as historical evidence. -->
The owner has resumed work for **0.1.33 and 0.1.34**, followed by testing and
another architecture/results and separate cleanliness audit. Calculated must
disable Native/graphics fallback for the next build. Unsupported output will be
explicit, and an empty or incomplete image will not count as a performance gain.
The current kernel remains a restricted rectangle implementation; no complete
independent Dark Moon renderer is claimed. The strict Draw path is implemented locally and passes focused route proof.
Backend refusal and terminal reload handling pass their focused failure controls;
the merged native build, full host probe and CTest pass, with five
firmware-dependent skips. The bounded scratch benchmark passes its actual-owner
and injected-failure controls, original-input pixels and Vulkan module validation.
The merged compute pixel gate also passes, and the Android-only benchmark object
compiles with the pinned NDK for ARM64/API 33.
The private CPU input-recipe experiment passes whole-path output/lifetime checks
and A64 compilation; host gains and fallback regressions remain provisional.
The .33 Android delivery and bounded device observations are recorded above.

<!-- CodexAstraLocal: Retain the delivered review result below; its pause was superseded by the explicit two-build instruction above. -->
## Completed 0.1.32 review checkpoint

<!-- CodexAstraLocal: Keep the latest owner implementation order, consolidated testing and explicit review boundary visible above historical scope. -->
The scheduled 0.1.31 adaptive CPU fragment work and 0.1.32 Calculated extension
are delivered and tested, with one cold opening per method per build. The
systematic architecture/correctness/performance and separate cleanliness/comment
reviews are complete. **That .32 checkpoint paused for owner inspection**; the current two-build
authorization above supersedes the historical stop.
Independent calculation, correct graphics and sustained 99%+ remain the goal;
0.1.32 does not qualify them. The current .33/.34 batch is separately authorized; automatic 0.2.0
continuation remains outside this batch.

<!-- CodexAstraLocal: Bind verified delivery and bounded target results to the released source without claiming product qualification. -->
**0.1.32 passed all 22 required release gates** in
[run 37750392228](https://github.com/RegiRex/uberhar/actions/runs/37750392228)
at `6486198d52df144e184165133c716ffa01a05f26`. Exact source, checksum, package
and signing compatibility checks passed, followed by a data-preserving update
to **0.1.32 / 33984206**. APK SHA-256:
`bee332a5504a9323609f5bb7557e16319fb0c70f4772391fdb6b2be108bc040a`.
All four single cold openings completed with normal returns: **20.957% Calculated,
20.964% Native, 23.185% full Combo and 19.455% ComboGeneric** mean normal speed.
Every accepted reporting window remained below 99%. No skipped draws or required
fallback failures were reported; scene and observer differences prevent a causal
speedup claim. Calculated recorded zero compute draws: all lifecycle attempts
failed state admission before geometry/format despite a prepared compute pipeline.
The host-validated extension has not demonstrated Dark Moon coverage improvement.
Consecutive review found zero/two/zero/two isolated moon events in Calculated,
Native, Combo and Generic respectively, all below the owner's priority threshold.
Calculated and Native have four/two separate single-frame ghost blocks; the
prior dense ghost patches are absent in the covered cores. Unequal scene coverage
and capture limits prevent broader correctness or frequency claims.
See the [test review](UBERHAR_TEST_REVIEW_0.1.32.md),
[architecture review](UBERHAR_ARCHITECTURE_0.1.32.md) and
[separate cleanliness audit](UBERHAR_CLEANLINESS_0.1.32.md).

<!-- CodexAstraLocal: State the review's next proposed evidence and implementation order without starting another iteration. -->
The next proposed performance work targets the complete CPU vertex path, which
still occupies about 83.7% of separate Calculated vertex-report wall time.
Select exact input-loader recipes only after representative workload coverage
and whole-path output/cost checks. Further compute-pixel expansion first needs
bounded exact failed-state examples: raw ColorWrite/Blend counts alone did not
identify states admitted by .32. These are resumable proposals, not implemented
speedups. The new full-review anchor is released .32, with zero successors.

<!-- CodexAstraLocal: Preserve the final device state and local documentation-only correction without changing released identity. -->
Both Thor screens are off, the original 60-second timeout is restored and the
protected .22 state remains unchanged. Six local comment lines cover three older
Android lifecycle blocks missing CodexAstraLocal attribution; byte-reconstruction
checks confirm no executable change. They are preserved for the next authorized
source build, separate from installed .32 and this documentation-only checkpoint.

<details><summary>Earlier .30/.31 implementation and testing checkpoints</summary>

<!-- CodexAstraLocal: Advance the reviewed adaptive candidate only through completed host gates; release and target results remain separate. -->
**0.1.31 is integrated and passes full local validation.** Bounded CPU fragment
pipeline replacement addresses .30's menu-filled bank while retaining eight
physical slots, complete generic fallback and separate compiler/GPU lifetime
proofs. The fixed joint Calculated census preserves admission and guides .32
coverage without inspecting vertex payloads. Independent correctness, cleanliness
and combined-hook reviews found no blocker. Fresh native integration and CTest
pass with the same five firmware-dependent skips; the full host probe passes
in 363.786 seconds. Policy checks pass 643 cases plus nine intended failures;
actual helper/binding checks pass 6,698 plus seven; census checks pass 41,283
with four private intended-failure controls. Existing worker and CPU fragment
ABI/image/binary gates remain required. See [0.1.31 notes](releases/0.1.31.md).
<!-- CodexAstraLocal: Record verified delivery separately from incomplete device qualification. -->
[Run 37738256467](https://github.com/RegiRex/uberhar/actions/runs/37738256467)
passed all 22 required shader/Android/signing/publication steps at source
`775d1437c9c9cbb5b9756ca6861c1e25608c877b`. Exact-source, downloaded checksum,
package and signing-identity checks passed before a compatible data-preserving
install of **0.1.31 / 33983499**. APK SHA-256:
`f844c066bf39cf37598d02c788a170494dada6dd578304ad2019ea7d31e5ef6f`.
One cold run per method has completed with normal returns: mean speed is
20.666% Calculated, 20.871% Native, 22.792% full Combo and 19.439% ComboGeneric.
All accepted windows remain below 99%; scene mixtures prevent a causal speedup
claim. No skipped draws or required fallback failures were reported. The .29
review anchor now has two gated release successors.

<!-- CodexAstraLocal: Separate demonstrated adaptive use, zero computed coverage and the exact scope of the next implementation. -->
Full Combo's adaptive CPU bank recorded 624,679 selections between two bounded
opening interior records, including 377,310 lit draws, with 43 retirements and
destructions. Eight physical slots remained the limit. A delayed cleanup check
found 13.00 MiB same-process GPU memory; longer gameplay growth is not qualified.
Calculated still computed zero draws. Its useful ColorWrite/Blend census evidence
is concentrated in menus and the opening boundary, not a proved broad gameplay
opportunity. The .32 partial-mask/endpoint replacement implementation is integrated
and passes fresh native builds, CTest and 41,890 census checks. The complete
host probe passes in 404.578 seconds, along with both pixel comparison suites
and independent source/cleanliness review. Release gates and the final device
review subsequently completed as recorded above.
It retains the roughly 83.7% inclusive CPU vertex wall
bracket observed in Native/Calculated. The
[Calculated design](UBERHAR_CALCULATED_DESIGN.md) records substantial alternatives
and the exact semantics needed before moving that work onto the GPU.

<!-- CodexAstraLocal: Distinguish the integrated candidate's completed proofs from pending delivery and device controls. -->
**0.1.30 is published and installed after all release gates passed.** Full Combo can select
already-ready optimized fragments for CPU vertex draws, retaining the exact
software layout and complete generic fallback. A separate eight-entry CPU
pipeline bank stays within the existing combined limits and shares the optional
worker. Queue and worker exceptions become bounded optional failures. Native
integration/CTest, the full host probe, actual selection/worker negative controls
and CPU vertex/fragment image and binary gates pass. Independent reviews found
no blocker. See [0.1.30 notes](releases/0.1.30.md) for proof scope and limits.
The exact source is published at `b3ea97f4f059af5aa1a4644a16cd9afc0b04343b`;
[run 37707003464](https://github.com/RegiRex/uberhar/actions/runs/37707003464)
passed all 22 required shader/Android/signing/publication steps without waivers.
Exact-source, downloaded asset, package and signing-identity checks passed,
followed by a compatible data-preserving install of **0.1.30 / 33981241**.
APK SHA-256: `63ba6d7c3f1b129cbc35d413757d1fd79e5788ba9c3b4bf1a2030a70f2db7d41`.
The completed cold full-Combo opening averaged **20.932%** across 40 complete
windows (fifth-percentile window 16.611%, worst interval 315.366 ms). Its eight CPU
optimization slots filled during menus, and CPU-specialized selections stopped
increasing about 12 seconds into the opening. Most measured gameplay did not
exercise the new route; bounded admission is under review. Four fragment routes
close with zero reported failures/skips. Delayed normal-exit cleanup observed
13.06 MiB GPU memory after 2,859.02 MiB during the opening.

<!-- CodexAstraLocal: Keep temporal coverage and actual route use separate from candidate acceptance. -->
The 343-frame moon clip showed no comparable event. All 506 ghost frames were
reviewed: two covered target cores lacked the prior corruption, while the
book-holder and later rear target remain unqualified. Native and ComboGeneric
completed normal return at 20.986% and19.518% mean speed. Their fifth-percentile
windows were 16.077%/13.691%, and worst intervals 337.695/351.160 ms; no skipped draws
or optional failures were reported. Native's moon has three isolated singles,
ComboGeneric one, all below the owner threshold. Native has a separate single
rear-ghost event; Generic's later ghost targets are unobserved.

<!-- CodexAstraLocal: Investigate an observed timing change without assigning cause from unmatched process history. -->
Native was slower than its .29 observation despite identical saved settings;
the same camera cue arrived about 26.8 s later. Unlike .29, this Native control
followed Combo in a reused app process. Source review found the new optional CPU
route disabled and no PICA/JIT/input/output logic change. A fresh-process repeat
still averaged 20.940% across 40 complete windows (fifth-percentile window 16.078%,
worst interval 283.733 ms), although process memory returned near the earlier
baseline. Reused-process memory is therefore insufficient to explain the decline.
The repeat retained identical settings, an empty application cache and zero
reported failures/skips. Its 349 moon frames contain three isolated singles;
586 ghost frames lack the prior dense patches in covered cores, with later
electrical/rear targets unobserved. No cause or matched speedup is claimed.

<!-- CodexAstraLocal: Distinguish released instruction structure and endpoint scheduling observations from a causal performance profile. -->
Seven released ARM64 hot functions have equal instruction counts, opcodes and
register structure; 114 of 2,547 instruction words differ in address/displacement
fields. This does not prove all resolved targets, caller behavior, instruction-cache
effects or runtime conditions equivalent. A final read-only placement control
found the emulation owner using about 9.87 CPU seconds over 10.00 elapsed seconds,
with 19.70 ms of runqueue wait. Eleven last-CPU observations sampled the middle
cluster and none the prime core; these are endpoints, not continuous residency
or a cause for the version difference. The control averaged 20.800% over 40
complete windows, or 20.273% after excluding four observer-overlapping windows.
Settings were unchanged; no affinity, clocks or security controls were changed.
The requested one-run-per-method rule governs subsequent builds; no further
0.1.30 openings are scheduled.
The .29 full audit remains the anchor with **two successful release successors**;
0.2.0 remains unqualified.

</details>

## Completed 0.1.29 controls and review

<!-- CodexAstraLocal: Advance the reviewed per-draw call candidate without declaring pending release or device gates complete. -->
**0.1.29 is implemented and passes local host validation.** Eligible CPU draws now bind
the existing shader function/live uniforms/entry once, preserving generated code,
register carry, FIFO and assembly. Independent source review, the real native
integration build and CTest pass. The focused regression passes 1,920 complete
draw comparisons with profiler off and another 1,920 with it on. The full host
probe passes. All Android, shader, signing and publication gates passed in
[run 37695964933](https://github.com/RegiRex/uberhar/actions/runs/37695964933)
at `0136f89d429eab2b25f626703b2158b9ebe5091d`.
The [published prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.29)
passed exact-source/digest/package/signing-identity verification and installed
**0.1.29 / 33980566** with app data preserved. APK SHA-256:
`f175e3e8cd57785395224fcd3eb13bed2ded62dd42e54eec15b0b798d59c52c5`.
Cryptographic signing passed in CI; local certificate parsing verifies identity.
Cold Native/Combo controls completed normal return; no device gain is claimed. See
[0.1.29 notes](releases/0.1.29.md).
<!-- CodexAstraLocal: Integrate independently checked controls without converting unmatched scene mixtures into a version speedup. -->
The two openings average **27.869% Native and 21.892% full Combo** across 40
complete normal-limit windows each, all below 99%. Settings match their respective
prior controls, and complete-zero application cache, no-skipped-draw and failure
checks pass. Native's lower fifth-percentile window is 20.781% with a 208.762 ms
worst interval; Combo's is 16.628% with 222.201 ms. Direct vertex brackets cover
83.81% and 54.90% of separate complete reporting-window wall populations; these
are not thread-CPU/GPU shares. Native moon review finds five isolated singles,
within the owner's reference threshold, and one separate single-frame rear-ghost
disturbance. Combo's 417 reviewed moon frames show no comparable event; three
bounded ghost cores lack the older dense patches. Its clip ends before the later
Native rear-ghost phase, which remains independently tracked.

The [three-build architecture review](UBERHAR_ARCHITECTURE_0.1.29.md) and separate
62-file cleanliness review found no new blocking defect in the delivered changes.
The final 17-file purpose-comment patch is integrated with identical executable
tokens/ASTs. Final visual integration closes the audit at the new **0.1.29**
source anchor, with zero completed successors. The next
candidate gives CPU-generated vertices access to already-ready optimized
fragment pipelines while retaining exact software layout and complete generic
fallback. Its host ABI, state-transition, key, failure and ownership proofs have
passed; release gates and target measurements remain separate.

<!-- CodexAstraLocal: Retain the earlier changed-overlay control as bounded historical evidence, not a cause or cure claim. -->
The existing-.28 overlay-isolation opening completed a normal return; all 405
moon frames show no comparable event with performance text hidden. Slightly
shifted scene progression and a single repeat prevent a causal or cure claim.
Touch controls stayed enabled. The separate rear-ghost review still finds one
single-frame and one two-frame disturbance, so text removal is not a general cure.

<!-- CodexAstraLocal: Replace pending gates with verified publication and a compatible data-preserving upgrade; device acceptance remains separate. -->
**0.1.28** groups exact contiguous CPU output copies after resolving final
semantics and repairs Android performance-overlay callback ownership. Host
production-path and lifecycle checks passed, followed by every shader, Android,
signing and publication gate in
[run 37682868970](https://github.com/RegiRex/uberhar/actions/runs/37682868970).
The [prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.28) APK SHA-256 is
`128f25a5f3630e0cefc3d3ca4553f572a856ebf3cfcb51b9cdf2701a64a7b30a`.
Exact-source provenance, asset digests and signing identity passed verification;
cryptographic APK verification passed in CI. The data-preserving upgrade installed
**0.1.28 / 33979906**. Cold Native, Combo and owner-requested calculated-mode
controls completed normal return. No device speedup is established. See
[candidate notes](releases/0.1.28.md).

<!-- CodexAstraLocal: Close the two measured controls and use complete vertex-window brackets without assigning residual wall time to the GPU. -->
The 0.1.28 Native and Combo openings averaged **28.424% and 21.799%** normal
speed across 40 and 39 complete windows respectively, with every window below
99%. Each launch verified Vulkan 2x, the 100% limiter, File 1 Empty and complete
zero saved application-cache inventory. Draw/fallback/shader failure checks pass.
These unlike scene mixtures, recording intervals and display conditions are not
a controlled speedup comparison. Direct complete reporting-window brackets place
**83.92% Native / 57.71% Combo** wall time inside CPU `LoadVertices`; this includes
its setup, decoding, execution and assembly, and is neither thread CPU time nor
a function-specific share. The remaining time is not a GPU measurement.

All 439 Native moon frames were reviewed, finding four isolated one-frame
rectangular events. All 415 Combo moon frames show no observed recurrence of the
earlier segment flashing or distinct rectangular events. The reviewed near,
foreground and book-holder ghost sequences show none of the old dense irregular
patches. The .27 rear-ghost event was not observed in the .28 Native counterpart;
bounded observations cannot establish a cure or full-game correctness.

<!-- CodexAstraLocal: Preserve the owner's exact temporal priority rule and the calculated mode's limited implemented scope. -->
Moon events become a fix priority above five occurrences, if any lasts more than
two consecutive decoded frames, or if at least three each last two or more.
The observed Native singles remain reference data. Ghost corruption is scored
separately. The calculated baseline must report actual eligible/computed/fallback
draw counts: its present compute path supports solid rectangles, with other work
falling back to Native. Zero computed draws cannot qualify general 3D compute.

<!-- CodexAstraLocal: Close the calculated control using actual route counters; do not describe native fallback as a 3D compute benchmark. -->
Calculated mode averaged **27.337%** across 40 complete normal-limit windows,
all below 99%, with a 20.623% lower fifth-percentile window average and 244.165 ms
worst observed interval. All **5,087,963** considered lifecycle draws were
rejected by the narrow compute state contract and rendered through Native;
eligible rectangles and computed draws were both zero. These counts include
setup/exit and are not opening-only coverage. Complete reporting windows put
83.87% of elapsed time inside `LoadVertices`. During-opening GPU allocations
remained around 1,072 MiB at both readings, with battery temperature 29 C;
thermal headroom is unavailable.

<!-- CodexAstraLocal: Apply the owner's occurrence threshold to new consecutive-frame evidence without assigning a rendering or recording cause. -->
Calculated moon review found **seven isolated one-frame rectangular/band events**
across 424 decoded frames; each clears on the following frame. This crosses the
owner's greater-than-five threshold and is now a priority for a bounded visual
discriminator. The run selected zero compute draws; that fact does not identify
the cause. Rendering, presentation and recording remain to be separated. Ghost
review stays independent, and the clean bounded Combo clip is not overwritten.

<!-- CodexAstraLocal: Close the single-variable existing-build control without assigning cause or treating native fallback as calculated rendering. -->
The calculated overlay-off control averaged **26.627%** across 40 complete
normal-limit windows, all below 99%; its lower fifth-percentile window was 20.448%
and worst observed interval 220.740 ms. Ordinary route checks again found zero
computed draws. The effective saved configuration differed only in performance
text being hidden. All 405 consecutive moon frames show no comparable event;
slightly shifted progression and one repeat cannot establish causality. The rear
ghost still has a one-frame event and a separate two-frame event (66.811 and
142.789 ms); matched primary ghost cores do not show the older dense patches.
The measured opening ends at first Back, excluding the delayed normal-exit tail.

<!-- CodexAstraLocal: Record a fresh Native baseline and keep brief tolerated moon events distinct from a longer rear-ghost disturbance. -->
A fresh cold 0.1.27 Native/Vulkan/2x opening averaged **27.976%** across 40 complete
normal-limit windows; every selected window remained below 99%. All cache,
settings and complete-draw checks passed. Consecutive moon review found five
separate single-frame rectangular events, each followed immediately by a coherent
frame. The owner accepts brief one- or two-frame loading flashes. A separate
rear-ghost rectangle spans four decoded frames (about 0.276 seconds); it remains
under investigation and is not included in that tolerance. These bounded video
observations do not establish their rendering/recording cause or full-game
correctness, and unlike Native/Combo scene mixtures cannot prove a speedup.

The owner now accepts sustained **99%+ normal speed** with the limiter at 100%,
correct graphics and zero skipped draws. Qualify 2x first, with 4x the long-term
target. Memory evidence will focus on continuing growth during longer/repeated
gameplay and pressure/crashes; normal-exit memory recovery is no longer a required
measurement every run. Parallel research is examining substantial Native and
calculated-rendering alternatives, alongside this focused candidate. The full
0.2.0 evidence gate remains open.

<!-- CodexAstraLocal: Replace pending delivery with exact successful publication and compatible installation, without advancing device qualification. -->
All 0.1.27 shader, Android, package/signing and publication gates passed in
[run 37668250513](https://github.com/RegiRex/uberhar/actions/runs/37668250513)
for `76b307a917cf2098c636492648a601e71079a2a1`. The
[published prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.27)
APK SHA-256 is
`a41ece374cacc586a2092c1797c42dfc87a2f893ea9ba8d0b9748cb0a159add4`.
Asset/provenance, package, ARM64 and compatible certificate checks passed;
cryptographic signature verification passed in CI. The data-preserving update
installed **0.1.27 / 33979207** on the Thor. The request-free, boundary-only and
detailed cold controls have completed normal return to the game list.

<!-- CodexAstraLocal: Replace the pending control with its bounded observed result and distinguish finite diagnostic coverage from workload attribution. -->
The request-free Combo/Vulkan/2x opening averaged **22.568%** across 40 complete
normal-limit windows; every selected window remained below 95%. Consecutive
review of 449 moon frames and three bounded ghost sequences found no recurrence
of the earlier flashing or dense irregular ghost corruption. Coherent
transparency bands remain unqualified. These results do not establish a matched
speedup, full-game correctness or 2x qualification.

Both finite timing reports passed the strict reader with all **64 chunks**
accepted and no clock, identity or phase failures. They exhausted their record
budgets about 6.4 seconds into the configured ten-second eligibility interval.
Boundary/detail observations contain 4,074 / 4,068 inputs and 1,753 / 1,736 actual
shader invocations, respectively; every recorded miss used the fused loader.
Detailed stage instrumentation is material at this scale. Its raw wall intervals
are not CPU shares, and differing selected populations cannot establish a precise
observer-cost ratio or be extrapolated to the whole title. Independent cohort
and ordinary-log interpretation is complete, with these limits retained when
selecting the bounded output-copy experiment.

<!-- CodexAstraLocal: Use direct distant endpoints on the same verified owner thread, independently of the tiny selected-chunk sums. -->
Across first-chunk begin to last-chunk end, the owner thread consumed **6.297 /
6.293 CPU seconds in about 6.407 wall seconds** in the two controls, approximately
98.3% of one thread. This directly brackets all intervening work, including
unobserved emulation, draw preparation and diagnostic overhead; it does not
attribute time to the selected inputs or measure GPU utilization. The controls'
ordinary opening windows average **23.121% / 23.172%**, with no temporary speed
limit. The on-screen overlay's implausible rates are excluded: a separate source
review found that stale fragment callbacks can continue resetting shared native
statistics after title exit. Independent review and focused regression support
the lifecycle repair and exact-output-copy experiment selected for 0.1.28.
Neither is a measured device gain.

All three launches verified complete zero application-cache inventories after
the title-specific Vulkan reset, and each opening reset File 1 and verified
Empty. Same-process GPU allocations recovered to **13.00 / 12.70 / 12.88 MiB**
after normal return. The protected .22 state remains unchanged; temporary timing
requests were removed after retaining their exact bytes. Battery readings were
22–26 C across these controls; thermal HAL headroom remains unavailable.

<!-- CodexAstraLocal: Record the completed installed-build discriminator and its platform limit before developing an opt-in measurement candidate. -->
The approved .26 audit checkpoint is published at
[`69c4723a5`](https://github.com/RegiRex/uberhar/commit/69c4723a5d3c6784371d218c3026b298382a028f).
A third cold .26 opening, without video, averaged **22.863%** over 34 complete
timing windows. Two bounded thread-counter observations found the main emulation
thread consumed 9.80 and 6.43 CPU seconds in approximately ten-second intervals;
sequential query duration limits their time precision and neither interval proves
an exclusive scene or a particular costly function. Normal-return KGSL was
13.39 MiB. No new graphics conclusion follows from that profiling run.

An idle one-second scheduler-trace capability check produced no scheduler data:
the stock Android tracing producer crashed on an unsupported vendor f2fs event
format before applying the requested scheduler-only filter, then restarted.
The title was stopped throughout. Further ftrace retries and security changes
are not part of this experiment. **0.1.27 adds an independently reviewed,
default-off bounded diagnostic** to
measure complete vertex chunks, separate operation wall times from owner-thread
CPU time, and provide lighter boundary-only controls for observer overhead.
Unmeasured work will remain unknown; sparse vertex timings will not be expanded
into workload shares. Rendering order, complete draws and cold-test requirements
remain the acceptance constraints.

The production diagnostic suite passed **5,833 checks**, with seven actual
producer reports accepted/classified by the independent reader and eight reader
test methods. The largest report is 44,714 bytes within its 65,536-byte cap.
The full host probe suite and native integration build passed; all 67 CTest
entries passed or retained their five existing firmware-dependent skips.
Ordinary shader generation/cache identity and the original vertex runner remain
unchanged. The completed .27 release is **successor one after the .26 audit**;
its device controls remain separate from sustained-speed qualification.
Matched observer cost and representative workload attribution remain unresolved.

<!-- CodexAstraLocal: Integrate two independent cold confirmations and the three-build audit while preserving limited visual coverage and unmet performance gates. -->
All 0.1.26 shader, Android, package/signing and publication gates passed in
[run 37587379116](https://github.com/RegiRex/uberhar/actions/runs/37587379116)
for `e83e8f4f1738d4883829cc06689ce1ab075a5ac0`. The exact release APK checksum
and compatible signing identity were verified before installing version code
33975176 with app data and the protected emulator state preserved.

Both clean full-Combo/Vulkan/2x openings reset File 1, verified Empty and
started with all application shader-cache inventories complete and zero. No
capture sidecar or diagnostic menu interrupted the scene. Independent consecutive
frame review found no earlier flashing in the recorded fully visible moon
interval and no earlier dense irregular corruption in the three matched ghost
intervals in both runs. Root corroborated selected consecutive sequences in both.
This is repeated bounded improvement, not a full-game cure. Complete opening
timing windows average **23.227% and 22.339% of normal speed**; every selected
window is below 95%. Both record zero named skipped draws/fallback failures.
The unlike scene mixtures do not establish a speedup or recorder cost.
Same-process KGSL returns to **13.32 MiB** after both verified normal exits;
short cleanup observations do not bound long-game memory. Battery readings were
25 C in run one, 26 C during run two and 27 C after its return; SoC thermal
headroom remains unknown. See the [two-run report](UBERHAR_DEVICE_TEST_2026-10-07.md).

The [full .24–.26 architecture audit](UBERHAR_ARCHITECTURE_0.1.26.md) integrates
parallel renderer, performance, delivery and visual review. Released .26 is the
new source audit anchor; completed .27 is successor one. The installed-build
thread-counter observation is complete, with the scope limits recorded above.
The raw perf-event/stat access probe was denied; its internally changed security
property was restored to its original value and verified. The 0.2.0 evidence gate,
including the retained 2x FEA baseline,
remains open. No 4x or broader-title qualification follows from this opening.

## Historical pre-publication implementation checkpoints

<!-- CodexAstraLocal: Advance the focused correction independently of installed title evidence, retaining all prior capture findings below. -->
0.1.26 is implemented and independently reviewed. Combo's optional GPU path
uses precise pairwise DP4/DPH additions when the CPU JIT is enabled. Nine
numerical cases and two opposite mixed-depth failure/repair witnesses pass;
48 SPIR-V modules validate, and the depth controls execute as host OpenGL
SPIR-V under both optimizer policies. Ten unflagged/native shader sources
remain byte-identical to 0.1.25. Full probes, native integration, CTest and
existing shader/render gates pass. The source-derived cache identity was
refreshed and verified after CMake configuration. At this earlier checkpoint,
Android/signing/publication and cold Thor comparison were pending; installed
0.1.25 still had both visual faults. The current delivery/device result is above.

<!-- CodexAstraLocal: Advance the diagnostic candidate independently of the unchanged device-qualification result and retain previous checkpoints below. -->
- **0.1.25 implemented and independently reviewed:** default-off, finite Combo
  vertex discovery/capture and private production CPU interpreter/Mesa replay. One manual
  Test phase edge arms at most eight swap intervals, eight packets and a 4 MiB
  aggregate budget. Actual uploads and bound uniforms are immutable; recorded,
  accepted and completed states stay separate. No added GPU wait, worker,
  continuous logger or rendering-policy change. See [release notes](releases/0.1.25.md)
  and the [capture guide](../tools/uberhar/VERTEX_CAPTURE.md).
- **Focused validation passed:** 403 checks of the production Session with modeled
  IO/mapping, 13 real producer artifacts accepted by the independent reader, and
  full indexed-layout reconstruction. Independent replay passed 70 checks across
  13 synthetic CPU interpreter/Mesa cases. These do not execute the Thor scheduler or establish
  a visible defect's cause. The native core/Vulkan/tests/room rebuild and complete probe suite passed. CTest
  passed 61 distinct cases and 1,096 assertions, with five existing firmware-dependent
  skips and zero failures. Policy tests passed 163 checks. All Android, shader,
  signing and publication gates passed in
  [run 37572128020](https://github.com/RegiRex/uberhar/actions/runs/37572128020).
- **0.1.25 / 33974141 installed:** exact release source `9bf0cab4e` and APK
  SHA-256 `0dfd409de77ed6f30f9730a0ccadad784b1884dfe48f49f6e5afeba1191b9850`
  were verified with compatible signing before the data-preserving update.
  The first cold opening produced a valid private discovery artifact: 28 rows
  across four swap intervals, zero capture errors or caps. The capture ID says
  moon, but the actual trigger was the early ghost scene at opening +145 seconds;
  the missed moon cue and partial menu occlusion are explicitly retained.
  A second opening captured the actual moon scene: 33 discovery rows, all
  accepted/completed. A third retained eight selected color packets and executed
  matching-source production CPU interpreter/Mesa replay. A fourth retained eight selected
  ghost-window packets; independent host replay places that family wholly beyond
  the right clip plane in all eight. A fifth retained eight actual depth packets;
  their mesh matches the earlier color selection, but every cross-run pairing
  differs in position uniforms. A sixth retained eight packets from another
  color family with unclipped host geometry, still without actor attribution.
  A seventh selected ghost-window family is partly clipped and unassigned; the
  eighth depth instance is wholly right-clipped in both host routes. No packet
  yet identifies an affected visible actor or establishes Adreno/pixel parity.
  All eight openings used the same APK; selectors do not count as builds.
- **0.1.25 graphics remain incorrect:** independent consecutive-frame review
  reproduces moon flashing and the matched ghost clear/patch/clear sequence.
  The first run's earlier ghost footage is UI-obscured; later openings retain
  unobscured matched ghost sequences with the same fault. The fourth run again
  shows the right book-holder clear, patched and clear after excluding its UI
  transition. The later coordinated moon clips retain segment reversals, and
  separate ghost clips retain matched clear/patch/clear sequences. Eighth-run
  KGSL is 3,120.45 MiB after the ghost clip and 13.35 MiB after verified normal
  return with the process alive. Its initial return input left the exit dialog
  open; only the verified retry/final log establishes returned state. The sixth
  run's earlier mislabeled sample remains explicitly pre-exit.
  These discrete readings do not establish lifetime bounds.
  Sustained normal speed and 2x qualification remain unmet. Every opening resets
  File 1 and clears the title Vulkan cache; captured runs are diagnostic, not
  performance qualification.
- **Captured-family limits:** the moon-window color replay has small numerical
  differences; its tolerance exceedances affect an unused fragment input. A
  same-input host depth-entry counterfactual produces identical depth/color
  positions within each host route and does not reproduce cross-program variance.
  Host CPU replay uses the production interpreter; the device's enabled CPU JIT
  was not replayed. Actual depth replay has zero output tolerance exceedances,
  but distinct cross-run transforms prevent a same-frame depth/color comparison.
  The sixth family's host geometry remains unassigned to a visible actor;
  numerical position differences alone do not identify the faulty stage. The
  seventh/eighth selections likewise supply no affected actor. No title fix or
  speed improvement follows from these captured-family host results.

<!-- CodexAstraLocal: A separately executed x64 JIT witness supports narrow arithmetic work, not an ARM64/Adreno or title-cause claim. -->

- **0.1.26 scoped work authorized:** a separate synthetic host test demonstrates
  a DP4 arithmetic/depth hazard when x64 CPU JIT and legacy generated-GPU routes
  are mixed. Two fixed cases lose all 1,024 color pixels in opposite route
  orders; same-route and precise pairwise controls retain all 1,024. Host OpenGL
  SPIR-V execution confirms both optimizer modes; Vulkan modules were validated
  only. The next candidate scopes precise pairwise DP4/DPH to Combo optional
  GPU shaders with CPU JIT enabled. Production regression/review and local
  integration passed; release gates and cold Thor testing remain pending.
  It is not an established Dark Moon cause
  or cure; actual AArch64/Adreno behavior remains unmeasured by this host test.
- **Cadence:** 0.1.24 is successor one to the completed 0.1.23 audit; gated 0.1.25
  is two, and 0.1.26 will be three after its completed gated build. Reaudit
  before 0.1.27 or earlier if evidence requires.
  The docs-only checkpoint is `f27d7edd6`; installed release source remains
  `9bf0cab4e`. The original report and private
  artifacts remain intact. Only the root coordinator operates the Thor.

## Completed 0.1.24 checkpoint


<!-- CodexAstraLocal: Record the reviewed candidate and completed host gates without promoting synthetic parity or a built host binary into Android/title qualification. -->
- **Candidate 0.1.24 implemented and reviewed:** Combo rejects a complete optional
  GPU vertex draw when a final mapped W component has no possible write in the
  guest program. Exact, bounded program/swizzle memoization contains repeated
  scans; existing CPU execution preserves its register carry. This is a narrow
  proven CPU/GPU difference, not an established Dark Moon fix. Native and Custom
  production paths and shader generation are unchanged.
- **Host gates passed:** production admission/interpreter/output conversion and
  generated GPU regression (old admission fails), independent review, eight manifest
  regressions, complete
  build probes, all shader/TEV/full-fragment parity gates, and native core/Vulkan/
  tests/room build. CTest passed 61 distinct cases and 1,096 assertions; five
  existing audio cases require absent firmware and were skipped. The new GPU
  regression is also included in the reusable shader workflow.
  Android/package/signing/publication remain separate release gates.
- **Audit completed:** the [October 7 architecture/change audit](UBERHAR_AUDIT_2026-10-07.md)
  covers the three successors to 0.1.20 and additions after 0.1.22. Candidate
  0.1.24 is successor one after this audit. Audit again after
  three builds; continue toward the documented 0.2.0 threshold.
- **Device work:** verified 0.1.24 / 33973716 is installed through a compatible
  data-preserving update. Three fresh 0.1.23 full-Combo/Vulkan/2x baselines are
  retained; consecutive frames reproduce moon flashing and intermittent dense
  dark patches on both the foreground ghost and the early wide-view book-holder.
  The book-holder is clear immediately before and after its corrupt sequence.
  The candidate reproduces both faults in matched consecutive frames. Its guard
  checked 773,137 batches with zero missing-W rejections (one memo entry), so the
  narrow synthetic defect did not trigger containment in this title run. Ordinary
  logs report zero skipped draws; normal exit recovers KGSL to about 13.34 MiB.
  The [scoped report](UBERHAR_DEVICE_TEST_2026-10-07.md) measures 21.567% average
  speed over 206.918 seconds, every window below 95%, and a 402.226 ms worst
  interval. Sustained speed remains unqualified; unequal scene/capture mix does
  not establish a build-to-build regression or speedup.
  Every opening resets File 1 and starts with the title's Vulkan cache deleted;
  all tested titles follow the same zero-saved-cache requirement. Warm runs
  cannot qualify the project goal. Only the root coordinator operates the Thor.
- **Publication status:** reviewed source `970805ddc` is published on the authorized
  release branch. [Run 37566500834](https://github.com/RegiRex/uberhar/actions/runs/37566500834)
  passed shader, Android, package, signing and publication gates. The released
  APK SHA-256 is `8c9ada7a510804cffa7a23bef341d761ea80ceb5f0a009d6b873b8e97b0748f6`.
  Asset checksums, exact tag/source, ARM64 payload and matching signing identity
  were verified locally; cryptographic signature validation passed in CI.
  The existing gated fork workflow retains all gates and now correctly classifies
  alpha versions as prereleases and a zero alpha component as a full release.
- **Next implementation:** default-off, finite discovery/capture of selected
  actual GPU draws, with immutable uploaded inputs and bound uniforms, explicit
  recorded/accepted/completed states, strict byte/record limits and offline replay.
  This diagnostic follows the existing handoff after the candidate did not explain
  either visual fault. Raw game payloads stay private; full draw behavior is retained.

## Preserved recovery and earlier checkpoints

<!-- CodexAstraLocal: Older pending-status and timed-batch paragraphs below are historical; the current candidate, every-three-build cadence and cold-cache scope above supersede them. -->

<!-- CodexAstraLocal: Reconcile the resumed checkout and live device/release checks before continuing the interrupted candidate; preserve earlier checkpoints below. -->
- **Recovered:** origin fetched at `814de4d60`; the local branch already matches
  the latest release guidance. Staged/unstaged changes, original report and all
  344 retained private artifacts are preserved, with an external recovery backup
  and SHA-256 inventory. New comments use `CodexAstraLocal`; historic attribution
  and the original device report remain unchanged.
- **Ready:** the explicitly selected AYN Thor is authorized, Android API 33,
  installed Uberhar **0.1.23 / 33972593**. Live GitHub metadata reconfirms shader,
  Android and publication success for run **37551055615** at `99fe4a885`; the
  retained APK matches its published checksum and release asset digest. No update
  or data reset was needed. Setup battery is 80%, AC powered, 27°C, power saver
  off; hardware performance/fan policy and SoC headroom remain unverified.
- **Work underway:** independent architecture/post-0.1.22 audit, output-W guard
  regression/review and retained temporal-evidence analysis run in parallel.
  Only the root coordinator operates the device. Mode 4 already reproduced moon
  flashing; no need to repeat unchanged setup. No new APK has been produced.
- **Current authority:** continue toward 0.2.0 with a full audit every three
  builds, replacing the initial timed batch. Source/release ownership is local;
  the former cloud publication hold is historical. Qualification still requires
  the documented correctness, throughput, memory and retained baseline evidence.

<!-- CodexAstraUlt: New owner scope and completed route-isolation evidence supersede the initial batch boundary and repeat-Native plan below. -->
- **Current loop:** continue toward 0.2.0 until credits, beta qualification or a
  genuine blocker; the initial three-build/four-hour cap is superseded. Audit every
  three builds, with the 0.1.21–0.1.23 audit now due. Test Combo Dark Moon only
  unless the Native process changes, requiring both modes. Alpha versions receive
  prereleases; beta milestones receive full releases after the same gates.
- **New existing-build result:** 0.1.23 mode 4 retains moon flashing in consecutive
  frames with zero optional specialized-fragment requests. Observed ghosts are
  cleaner, with unmatched-phase limits; that is a separate result. Normal exit
  recovers KGSL to about 11 MiB. See the [experiment ledger](UBERHAR_LOCAL_BATCH_2026-10-06.md).
- **Next candidate:** a narrow optional-GPU fallback for a source-proven consumed
  output-W lane that no instruction writes; implementation/review underway.
  No new APK has been built. This does not establish the cause of either title
  symptom and may increase CPU work. Native production behavior remains unchanged.

<!-- CodexAstraUlt: Completed publication and the attached local device report supersede earlier pending status; local Codex now owns the next implementation/test batch. -->
- **Current owner:** [local Codex on Nobara](UBERHAR_LOCAL_AUTONOMOUS_HANDOFF.md)
  takes over the complete build/review/device-test loop. The first batch is up to
  three candidate builds or four hours; cloud source development pauses after this
  handoff. This documentation update starts no APK build.
- **Latest reported device evidence:** the [submitted local report](device_reports/UBERHAR_DEVICE_TEST_2026-10-06.md)
  records completed 0.1.22 Combo and 0.1.23 Combo/Native runs, with 0.1.23 still
  installed. Opening speeds were 19.542%, 22.165% and 25.649%, respectively;
  sampled KGSL maxima were 3,823.86, 3,051.37 and 1,076.11 MiB. All returned
  normally and recovered GPU accounting. These unmatched samples do not establish
  a speedup, bounded gameplay memory or an optimizer-caused reduction.
- **Corrected visual direction:** the owner sees no moon glitch in Native and
  many flashing moon segments in Combo. This supersedes the local report's inference
  about shared facets from selected stills; those stills do not establish a shared
  temporal fault. Keep moon flashing and ghost corruption as separate Combo targets.
  Inspect the retained clips over matching scene phases and try mode 4 before
  choosing the next source experiment. The cloud read the report and the owner's
  correction, not the private raw captures. This correction starts no APK build.

<!-- CodexAstraUlt: Reconcile upstream takeover authority with preserved local observations; the original report is unchanged, while temporal review supersedes the old still-frame interpretation and cloud-publication hold. -->
- **Active local batch:** started October 6 at 21:32:55 EDT (October 7
  01:32:55 UTC), ending by 01:32:55 EDT / 05:32:55 UTC or three new
  candidate builds. Current candidate count: **0**. The existing-build mode-4
  experiment comes first; no new APK is needed for route isolation.
- **Evidence correction verified locally:** consecutive retained Combo frames
  show repeated abrupt moon-segment brightness reversals; reviewed Native
  sequences provide the owner's nonflashing reference. Ghost corruption is a
  separate target. Static shared facets did not test flashing. The original
  local report and ignored evidence are preserved, with a pre-integration backup.
- **Source continuity:** fetched and fast-forwarded to `814de4d60`, restored
  local documentation, and retained both the original report and the cloud's
  distinct archive. Local ownership supersedes the previous publication hold.

## Preserved local checkpoint before autonomous takeover

<!-- CodexAstraUlt: The following checkpoint remains historical evidence; its still-frame inference and unavailable-cloud hold are superseded above and must not direct the active batch. -->
<!-- CodexAstraUlt: Replace the preliminary USB/pending-build checkpoint with verified publication, update installation and bounded device evidence; retain the cloud implementation record below. -->
- **Local Thor testing, October 6:** USB authorization and the installed **0.1.23**
  package are verified. The isolated local `uberhar/thor-device-testing` branch
  tracks release-branch source `7a1b2717db24cf913f3b63852a6b7b7b4acd2b34`;
  the comparison branch is preserved. The 0.1.22 full Combo/Vulkan/2×,
  100%-limit run reached the initial moon and laboratory ghosts and returned
  normally. Own-process KGSL reached about 3,824 MiB during that session and
  returned to about 12 MiB. The 0.1.23 Combo and Native reference runs also
  reached the initial moon and laboratory ghosts and returned normally.
  Their sampled live KGSL peaks were about **3,051 / 1,076 MiB**, falling to
  **12 / 17 MiB** immediately afterward. Native later reached about 11 MiB.
  System available memory recovered. These short, differently timed captures
  establish cleanup, not a controlled speedup or a memory cure.
  Delete only Dark Moon's Vulkan shader cache and in-game **File 1** before
  each run. The owner abandoned save-state reuse after the cross-build mismatch;
  the existing 0.1.22 Slot 1 remains preserved. See the
  [local test record](UBERHAR_DEVICE_TEST_2026-10-06.md).
- **Image and speed limits:** the 0.1.23 Combo wide laboratory sample contains
  dense dark patches on a ghost that were not seen in the inspected Native
  frames. Camera/animation phases differ, so no exact pixel or causal claim is
  made. Moon facets occur in both modes. Selected scene windows averaged
  **22.2% emulation speed in Combo / 25.6% in Native**; Native is a visual
  reference, not a playable-performance standard. No skipped draws or pipeline
  failures were recorded. Full Combo/Vulkan/2×/100% settings are restored and
  the Thor is left at the game list. This covers the initial moon and laboratory
  sequence, not the complete introduction or later moon destruction.
- **Release verified:** [0.1.23 run 37551055615](https://github.com/RegiRex/uberhar/actions/runs/37551055615)
  passed shader, Android, signing/package and publication gates. The release APK
  checksum and installed/candidate signing identity match; `adb install -r`
  succeeded without uninstalling or clearing app data. No emulator changes or
  additional build are needed for this device-testing turn. Local findings await
  cloud-session handoff before any emulator-change publication; no public push
  has been made. The following cloud record predates this local checkpoint.

## 0.1.23 implementation and earlier evidence

<!-- CodexAstraUlt: Replace the pending comparison-only milestone with measured 0.1.22 cleanup and the owner's normal-prerelease authorization; retain prior records below. -->
- **New device evidence:** the supplied 0.1.22 log contains full Combo and Native
  Dark Moon runs. Combo reaches approximately **4,232 MiB** in own-process KGSL
  accounting and falls to **12 MiB** after normal exit; Native stays around
  **1,081 MiB** and returns to **15 MiB**. System available memory recovers.
  This identifies a much larger live Combo GPU allocation footprint in these
  runs, with cleanup on exit; it does not demonstrate a persistent post-exit leak.
  The owner still sees ghost and moon glitches, reports more visible glitches,
  and perceives a significant speed improvement over earlier builds. This is
  useful owner feedback, not a matched speed measurement. See the
  [0.1.22 log analysis](UBERHAR_LOG_ANALYSIS_0.1.22.md).
- **Implemented:** optional background vertex/geometry and specialized fragment
  jobs now optimize, with compiler options frozen before queueing. The cold generic
  path, Custom policy, draw order and existing fallbacks remain. Existing bounded
  reporting identifies the selected policy. This corrects an inappropriate compiler
  setting; it is not a confirmed driver-memory cure or device speedup.
- **Validation passed:** native core/Vulkan/tests/room build; complete host probes;
  62 passing CTest entries and five firmware-dependent skips; eight manifest tests;
  16 simulated device-probe tests. All 48 production queued compiler cases pass;
  the old source fails the policy assertion. Actual SPIR-V execution on Mesa matches
  CPU results for 42 vertex cases and produces identical color/depth/discard for
  75 representative fragment pairs (76,800 pixels). Separate generated Vulkan
  modules pass SPIR-V validation. Independent source/cache review found no blocker.
  No Android source changed; the fresh Android package and full shader gates run in CI.
- **Accounting limits:** a model of **96 MiB per GPU pipeline with color format 0**
  and **72 MiB per GPU pipeline without a color attachment** closely follows the
  measured KGSL staircase, leaving a stable approximately 1,086–1,088 MiB baseline.
  These are fitted pipeline-associated increments, not image size calculations or
  direct observations of individual driver allocation types.
- **Remaining visual investigation:** a separate synthetic guest shader reproduces
  a partial-output difference (CPU alpha 0 versus GPU alpha 1). The current game log
  lacks its shader instructions/component masks, so Dark Moon attribution remains
  open. Preserve within-batch CPU state in any future correction; changing every
  GPU register's default alone would not establish parity.
- **Verified prior build:** 0.1.22
  [run 37524017429](https://github.com/RegiRex/uberhar/actions/runs/37524017429)
  passed Android, signing/package and shader gates; artifact
  `uberhar-0.1.22-arm64` is available. This is the second successful successor to
  the completed 0.1.20 source review. The new device log does not qualify every
  title or a controlled performance comparison; the owner's visual report
  confirms that Dark Moon remains incorrect.
- **Publication:** the owner now authorizes normal repository prereleases and
  will use a side branch for parallel work. The existing
  `uberhar/hybrid-shaders` workflow preserves signing continuity and release
  gates in **RegiRex/uberhar only**, never the upstream Azahar repository. The
  owner confirmed this publication path; literal integration of divergent
  upstream `master` is unnecessary here and deferred for separate review.
  [0.1.23 notes](releases/0.1.23.md) describe the implementation.
  Source `99fe4a885` is published on that release branch and
  [run 37551055615](https://github.com/RegiRex/uberhar/actions/runs/37551055615)
  completed **successfully**, including shader, Android and publication jobs.
  The [normal 0.1.23 prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.23)
  contains the APK, checksum, validation and native-symbol assets. The cloud checked
  this publication independently; the local report also records a compatible install.
- **Device setup:** the [Nobara/Thor readiness guide](../tools/uberhar/device_testing/README.md)
  and [local Codex handoff](../tools/uberhar/device_testing/LOCAL_CODEX_HANDOFF.md)
  are now ordinary repository files. Changing the Thor's **USB controlled by**
  selection is not a prerequisite; check its state with `adb devices -l`.
  Readiness-helper tests pass with fake ADB; the latest local report now also records
  authorized USB access and completed physical-device runs.

## Previous 0.1.22 comparison record

<!-- CodexAstraUlt: New owner-supplied 0.1.21 device evidence supersedes the pending comparison status; retain prior records below. -->
- **Evidence:** the owner supplied one `e72209ba6` full Combo/Vulkan/2x Dark Moon
  run and a 15.747-second recording. System available memory still falls sharply;
  warmed process RSS and tracked Vulkan capacities plateau, and explicit Vulkan
  allocations are released at orderly shutdown. This is persistent system memory
  pressure, not proof of a growing application heap or a crash in this run.
- **Correction implemented:** the 0.1.21 optional GPU path admitted lit draws
  on Android despite geometry correction being disabled and this driver lacking
  fragment barycentrics. The CPU path performs triangle quaternion sign correction;
  that GPU path did not. Added an early accurate CPU fallback and differential
  regression, preserving unlit promotion and inherited Custom admission rules.
- **Independent input fix:** restore `w=1` for emulated three-component vertex
  fetches before guest shader execution; the wider fetch previously forced zero
  instead of matching the CPU loader. Source-derived cache identity invalidates
  old compiled code. Dark Moon's use of this format is not established.
- **Memory follow-up implemented:** two bounded optional own-process KGSL counters
  and system `MemAvailable` join the existing periodic/lifecycle snapshots.
  Missing or denied nodes stay unknown. No extra worker, scan, bundle, GPU wait,
  arbitrary cache eviction or claimed leak cure.
- **Validation passed:** reconfigured native build with fresh source-derived
  shader cache identity; full host probes; 62 passing CTest entries and five
  firmware-dependent skips; Android production compilation and 54 JVM tests;
  eight manifest tests. Quaternion admission/generator checks pass ASan/UBSan.
  Mesa reproduces opposite-hemisphere lighting divergence with matching positive
  controls. All 21 padding cases match CPU/interpreter values on Mesa, and all
  21 emitted vertex modules pass Vulkan SPIR-V compilation/validation. Old admission
  and old W=0 fixtures fail the respective semantic regressions. Independent
  source reviews found no blocker. Full fragment pixel corpus is left to the
  unchanged CI gates; no Adreno or Dark Moon execution occurred in this environment.
- **Build:** 0.1.21 [run 37436727334](https://github.com/RegiRex/uberhar/actions/runs/37436727334)
  passed Android, signing/package and shader gates; its comparison artifact is
  available. 0.1.22 is ready for the existing comparison PR build, without shared
  release publication. Shader and Android/package jobs must both pass before its
  artifact is ready. Device correction and performance remain unverified.

## Previous 0.1.21 comparison record

<!-- CodexAstraUlt: Replace the current milestone for the owner's architecture review; retain prior evidence/build records below. -->
- **Scope:** full source/evidence review of `c75e544f7` against base Azahar
  `9e6f523a5`, followed by a diagnostic/correctness comparison candidate. The
  0.1.17 gameplay and 0.1.18 exit evidence remain the latest device measurements.
- **Selected changes:** correct stale completion reuse in Vulkan resource pools
  and release allocated stream-buffer memory on failed initialization attempts;
  add bounded process/explicit Vulkan memory measurements; expose Combo with
  generic fragments to separate GPU vertex behavior from optional specialization.
- **Architecture:** Native deliberately uses CPU vertices and generic fragments.
  Dark Moon's CPU stage accounts for about 78% of its Native run, but only about
  15% of the much slower Combo run. Large compile jobs do not establish runtime
  bloat. Preserve beneficial CPU caches and focus first on resource ownership and
  route correctness; independent fragment promotion is a later measured change.
- **Validation passed:** native core/Vulkan/tests/room build; complete host probes;
  62 passing CTest entries with five firmware-dependent skips; Android production
  Kotlin/Java/resources compilation and 48 JVM tests; eight manifest checks.
  New ownership, route and concurrent-counter regressions pass optimized and
  ASan/UBSan checks. Old-source fixtures fail the targeted ownership regressions.
  Workflow trust/ref selection and signing-failure scenarios pass focused checks.
  Independent source reviews found no blocker. The full shader pixel corpus was
  not repeated locally for this candidate; PR CI must rerun all existing gates.
  No new image-correctness, memory plateau, performance or device result is claimed.
- **Build blocker confirmed:** [0.1.20 diagnostic run](https://github.com/RegiRex/uberhar/actions/runs/37401999433)
  reports **Comparison signing key unavailable**. No replacement was generated.
  Its shader gate passed; Android compilation did not run. The original 0.1.20
  run passed Android compilation/JVM/shader checks but failed packaging.
- **Recovery:** owner reports the original key is only in GitHub Actions or has
  no known backup. The narrowly restricted same-repository comparison PR workflow
  is implemented and tested. It can read its release-base cache if retained;
  certificate and package gates remain intact. An authenticated GitHub PR-creation
  tool is unavailable here: opening a draft PR from the comparison branch into
  `uberhar/hybrid-shaders` is the remaining external step to start CI. A comparison
  push alone no longer starts an inaccessible-key build. No shared release or
  default-branch change; no 0.1.21 APK is ready.

## Previous 0.1.20 comparison record

<!-- CodexAstraUlt: Replace the current 0.1.19 summary with the owner-authorized device-evidence follow-up; retain its validation record below. -->
- **Branch/base:** `uberhar/codexastra-diag-comparison`, following `c1516360b`
  (0.1.19) and release `27b4ccbd8` (0.1.18). No shared release publication or
  version reservation; identify comparisons by source commit.
- **Evidence:** seven uploaded files, six distinct. All gameplay logs are 0.1.17;
  0.1.18 has an OS exit report and a startup-only log. Android classified both
  reported exits as low memory. Dark Moon Combo loses 8,651 MiB of **system**
  available memory over 330 seconds; allocation ownership remains unknown.
  See [analysis and marker-bounded Sonic results](UBERHAR_LOG_ANALYSIS_0.1.17_0.1.18.md).
- **Implemented:** three additional CPU fallbacks for reproduced GPU input
  differences: short copied strides, default/stream conflicts and register aliases.
  Guarding happens before speculative reads/uploads. Original zero-stride handling,
  Custom mode, all draws and ordering remain. Affected draws can cost more CPU.
- **Logging:** bound two observed valid AppletUtility signatures using four initial
  warnings and powers of two, plus final totals. Unknown or malformed signatures
  retain warnings; IPC responses are unchanged. Keep 0.1.19 optional-progress delivery.
- **Validation:** focused production-input tests pass optimized and ASan/UBSan;
  old source fails the new admission regression. Existing admission/assembly tests
  pass. APT production-body response/census test passes 109,538 checks, including
  replay of 17,981 observed requests. Native build of core, Vulkan renderer, tests
  and room passes; CTest has 62 passing entries and five firmware-dependent skips,
  no failures. Eight manifest tests pass. Independent source/test review found no
  actionable issue. No shader algorithm or Android source changed: the prior local
  pixel/JVM results below remain historical, and CI reruns the full existing gates.
- **Publication:** source `6a98d0049` is pushed and
  [0.1.20 run 37401646321](https://github.com/RegiRex/uberhar/actions/runs/37401646321)
  started. The previous 0.1.19 run completed with successful Android compilation,
  JVM tests and all shader gates, but **failed package/provenance validation** and
  uploaded no validated APK. Public GitHub exposes only exit code 1 for that step;
  authenticated error details are needed to identify the failing subcheck. The
  branch-scoped signing-key cache is a hypothesis, not an established cause.
  Follow-up `1f4708eb8` adds fixed-stage failure annotations and a comparison
  certificate preflight while preserving the final package/signing gates.
  [Run 37401999433](https://github.com/RegiRex/uberhar/actions/runs/37401999433)
  is queued behind the initial 0.1.20 run. Extracted Bash checks passed with
  missing, unreadable, wrong and correct temporary test keys, cleanup and error
  exit-status cases. No APK is ready; no signing-key diagnosis is claimed yet.
- **Limits/next:** the game upload failed; no cutscene or physical Adreno execution
  occurred here. These guards are confirmed correctness fixes, not a demonstrated
  Dark Moon cure. Short route-isolation and process/GPU memory attribution come
  before broader performance work. Existing cloud/Android tooling is usable;
  cloud CPU/software Vulkan cannot mimic Thor Max throughput.

## Previous 0.1.19 comparison record

<!-- CodexAstraUlt: Current follow-up inherits the successful 0.1.18 build; historical local-tool limitations below describe that earlier session. -->
- **Base:** `27b4ccbd8e18fedd630700a31a70311c03d16002`, version 0.1.18.
  Its [gated workflow](https://github.com/RegiRex/uberhar/actions/runs/37382914237)
  completed successfully.
- **Comparison:** `uberhar/codexastra-diag-comparison` keeps this follow-up separate
  from the owner's other development session. Candidate version 0.1.19 does not
  reserve the shared release number. This branch runs all existing package/signing
  and shader gates, uploads artifacts, and skips shared release publication.
- **Current scope:** explicitly optional periodic renderer/vertex/cache progress
  records, nonblocking admission and bounded omission reporting. Reliable totals,
  lifecycle/error context and all renderer decisions are preserved.
- **Build/publication:** source `350675a2a65ec2035e7f5ec7529bac0816e2eea3`
  is pushed. [Initial comparison run 37399196834](https://github.com/RegiRex/uberhar/actions/runs/37399196834)
  failed the host probe compile step: the narrow CI checkout omitted the new
  Libretro test's pinned header dependency. The same failure was reproduced in
  an isolated minimal fixture; fetching that dependency corrects the fixture.
  CI now fetches it and supersedes obsolete comparison runs while preserving the
  main release queue. [Replacement run 37400199457](https://github.com/RegiRex/uberhar/actions/runs/37400199457)
  started from `a08a7fd78878fc7b1bd0f01569deed0ca5b948ca` with that correction.
  It subsequently passed Android compilation/JVM tests and the full shader job,
  then failed package/provenance validation. No validated APK was uploaded.
  Shared release publication is disabled on this branch.
- **Environment:** active development branch, pinned JDK 17/Android SDK/NDK tools,
  host CMake/Ninja, glslang/SPIR-V tools and Mesa offscreen context are installed.
  Complete reusable installation and repeat installation passed.
- **Local checks passed:** headless native build with Vulkan renderer; 61 Catch
  cases and 1,096 assertions, five firmware-dependent skips (CTest includes the
  aggregate and reports 62 passing entries plus five skips); real room join;
  Android production Kotlin/Java compilation and 43 JVM tests with no skips;
  complete host renderer probes; focused queue/backend concurrency, omission,
  storage-fault, Libretro and Linux signal checks; 1,000 completion races;
  eight manifest tests; 1,066 Vulkan modules in each optimizer mode.
- **Pixel checks passed:** 208,896 exact TEV RGBA8 comparisons and texture-use
  checks across 816 programs; 1,081,344 exact color and depth/discard comparisons
  each across 1,056 full-fragment states; 256 compute/native pixel comparisons.
  These use Mesa llvmpipe OpenGL plus Vulkan module validation, not the full
  Android Vulkan renderer or handheld performance measurements.
- **Review:** independent source review completed. It caught and resolved Libretro
  omission reporting and Linux locked-sink fatal-handler behavior before publication.
  Workflow review confirmed comparison isolation and unchanged package/signing gates.
- **Device:** no new Thor Max or Retroid results. This follow-up is not a Dark Moon
  renderer fix or a demonstrated speed increase. Exact secondary Retroid model is
  still unconfirmed.

## Previous 0.1.18 implementation record

<!-- CodexAstraUlt-2: Current implementation, CI and device states must remain distinct. -->

- **Source:** 0.1.18 implementation completed and integration review passed, based on
  reviewed 0.1.17 commit `fe7d1b8a0b395229ca34ab79e14a005cedd4411e`.
- **Build/publication:** prepared for publication through the push-triggered
  [ARM64 workflow](https://github.com/RegiRex/uberhar/actions/workflows/uberhar-alpha.yml)
  workflow; source/build status is available there. It must pass all existing gates
  before an APK is available. This source snapshot does not claim a completed build.
- **Device:** pending owner testing on stock Ayn Thor Max with Vulkan. No new speed,
  image-correctness or crash-resolution result is claimed.
- **Implemented scope:** Combo zero-stride input guard, shader/pipeline terminal
  failure handling, nonblocking flush insertion, and staged interrupted-log recovery.
- **Local validation:** fourteen focused checks passed: shader failure handoff,
  asynchronous completion (1,000 races), log retention (10,066 checks), contended
  queue, wait/frame/activity/cache diagnostics, 164 Android keys, GPU input parity
  (192 layouts and 96 retained CPU vertices), actual file backend faults, and eight
  manifest tests, failure-aware pipeline selection, and seventeen production
  readback/debug-cleanup cases. The input and cleanup regressions fail on pre-fix
  source as expected. The policy check uses production policy/register code with
  only unused logging/serialization interfaces stubbed; full-header CI is separate.
  Targeted sanitizer checks passed with leak detection disabled because the local
  tracing environment does not support LeakSanitizer.
- **Validation limits:** pinned Vulkan/other C++ dependencies, CMake, Kotlin/JUnit,
  glslang and SPIR-V tools are unavailable locally; the dependency download was
  canceled. Full C++/Android compilation, sixteen crash-store Kotlin tests, and the
  complete shader/pixel corpus must pass the unchanged GitHub publication gates.
  Model/extracted-code checks do not prove Adreno execution, output or performance.

See [0.1.18 notes](releases/0.1.18.md), [roadmap](Uberhar_Roadmap.html),
[code map](UBERHAR_CODE_MAP.md), and [review](UBERHAR_ARCHITECTURE_0.1.17.md).

</details>

# Uberhar progress — 0.1.25 capture candidate; local gates passed

<!-- CodexAstraLocal: Advance the diagnostic candidate independently of the unchanged device-qualification result and retain previous checkpoints below. -->
- **0.1.25 implemented and independently reviewed:** default-off, finite Combo
  vertex discovery/capture and private production CPU/Mesa replay. One manual
  Test phase edge arms at most eight swap intervals, eight packets and a 4 MiB
  aggregate budget. Actual uploads and bound uniforms are immutable; recorded,
  accepted and completed states stay separate. No added GPU wait, worker,
  continuous logger or rendering-policy change. See [release notes](releases/0.1.25.md)
  and the [capture guide](../tools/uberhar/VERTEX_CAPTURE.md).
- **Focused validation passed:** 403 checks of the production Session with modeled
  IO/mapping, 13 real producer artifacts accepted by the independent reader, and
  full indexed-layout reconstruction. Independent replay passed 70 checks across
  13 synthetic CPU/Mesa cases. These do not execute the Thor scheduler or establish
  a visible defect's cause. The native core/Vulkan/tests/room rebuild and complete probe suite passed. CTest
  passed 61 distinct cases and 1,096 assertions, with five existing firmware-dependent
  skips and zero failures. Policy tests passed 163 checks. Android/signing/publication
  and device capture remain pending.
- **Current installed result remains 0.1.24:** both consecutive-frame moon flashing
  and matched ghost corruption persist. Sustained normal speed and 2x qualification
  remain unmet. Every opening resets File 1 and clears the title Vulkan cache;
  captured runs are diagnostic, not performance qualification.
- **Cadence:** 0.1.24 is successor one to the completed 0.1.23 audit; gated 0.1.25
  will be two, 0.1.26 three. Reaudit before 0.1.27 or earlier if evidence requires.
  The latest fetched release branch is `43208270c`; original report and private
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

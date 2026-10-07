# Uberhar progress — 0.1.23 Combo shader-memory follow-up

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
  [0.1.23 notes](releases/0.1.23.md) describe the locally validated candidate;
  no 0.1.23 APK is ready until its gates pass.
- **Device setup:** the [Nobara/Thor readiness guide](../tools/uberhar/device_testing/README.md)
  and [local Codex handoff](../tools/uberhar/device_testing/LOCAL_CODEX_HANDOFF.md)
  are now ordinary repository files. Changing the Thor's **USB controlled by**
  selection is not a prerequisite; check its state with `adb devices -l`.
  Readiness-helper tests pass with fake ADB; actual USB access remains unverified.

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

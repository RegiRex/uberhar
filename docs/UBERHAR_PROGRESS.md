# Uberhar progress — 0.1.20 comparison candidate

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

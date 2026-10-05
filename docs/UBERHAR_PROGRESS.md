# Uberhar progress — 0.1.18

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

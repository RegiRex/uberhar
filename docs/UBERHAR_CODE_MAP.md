<!-- AstraEH: Attribution index for the Uberhar implementation, tests, packaging and documentation. -->
# AstraEH code map

**AstraEH** is the assistant attribution name requested for work on Uberhar.
Comments use `AstraEH:` at each logical change section to explain intent and
important constraints. In new Uberhar-only files, the file comment attributes
the whole file; additional section comments explain non-obvious behavior.
Temporary renderer output carries adjacent `AstraEH Log Line` markers;
[the diagnostics map](UBERHAR_DIAGNOSTICS.md) lists caps and removal constraints.
Existing copyright headers and upstream authorship remain intact. Imports and
small declarations belong to the annotated feature section that uses them.

The comparison base is Azahar 2126.1.2:
`9e6f523a57fac9564ac0bf8286db3c3702d301ec`. Git records exact edited lines:

```sh
git diff 9e6f523a57fac9564ac0bf8286db3c3702d301ec HEAD -- src CMakeModules tools .github README.md UBERHAR.md docs
rg -n AstraEH src CMakeModules tools/uberhar .github/workflows README.md UBERHAR.md docs/UBERHAR*.md
```

## Renderer

| Files | AstraEH work |
| --- | --- |
| `src/video_core/shader/generator/glsl_fs_shader_gen.{h,cpp}` | Optional dynamic six-stage TEV generation, support gate, register decoding, shared operation formulas, rounding/scales and delayed buffer writes. 0.0.6 replaces six expanded copies with a loop carrying DontUnroll, lazily reuses per-fragment TEV texture samples and skips unused operands. 0.0.7 adds profile-aware family canonicalization for source-equivalent states without changing transferable FSConfig layout. 0.0.8 adds the shared 108-byte runtime ABI and interprets alpha/scissor/depth mapping, fog, emulated borders and compatible 2D sampling controls; lighting/procedural structure and cube resources stay specialized. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.{h,cpp}` | Mode capture, push-constant layout, bounded family/pipeline caches, dedicated serial fallback compilation with one-pipeline warm-up admission, first-ready selection on the scheduler, per-draw register snapshots, title-switch cleanup and periodic wait/build diagnostics. 0.0.7 uses canonical families and logs a bounded per-title census of candidate fragment/pipeline keys and individual state dimensions, without changing native sampler or fixed-function state. 0.0.8 adds ready CPU-bank preparation/validation, module-based keys, independent bridge control, failure signaling/recovery and tagged bounded diagnostics. |
| `src/common/async_handle.h` | Shared completion signal, acquire/release publication and event-driven wait for either compatible pipeline; moved the existing single-handle primitive here. |
| `src/video_core/renderer_vulkan/vk_graphics_pipeline.{h,cpp}` | Background-only hybrid creation, queue/dependency/driver timing, build phase and aggregate statistics. 0.0.6 restores normal driver optimization and records per-pipeline fallback use/driver duration for utility reports in PipelineCache. 0.0.8 adds process-local host-module identity, active-state execution hashing and explicit failed completion. |
| `src/video_core/renderer_vulkan/vk_shader_disk_cache.{h,cpp}` | Connect new/disk-loaded specializations to completion diagnostics. 0.0.8 keys both runtime and reloaded pipelines by resolved host modules while preserving disk guest IDs; reports cache reuse and foreground VS translation cost. |
| `src/video_core/renderer_vulkan/vk_rasterizer.{h,cpp}` | 0.0.8 makes the ready CPU-bridge decision before draw submission, returns through existing PICA CPU vertex processing, validates/binds the prepared pipeline and times CPU preparation. 0.0.9 exposes the prepared-bridge contract to PICA. Clears the decision after the batch, including empty output. |
| `src/video_core/renderer_vulkan/uberhar_pipeline_policy.h` | Production list/strip/fan input/output-bounded admission with rejection reasons and failure-aware normal/forced first-ready selection, shared with host tests. |
| `src/video_core/pica/primitive_assembly.h`, `pica_core.cpp` and `src/video_core/rasterizer_interface.h` | 0.0.9 adds the explicit prepared-bridge query and isolates assembly for already-acceleratable bridge draws, restoring prior empty/winding state on every exit. Ordinary software assembly is unchanged; inherited accelerated strip/fan continuity limitations remain. |
| `src/video_core/renderer_vulkan/uberhar_wait_diagnostics.h` | Constant-memory histogram and eight worst waits, protected for concurrent reporting. PipelineCache records only actual waits and emits bounded final diagnostics. |
| `src/video_core/renderer_vulkan/vk_instance.cpp` | Read-only capability/feature queries for future GPL/shader-object work; reports advertised versus enabled support without changing device extension selection or driver workarounds. |

## Settings and Android application

| Files | AstraEH work |
| --- | --- |
| `CMakeModules/GenerateSettingKeys.cmake` | Shared keys for hybrid, forced TEV and CPU vertex bridge modes. |
| `src/common/settings.{h,cpp}` | Defaults, settings log entries and per-game override reset. |
| `src/citra_qt/configuration/config.cpp` | Read/write all three experiment flags in desktop configuration. |
| `src/android/app/build.gradle.kts` | ARM64-only build property, separate Uberhar flavor/application ID, numeric version read from `UBERHAR_VERSION`, and JVM-only log-name test dependency. |
| `src/android/app/src/main/jni/{config.cpp,default_ini.h}` | Native setting reads and mandatory default-INI declarations. |
| `src/android/app/src/main/java/org/citra/citra_emu/features/settings/SettingKeys.kt` | JNI declarations matching the generated keys. |
| `src/android/app/src/main/java/org/citra/citra_emu/features/settings/model/BooleanSetting.kt` | Hybrid/forced booleans default off; CPU bridge defaults on and is effective only in normal hybrid mode. |
| `src/android/app/src/main/java/org/citra/citra_emu/features/settings/ui/SettingsFragmentPresenter.kt` | Graphics switches and disabled upstream updater controls for Uberhar. |
| `src/android/app/src/main/java/org/citra/citra_emu/fragments/GamesFragment.kt` | Suppress the upstream update prompt in Uberhar. |
| `src/android/app/src/main/java/org/citra/citra_emu/utils/CitraDirectoryHelper.kt` | Require an empty or previously initialized Uberhar data directory. |
| `src/android/app/src/main/res/values/strings.xml` | Experiment descriptions, data-folder messages and log export labels. |
| `src/android/app/src/uberhar/res/values/strings.xml` | Launcher label and flavor-specific folder guidance. |
| `src/android/app/src/main/java/org/citra/citra_emu/fragments/{HomeSettingsFragment,LogExportDialogFragment}.kt` | Explicit current/previous session picker, filename styles/preview, Android file creation and sharing, background IO and saved picker state. |
| `src/android/app/src/main/java/org/citra/citra_emu/utils/{LogExportNames,LogExporter}.kt` | Session/title parsing, acronym and prefix filenames, legacy-log fallback, private snapshots, scoped file provider and destination copying. |
| `src/android/app/src/main/java/org/citra/citra_emu/utils/{Log,DirectoryInitialization}.kt` and `src/android/app/src/main/java/org/citra/citra_emu/fragments/EmulationFragment.kt` | Remove stale launch flag; add flush JNI declaration, session date and game-title records. |
| `src/common/logging/{backend.cpp,backend.h,log_entry.h}` and `src/android/app/src/main/jni/native_log.cpp` | Queue an export flush barrier, acknowledge it on the log worker and avoid replaying it during shutdown. |
| `src/android/app/src/main/{AndroidManifest.xml,res/xml/log_export_paths.xml}` | Private, flavor-specific provider exposing only staged log copies through explicit URI grants; no new permissions. |

## Validation and build automation

All files in `tools/uberhar/` are new AstraEH work.

| Files | Purpose and limit |
| --- | --- |
| `tools/uberhar/test_async_completion.cpp` | Production completion tests covering both winners, already-completed handles, delayed completion, unrelated notifications, 1,000 publication races and standalone waits. |
| `tools/uberhar/build_probe.sh` | Compile the production generator as small host executables and run family-key regressions before emitting shader cases. |
| `tools/uberhar/test_tev_family.cpp` | Source equivalence, non-mutation and idempotence across device profiles and fog/lighting/blending states; runtime border/alpha/fog/coordinate packing and sharing; retained active logic and typed resource distinctions. |
| `tools/uberhar/shader_probe.cpp` | 224 reproducible TEV cases, directed edge/texture-reuse/unused-operand cases, AddSigned exclusion and 64 full fragment modules. 0.0.7 emits canonical families and checks their source against the original family. |
| `tools/uberhar/compare_tev.py` | Compare generated specialized/interpreted combiner math on Mesa and independently verify texture fetch counts; synthetic sampling inputs do not test real texture derivatives or device drivers. |
| `tools/uberhar/validate_shaders.py` | Compile and validate all 64 full modules with frontend optimization off/on; verify the TEV loop's DontUnroll hint and all four fallback state offsets reach SPIR-V. |
| `tools/uberhar/fragment_state_probe.cpp` and `compare_fragment_state.py` | 192 full specialized/generic fragment comparisons, production state/uniform transport, real textured offscreen color/depth/discard checks on Mesa; GL resource-declaration adaptation is not Vulkan-driver validation. |
| `tools/uberhar/test_pipeline_keys.cpp` | 36 production execution-key checks covering equivalent host modules/inactive fields and active state that must remain distinct. |
| `tools/uberhar/test_pipeline_policy.cpp` | Production CPU admission bounds and normal/forced selection under successful, pending and failed fallback completions. |
| `tools/uberhar/test_bridge_assembly.cpp` | 264 comparisons using the actual PICA assembler against independent topology sequences, plus persistent ordinary batches, winding restoration and exceptional exit. |
| `tools/uberhar/test_wait_diagnostics.cpp` | Histogram boundaries, late worst events beyond the original detail cap and concurrent bounded retention. |
| `tools/uberhar/check_android_keys.py` | Catch missing default-INI keys that would abort Android startup. |
| `tools/uberhar/validate_apk.py` | Find AGP's actual APK, reject ambiguity, verify ARM64 ELF headers, native dependencies and ZIP integrity, and emit a versioned APK and checksum. |
| `tools/uberhar/validate_manifest.py` | Reject known install blockers and identity/authority/permission conflicts in the final decoded manifest. |
| `tools/uberhar/test_manifest.py` | Regression cases for test-only/debug/split flags, version mismatch, shared identity, provider collisions, new permissions and required external Java libraries. |
| `src/android/app/src/test/java/org/citra/citra_emu/utils/LogExportNamesTest.kt` | Ten production-parser JVM cases: owner examples, dates/offsets, game order/deduplication, older paths, short/numeric titles and portable bounded filenames. |
| `tools/uberhar/development-certificate.sha256` | Public certificate fingerprint pinned by AstraEH so a lost signing cache cannot silently produce incompatible updates. |
| `UBERHAR_VERSION` | Owner-requested release.beta.alpha version shared by Gradle and the release pipeline; this plain data file intentionally has no inline comment. |
| `.github/workflows/uberhar-alpha.yml` | Build/sign the isolated app, reject test-only packaging, verify manifest/native/alignment/signing metadata, and publish versioned GitHub pre-releases from a separate job. |
| `.github/workflows/uberhar-baseline.yml` | Build pinned unmodified upstream; allow its known extra x86 validation library without allowing an x86 emulator library. |
| `.github/workflows/uberhar-shaders.yml` | Compile and validate shader modules and pipeline policies, then run TEV and full-fragment differential comparisons using pinned test dependencies. |

The inherited workflows were moved unchanged from `.github/workflows/` to
`.github/upstream-workflows/` to prevent unrelated jobs from running on this
development branch. Their contents are upstream code, not AstraEH implementation.

## Virtual PICA experiment (0.0.10)

<!-- AstraEH: Attribution for the new profile and compute prototype implementation. -->

| File or section | AstraEH work |
| --- | --- |
| `src/common/uberhar_test_profile.h` | Temporary effective profile settings; custom values remain in Android's saved model. |
| `src/common/settings.h`, `settings.cpp`, `CMakeModules/GenerateSettingKeys.cmake` | One mode enum/key, global reset, session identification. |
| Android `UberharTestMode.kt`, `UberharGraphicsProfile.kt` | Mutually exclusive switch model and read-only effective values. |
| Android `SettingsFragmentPresenter.kt`, `SettingsAdapter.kt` | Top switches, persistence delegation, sibling refresh, lower-option locking. |
| Android `IntSetting.kt`, `SettingKeys.kt`, `config.cpp`, `default_ini.h`, `strings.xml` | Mode persistence, restart requirement, native overrides and truthful prototype descriptions. |
| `vk_pipeline_cache.*`, `vk_shader_disk_cache.*` | Primary generic mode, no speculative specialization for covered draws, explicit accurate recovery, measured foreground waits and preserved custom cache records. |
| `uberhar_compute_rect.h` | Exact rectangle/state admission, bounded constant combiner interpretation and area-bucket routing policy. |
| `uberhar_compute_rect_shader.h`, `vk_compute_rect.*` | Real compute pixel writes, startup compilation, fenced resources/barriers, bounded asynchronous GPU sampling and diagnostics. |
| `vk_rasterizer.*`, `src/video_core/CMakeLists.txt` | Actual route integration, framebuffer invalidation ownership, preparation and shutdown ordering. |
| `pica_core.*` | Interpreted vertex-stage time/input summaries for profile comparisons. |
| `test_compute_rect.cpp`, `compare_compute_rect.py` | Admission/rejection, measured route selection and native-vs-compute pixel comparisons. |
| `test_graphics_profile.cpp`, Android `UberharTestModeTest.kt` | Native setting contracts, exclusive transitions and restart rules. |
| `build_probe.sh`, `uberhar-shaders.yml` | Mandatory new profile, compute and Vulkan/SPIR-V gates alongside existing checks. |

## Vertex cost and cache reuse (0.0.11)

<!-- AstraEH: New implementation attribution; inherited CPU JIT itself is not new work. -->

| File or section | AstraEH work |
| --- | --- |
| `common/uberhar_test_profile.h`, Android `UberharGraphicsProfile.kt`, `strings.xml` | Cached CPU JIT selection, optional optimizer disabled, truthful descriptions. |
| `pica/uberhar_vertex_cache.h`, `pica_core.*` | Fixed lookup preserving 64-slot FIFO, actual invocation/reuse counts and bounded stage windows. |
| `shader/uberhar_interpreter_stack.h`, `shader_interpreter.cpp` | Allocation-free control stacks retaining circular overflow behavior. |
| `shader/shader.h`, `shader_jit.*` | Actual engine name and bounded first-use CPU JIT timing; the underlying JIT is inherited. |
| `uberhar_spirv_cache.h`, `vk_pipeline_cache.*` | Bounded generic module envelope, fingerprints/checksum, worker-side disk reuse and recovery diagnostics. |
| `uberhar_compute_rect.h`, `vk_compute_rect.*`, `vk_rasterizer.cpp` | Non-exclusive reasons for compute-state rejection; unchanged rendering admission. |
| `vk_shader_disk_cache.*` | Startup/live object origins and known-record misses. |
| `test_vertex_runtime.cpp`, `test_vertex_interpreter.cpp`, `test_spirv_cache.cpp` | Reference FIFO/stack equivalence, real control-flow/allocation checks and cache corruption/boundary tests. |
| `test_graphics_profile.cpp`, `test_compute_rect.cpp`, `build_probe.sh`, `uberhar-shaders.yml`, `video_core/CMakeLists.txt` | Updated contracts and mandatory release gates. |


## Documentation

`README.md` has an AstraEH branch overview above the upstream README.
`AGENTS.md` records owner preferences, including the review cadence and CI handoff.
`docs/UBERHAR_REVIEW_CADENCE.md` tracks the covered version and next review window.
`docs/UBERHAR_ARCHITECTURE_2026-09-24.md` contains the 0.0.6 source/evidence audit,
architectural alternatives, target execution model and ordered validation gates.
`docs/UBERHAR_LOG_ANALYSIS_0.0.7.md` compares the supplied cold/warm captures and
explains the 0.0.8 response. `docs/UBERHAR_LOG_ANALYSIS_0.0.8.md` records the
zero-bridge result, driver evidence and 0.0.9 coverage correction. `docs/UBERHAR_DIAGNOSTICS.md` maps feature counters,
frequency/count limits and diagnostic removal markers.
`UBERHAR.md` describes scope, limits and device testing. `docs/releases/` holds
versioned pre-release notes. `docs/UBERHAR_LOG_ANALYSIS_0.0.5.md` records the complete
0.0.5 cold/warm analysis, preprocessing limits and the compact-shader response.
`docs/UBERHAR_LOG_ANALYSIS_2026-09-24.md` records
the device evidence and new counter definitions without uploading raw logs. This map and
`docs/UBERHAR_DISPLAY_SYNC.md` are AstraEH documents. The display document is a
follow-up investigation plan; alpha 1 contains no screen synchronization or
model-sharpening changes.

`docs/UBERHAR_LOG_ANALYSIS_0.0.9.md` separates the owner's four sessions.
`docs/UBERHAR_ARCHITECTURE_0.0.9.md` is the early review and records implemented
scope, unresolved limits, test contracts and the next engineering order.

`docs/UBERHAR_LOG_ANALYSIS_0.0.10.md` maps all eight sessions and the 0.0.11 response.

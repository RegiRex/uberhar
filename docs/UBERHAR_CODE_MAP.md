<!-- AstraEH: Attribution index for the Uberhar implementation, tests, packaging and documentation. -->
# Uberhar code map

<!-- CodexAstraUlt: Map optional progress delivery without relabeling earlier work. -->
## 0.1.19 bounded progress logging

New sections use **CodexAstraUlt**. Logging API/backend and delivery-policy tests
make explicitly selected periodic records optional under pressure, aggregate
omissions, and retain reliable lifecycle/error/totals records.
`PicaCore::ReportVirtualVertices`, `PipelineCache::ReportUberharStats`,
`PipelineCache::ReportTevPushStats` and `ShaderDiskCache::ReportUberharStats` select
optional delivery only for progress snapshots. Renderer decisions are unchanged.
`BackendAccessGuard` serializes queued and synchronous sink writes. Linux faults
inside a guarded sink operation terminate directly because waiting for the custom
backtrace helper could deadlock; Libretro omission reporting uses its callback
availability. Extracted production probes cover both platform-specific branches.
The comparison branch runs the existing build gates and retains artifacts while
the shared release branch alone may publish a prerelease.
See [notes](releases/0.1.19.md) and [progress](UBERHAR_PROGRESS.md).

Earlier attribution rules below apply to the historical work they describe.

<!-- CodexAstraUlt-2: Current owner-selected attribution for the 0.1.18 work; historical entries follow. -->
New 0.1.18 sections use **CodexAstraUlt-2**. Search with
`rg -n 'CodexAstraUlt-2|AstraEH|AstraPro' src tools/uberhar docs`.
Older attribution statements below describe their original development periods.

## 0.1.18 input correctness and failure recovery

<!-- CodexAstraUlt-2: Map production changes, their purpose and independent validation. -->

| Files | Purpose and limits |
| --- | --- |
| `renderer_vulkan/uberhar_gpu_vertex_policy.h`, `vk_rasterizer.cpp/.h` | Reject active zero-stride loaders before optional GPU work; preserve CPU draw; first-four incidence records per renderer. Ordinary hardware mode and all other admission rules remain intact. |
| `video_core/shader_build_failure.h`, Vulkan shader/pipeline cache and policy files | Publish null/throwing compiler outcomes as failed completion; never select unusable shader/pipeline handles; retain an accurate surviving alternative. |
| `vk_scheduler.cpp/.h`, `renderer_vulkan.cpp`, `vk_present_window.cpp`, `vk_texture_runtime.cpp` | Cancel a terminal failed command stream, wake producer/presentation waits, distinguish submitted from canceled ticks and make teardown safe. No new normal-path foreground shader wait. |
| Android JNI `native.cpp` | Install cleanup before cache loading, clear startup/running state on exit and handle the typed loading failure as the existing terminal renderer outcome. |
| `common/bounded_threadsafe_queue.h`, `logging/backend.cpp` | Nonblocking flush insertion across writer/notification locks; ordinary producer FIFO and waiting remain. |
| Android `utils/CrashLogStore.kt`, `CrashSessionLogs.kt` | Stage incident text before rotation; large copying and legacy migration use the existing worker; provider failures preserve sources and pending recovery. |
| `test_gpu_input_parity.py`, `test_shader_failure.cpp`, `test_download_recovery.py`, `test_pipeline_policy.cpp`, `test_log_queue.cpp`, Android `CrashLogStoreTest.kt` | Real input preparation/routing, compiler failure and canceled submission, exception-safe readback/debug cleanup, full/contended queue, and staged incident retry regressions. Host tests do not establish Adreno output or speed. |
| `UBERHAR_VERSION`, release/progress/review/roadmap docs, `AGENTS.md` | Owner-approved version and attribution, acceptance gates and implementation order; preserve earlier history. |

Renderer paths above are relative to `src/video_core/renderer_vulkan/` unless
otherwise specified. No APK is considered ready before all publication gates pass.

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

<!-- AstraEH: Optional Android evidence supplements the lean 0.1.16 logger; no crash UI. -->
### File-only optional Android evidence (0.1.17)

| Files | Purpose and limits |
| --- | --- |
| Android `utils/AndroidCrashEvidence.kt` | One API-30+ startup check: tag current build/process, inspect at most eight exits, capture at most two previously unrecorded abnormal tagged exits. API-31+ native tombstones retain original protobuf bytes; no custom signal handler or per-frame work. Exceptions/missing services do not gate gameplay. |
| `utils/AndroidCrashReportStore.kt` | Atomic private staging of dated summary/optional trace; 8 MiB trace cap, 4,096-character OS description cap, 32-entry duplicate ledger. Untagged records are not assigned a guessed build. Missing/oversized traces still yield an exit summary. |
| `utils/CrashReportTransfer.kt`, `CrashReportFiles.kt` | Copy staged evidence into existing `log/crashes/` using temporary files. Verify SHA-256 before deleting private staging; differing existing files are never overwritten. Failed transfers remain recoverable; one bad destination does not block other reports. |
| `CrashSessionLogs.kt`, `DirectoryInitialization.kt` | Read the capture switch after config load, then run one short-lived worker. Base text preservation/file publication works with optional capture disabled/unavailable. Main log receives a bounded file-location pointer or pending-save record, with no crash-report popup. |
| `LogExporter.kt`, `LogExportDialogFragment.kt`, `strings.xml` | Remove crash-report listing/actions. Main picker exports only current/previous/older text. Older staged incident files migrate into the same filesystem-only crash folder. |
| `GenerateSettingKeys.cmake`, `settings.h`, Android config/default INI/JNI/setting bindings | `android_crash_reports` defaults true, may be disabled in `[Debugging]` in the config file; no new settings-screen item. Normal logger remains independent. |
| `AndroidCrashReportStoreTest.kt`, `CrashReportTransferTest.kt` | Production-policy tests for identity attribution, exact binary bytes, missing/oversized traces, interrupted commit/retry, bounded deduplication and transfer failures/conflicts. Combined with existing retention/export tests: 33 JVM cases. Android API/provider/device coverage remains separate. |


<!-- AstraEH: Current logging design supersedes historical 0.1.14/0.1.15 entries below. -->
### Lean logging and portability (0.1.16)

| Files | Purpose and limits |
| --- | --- |
| Android `utils/CrashLogStore.kt`, `CrashSessionLogs.kt` | One synced marker records active/idle state and bounded Java/handled failures. On next launch, copy the existing current text log only for incidents, commit before rotation, keep originals on failed recovery. No `ApplicationExitInfo`, process-history scan, new binary traces, session directories or scheduled Java flush worker. |
| `LogExporter.kt`, `LogExportDialogFragment.kt`, `strings.xml` | Existing three-log main picker; plain incident files behind Crash reports. Previously collected binary traces remain individually exportable after migration. No ZIP creation. Only retained evidence is explicitly deletable. |
| `src/common/logging/backend.{h,cpp}`, `src/common/bounded_threadsafe_queue.h` | Sole rotating text backend; native worker flushes a quiet tail after at most one idle second when scheduled. Dirty flag avoids file operations on empty wakeups. Failed archive recovery suppresses rotation and preserves append mode. Existing queue, provider and 100 MiB writer limitations remain. |
| Android JNI `native.cpp`, `native_log.cpp`, `NativeLibrary.kt`, `Log.kt`, `DirectoryInitialization.kt` | Pass a rotation decision instead of a second log path; primary-writer health checked at startup, run end and export. No descriptor handoff or duplicated live logger. |
| `CrashLogStoreTest.kt`, `test_session_backend.py`, `test_log_queue.cpp`, `test_log_retention.cpp` | Real-file restart/fault/migration tests, existing export-name tests, actual production worker quiet-tail flush/barrier teardown, SIGKILL, rotation and storage faults. Removed obsolete duplicate-journal path test. Full Android/provider and GammaOS checks remain device gates. |

The owner requires basic Android compatibility without depending on optional OS/vendor
services. No claim of an Android-free frontend, tested GammaOS performance or guaranteed
native fatal stacks. Existing health diagnostics stay optional, unknown-tolerant and
capped at one sample per 30 seconds. Historical entries below document superseded designs.


<!-- AstraEH: 0.1.15 repairs the Android-specific path defect missed by prior host adapters. -->
### Android logging correction (0.1.15)

- `src/android/app/src/main/jni/native.cpp`: open the frontend-created private log
  directly, hand off through `fd://`, then close the original descriptor after native
  duplication. Bypasses both game-data-relative raw translation and SAF traversal.
- Android `LogExporter.kt` and `LogExportDialogFragment.kt`: ordinary picker only
  returns current/previous/older text. Separate Crash reports view filters out ordinary
  launches and empty records; keeps useful exit-only incidents from the broken release.
- `CrashSessionStore.kt` / `CrashSessionLogs.kt`: reclaim empty idle records without
  incident evidence and retain two OS-confirmed clean idle backups. Never age out
  crash/error/active/unresolved evidence. A single process warning logs both flush
  completion and private-writer health if either fails.
- `tools/uberhar/test_android_session_path.py`: compiles actual JNI setup and Android
  `IOFile::Open`/`TranslateFilePath`, reproduces old relocation, tests descriptor
  ownership, append, raw/provider dispatch and missing-file handling. Added to the
  host gate. JVM tests cover incident filtering and conservative cleanup alongside
  previous log export/retention cases. Android device acceptance remains separate.

<!-- AstraEH: Accepted 0.1.13 review selects evidence retention before broader experiments. -->
### Reliability and crash evidence (0.1.14)

| Files | Purpose and limits |
| --- | --- |
| `utils/CrashSessionStore.kt`, `utils/CrashSessionLogs.kt` under the Android app package | Timestamp/UUID private session folders, synced lifecycle metadata, Android exit matching, optional raw trace capture and manual-only retention. One background flush per second; no native signal handler or promise of final queued bytes. |
| `utils/LogExporter.kt`, `fragments/LogExportDialogFragment.kt` | ZIP snapshots, sizes, explicit saved-session deletion, current/old/older text exports. Saved evidence remains accessible after a failed current flush. |
| `src/common/logging/backend.{h,cpp}`, Android `DirectoryInitialization.kt`, `Log.kt`, `jni/native_log.cpp` | Independent native session writer before provider writes; append without rotation, health reporting, existing 100 MiB cap. Adds disk writes whose device cost is unmeasured. |
| Android `EmulationFragment.kt`, `NativeLibrary.kt`, `jni/native.cpp`, `src/core/core.h`, `src/video_core/shader_recovery_error.h` | Run markers and explicit terminal shader-recovery status. Catch only the typed missing-recovery-shader error around RunLoop; stop through existing teardown, never skip the draw and resume. Other native crashes stay fatal. |
| `vk_pipeline_cache.{h,cpp}` | Capture generic job profile/options/title/path before queuing; drain generic module borrowers before repeated cache replacement in Native/Compute as well as Combo. No claim this resolves Dark Moon's visible errors. |
| `CrashSessionStoreTest.kt`, `tools/uberhar/test_session_backend.py`, `build_probe.sh` | Production policy tests for retention, PID reuse, failure classification, ZIP bytes, damaged metadata and deletion guards; native file backend exercised across rotation and SIGKILL, with write/flush/cap failures. Host tests do not replace Android crash testing. |
| Android `build.gradle.kts`, `.github/workflows/uberhar-alpha.yml` | Preserve release symbol tables and Java mapping alongside exact-build artifacts. Existing shader, package, signature and publication gates remain required. |

<!-- AstraEH: 0.0.12 run/health evidence preserves the 0.0.11 rendering behavior. -->
### Run diagnostics (0.0.12)

| Files | AstraEH work |
| --- | --- |
| `src/core/uberhar_frame_diagnostics.h` | Fixed-size interval/work/speed counters, fast-forward ranges, pause/discontinuity boundaries and eight worst-frame retention. |
| `src/core/perf_stats.{h,cpp}`, `src/video_core/renderer_base.cpp`, `src/core/CMakeLists.txt` | Overlay-independent frame sampling, run IDs, bounded logs and shutdown summaries. |
| `src/core/core.cpp`, `src/android/app/src/main/jni/native.cpp` | Actual frontend/modal waits and menu savestate boundaries; no change to rendering or pause decisions. |
| `src/android/app/src/main/java/org/citra/citra_emu/utils/UberharDeviceDiagnostics.kt`, `fragments/EmulationFragment.kt` in the same package tree | Background, lifecycle-scoped Android health sampling, unknown sensor handling and a 30-second cap. |
| `tools/uberhar/test_frame_diagnostics.cpp`, `tools/uberhar/build_probe.sh` | Production-counter tests for pacing, speed, pause/state boundaries, histograms and late worst hitches. |
| `.github/workflows/uberhar-alpha.yml` | Queue new alphas without cancelling an already running validation/build. |

### Rendering implementation

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
| `tools/uberhar/validate_shaders.py` | Compile and validate 64 baseline modules plus unique full lighting-corpus modules with frontend optimization off/on; verify the TEV loop's DontUnroll hint and all seven fallback state offsets reach SPIR-V. |
| `tools/uberhar/fragment_state_probe.cpp` and `compare_fragment_state.py` | 1056 full specialized/generic fragment comparisons, production state/uniform transport, real textured offscreen color/depth/discard checks on Mesa; GL resource-declaration adaptation is not Vulkan-driver validation. |
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

## Runtime lighting controls (0.0.13)

<!-- AstraEH: Attribution of the runtime-data expansion and its regression gates. -->

| File or section | AstraEH work |
| --- | --- |
| `glsl_fs_shader_gen.{h,cpp}` | Private ABI v3 (120 bytes), exact LUT control packing/recovery checks, family normalization, runtime LUT evaluation and physical-light/attenuation selection. Structural lighting stays specialized. |
| `vk_pipeline_cache.{h,cpp}` | Schema 10, same-run previous/current family census and lighting/procedural structural counts; existing bounded reporting cadence. |
| `test_tev_family.cpp` | Runtime lighting aliases, retained structural distinctions, exact packed controls and unsupported-input recovery. |
| `fragment_state_probe.cpp`, `compare_fragment_state.py` | 224 additional lighting cases, distinct physical-light uniforms and 24 LUT tables; complete specialized/generic color/depth agreement. |
| `shader_probe.cpp`, `compare_tev.py`, `validate_shaders.py` | ABI-aware transport tests; all unique full lighting-corpus shaders join the dual-optimizer Vulkan validation gate. |
| `.github/workflows/uberhar-shaders.yml` | Extended Vulkan corpus and 40-minute gate limit; publication still depends on success. |
| `docs/UBERHAR_ARCHITECTURE_0.0.12.md`, review ledger, release notes | Evidence-based architectural review, device acceptance criteria and next review after 0.0.16. |


## Runtime lighting enables/configurations (0.0.14)

<!-- AstraEH: Attribute this alpha's implementation and all new diagnostics. -->

| File or section | AstraEH work |
| --- | --- |
| `glsl_fs_shader_gen.{h,cpp}` | ABI v4 in the same 120 bytes: effective LUT enables, private zero-index CP encoding, enable/configuration family normalization, uniform disabled defaults, explicit diagnostic-only 0.0.13 key policy. Retains light counts and per-light structure. |
| `vk_pipeline_cache.{h,cpp}` | Renderer schema 11, generator-owned ABI metadata, same-state 0.0.13 key count, first 32 materialized-family structural records on serial worker; title reset after drain. |
| `src/core/perf_stats.cpp` | Separate `frame_diagnostics=1` label; unchanged frame accounting. |
| `test_tev_family.cpp` | Independent support matrix across eight configurations and all 128 enable masks (including spotlight dummy), current/0.0.13 key distinctions and exact CP transport. |
| `fragment_state_probe.cpp`, `compare_fragment_state.py`, `validate_shaders.py` | 512 additional full fragment cases, all 64 real enable masks crossed with eight configurations, stronger unsaturated reflection terms, final-light alpha/shadow/CP coverage; all 928 cases join existing pixel and dual-optimizer Vulkan gates. |
| `README.md`, `UBERHAR.md`, `UBERHAR_VERSION`, diagnostics and release notes | 0.0.14 scope, validation, metadata meanings and Native/2x acceptance instructions; 0.0.13 remains the measured baseline. |


## Compact lighting and speed bands (0.0.15)

<!-- AstraEH: Changes addressing 0.0.14's larger per-family driver cost. -->

| File or section | AstraEH work |
| --- | --- |
| `glsl_fs_shader_gen.{h,cpp}` | ABI v5 (128 bytes), eight packed seven-bit operation slots plus runtime light count; canonical loop key and diagnostic-only Alpha14 key. One ordered DontUnroll lighting body preserves source mapping, optional operations, final-slot Fresnel and shadow rules. Global bump/shadow structure and typed resources stay specialized. |
| `vk_pipeline_cache.{h,cpp}` | Renderer schema 12, same-observation Alpha14 census, bounded light-count mask, current static-family detail schema; exact generator-owned push size and ABI. |
| `src/core/uberhar_frame_diagnostics.h`, `perf_stats.cpp` | Frame schema 2; fixed normal/fast/uncapped lifetime buckets, transition interval exclusion from buckets only, up to three shutdown summaries, unchanged pause/overall accounting. |
| `test_tev_family.cpp`, `fragment_state_probe.cpp` | All slot/control bits, zero through eight lights, independent old-key distinction, retained bump/global-shadow distinctions; 128 additional full-render cases with operation permutations and ordered/remapped/duplicate lights. |
| `compare_fragment_state.py`, `shader_probe.cpp`, `validate_shaders.py` | 128-byte transport, 1056 full-render cases and nine SPIR-V offset checks; lit generic modules require both loop no-unroll hints with each optimizer mode. |
| `test_frame_diagnostics.cpp` | Mixed cap transitions, achieved speed versus 400% request, uncapped throughput, reset persistence and explicit sleep exclusion. |
| `docs/UBERHAR_LOG_ANALYSIS_0.0.14.md`, release notes, overview/status docs, version | Measured regression and successful warm result, compact-loop scope, beta acceptance and 400%-cap device test plan. |


## Native beta vertex transport (0.1.0)

<!-- AstraEH: This section identifies each beta implementation and review artifact. -->

| File or section | AstraEH contribution |
| --- | --- |
| `pica/uberhar_vertex_output.h` | Prepared input mapping and composed output-mask/semantic mapping; exact defaults/last-write/color clamp; 96-byte final-vertex FIFO; sampled/unsampled batch variants. |
| `pica/pica_core.*` | Experimental no-GS/debugger-free admission, incomplete-map recovery, same persistent assembler, fixed route counts and sparse input/execution/output/submission samples. |
| `video_core/CMakeLists.txt` | Register the new header with the existing core target. |
| `tools/uberhar/test_vertex_output.cpp`, `build_probe.sh` | All output masks/banks versus inherited production conversion; FIFO/assembly continuity and sample parity gate. |
| `UBERHAR_VERSION`, Android `uberhar/res/values/strings.xml`, `uberhar-alpha.yml` | 0.1.0 beta name and matching package-label check; original package/signature gates retained. |
| `UBERHAR_LOG_ANALYSIS_0.0.15.md`, `UBERHAR_ARCHITECTURE_0.1.0.md`, review ledger | Four-run evidence, complete architecture/alternatives review, scoped beta acceptance, next review window. |
| `UBERHAR_DIAGNOSTICS.md`, `README.md`, `UBERHAR.md`, `releases/0.1.0.md` | Sample meanings/bounds, current status, owner goals and repeatable test instructions. |

The additional native-vertex diagnostic call is tagged `AstraEH Log Line` and
shares the existing five-second/shutdown cadence. Sampling reads clocks for one
rotating vertex per 128 admitted batches, with an 8192-sample lifetime cap. No new
fragment shader behavior, general compute coverage or display synchronization is
claimed by this CPU transport optimization.


## Sustained diagnostics and compatibility research (0.1.1)

<!-- AstraEH: Small follow-up iteration after broader game coverage; no rendering algorithm change. -->

| File or section | AstraEH contribution |
| --- | --- |
| `pica/uberhar_vertex_output.h`, `pica_core.*` | Time-based sparse sample admission using existing draw timestamps; sampled batch setup/vertex/DrawTriangles fields; schema 2 and complete-sample reporting. |
| `tools/uberhar/test_vertex_output.cpp` | Empty/dense draws, period boundary, pause/no-catch-up and more than 8192 samples over a simulated long session. |
| `core/core.cpp` | One tagged startup machine-context log separates model, title requests, kernel 804 metadata and CPU-clock setting. |
| `core/hle/service/frd/frd.*` | Exact existing IPC response retained; first-four/power-of-two warning summaries and a tagged final count, outside guest serialization. |
| `UBERHAR_LOG_ANALYSIS_0.1.0.md`, `UBERHAR_COMPATIBILITY_PROFILES.md` | Six-session analysis, qualified primary-source research, and post-1.0 compatibility-rule roadmap; no speculative Sonic override. |
| `README.md`, `UBERHAR.md`, `UBERHAR_VERSION`, diagnostics/review ledger, `releases/0.1.1.md` | Measured beta status, diagnostic scope and controlled next-test instructions. |

All added/modified diagnostic calls retain adjacent `AstraEH Log Line` markers.
Sustained sampling uses fixed memory and one admission per 50 ms, rather than a
per-title quota that could run out before the most useful scenes.


## 0.1.2: prepared TEV stages and loading context

<!-- AstraEH: Current additions are mapped separately from inherited emulation code. -->

| Files | AstraEH purpose |
| --- | --- |
| `src/common/uberhar_activity.h` | Fixed atomic phase/generation/session and guest read/submission evidence; startup override, mixed-boundary attribution and conservative candidate helper. No rendering policy. |
| `src/core/uberhar_frame_diagnostics.h`, `src/core/perf_stats.{h,cpp}` | Preserve phase bands, read evidence and worst-frame context; bounded transitions and five-second summaries with pauses excluded. |
| `src/core/hle/service/fs/file.cpp` | Count each guest read request once before synchronous/asynchronous dispatch; no path/data logging, no service behavior changes. |
| `src/android/app/src/main/jni/native.cpp`, `NativeLibrary.kt`, `fragments/EmulationFragment.kt`, `res/menu/menu_in_game.xml`, `res/values/strings.xml` (Kotlin/resources under Android main) | Confirm frontend cache-loading boundaries; optional session-only Test phase chooser for Loading, Gameplay or automatic unconfirmed evidence. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.{h,cpp}`, `src/video_core/shader/shader_jit.cpp` | Phase context for compilation and generic waits; bounded phase summaries and draw-weighted stage-plan histograms. |
| `src/video_core/shader/generator/glsl_fs_shader_gen.{h,cpp}` | ABI 6 in the existing 128 bytes; compute activity/loop end once per draw, preserving intermediate buffer updates and one shared shader family. |
| `tools/uberhar/test_activity.cpp`, `test_frame_diagnostics.cpp`, `build_probe.sh` | Explicit/unknown/mixed phases, restart/reset, startup override, concurrency, streaming ambiguity and pause-safe exact reconciliation. |
| `tools/uberhar/shader_probe.cpp`, `compare_tev.py` | 736 programs including all 64 stage activity masks, two scale-one encodings and four buffer-write patterns; compare production shader output and fetch behavior against specialization. |

## 0.1.3: prepared TEV operands

<!-- AstraEH: Attribute operand transport, shader changes and independent validation. -->

| Files | AstraEH purpose |
| --- | --- |
| `src/video_core/shader/generator/glsl_fs_shader_gen.{h,cpp}` | Distinct prepared stage type, ABI 7 in 128 bytes; original-register stage activity, nonrecursive stage-zero source normalization, component/inversion selectors and literal scales. Shader consumes prepared operands with exact inherited math and buffer order. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.cpp` | Schema 14 and `prepared_tev_operands=true` startup identification; existing adjacent AstraEH Log Line attribution retained. |
| `tools/uberhar/shader_probe.cpp`, `compare_tev.py` | Expand to 816 TEV programs, cross all legal modifier pairs, emit original effective registers separately for independent texture-fetch expectations, reject an incomplete corpus. |
| `UBERHAR_VERSION`, `UBERHAR.md`, `docs/releases/0.1.3.md`, diagnostics reference | Version, implementation scope, validation results, per-draw CPU tradeoff and unchanged review cadence. |

## 0.1.4: automatic settings/cache context and full review

<!-- AstraEH: Read-only evidence and review maintenance; no rendering algorithm change. -->

| Files | AstraEH purpose |
| --- | --- |
| `src/core/perf_stats.{h,cpp}` | Human-readable settings at startup, sampled changes/resume and shutdown; 32-change cap, exact build/session, explicit Auto resolution and instantaneous sampling limit. |
| `src/video_core/renderer_vulkan/uberhar_cache_diagnostics.h` | Read-only bounded file inventory, active/bypassed layer distinction and independent observed generic-reuse labels. Missing, partial/error, disabled and unobserved states remain separate. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.{h,cpp}` | Translate Android paths, snapshot before load/write, record scan time and driver-load outcome, per-title-load reuse baselines, schema 15 and bounded tagged output. Existing compilation/cache recovery is unchanged. |
| `tools/uberhar/test_cache_diagnostics.cpp`, `build_probe.sh` | Real temporary filesystem cases for namespace filtering, no mutation, empty/zero/stale files, errors, unmapped paths, capped scans, bypassed/disabled layers and independent reuse classification. |
| `AGENTS.md`, `UBERHAR.md`, version/release/diagnostic/review docs | Owner's normal-speed filtering preference, 0.1.4 scope and completed 0.1.3 architecture review; next default full review after 0.1.7. |


## 0.1.5 renderer preparation reuse

<!-- AstraEH: Keep the new optimization, measurements and ownership reviewable. -->

| Files | Purpose and limit |
| --- | --- |
| `src/video_core/renderer_vulkan/uberhar_tev_preparation.h` | Pure current/historical family and sixteen-dimension census preparation; 256 owned-value entries, full input equality after slot hashing, last-entry fast path, profile configuration and title reset. No Vulkan handles or dynamic draw state cached. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.{h,cpp}` | Use prepared values before existing fallback lookup/admission; preserve first observations and caps; reset with title and reconfigure at the profile setter. Schema 16 adds aggregate reuse/eviction/storage and one-in-1024 host timing samples. |
| `tools/uberhar/test_tev_preparation.cpp` | Released-path differential oracle, complete input-byte mutations, forced slot collisions, eviction, title/profile/feature changes, census saturation and dynamic-state exclusion; optional host-only benchmark including low reuse. |
| `tools/uberhar/build_probe.sh` | Run preparation correctness in the existing required host/Actions gate. Benchmarks are opt-in and impose no unstable timing threshold. |
| `docs/releases/0.1.5.md`, `docs/UBERHAR_DIAGNOSTICS.md`, `UBERHAR.md`, `UBERHAR_VERSION` | Release scope, evidence limits, log semantics and current version. Full architecture review cadence remains after 0.1.7 by default. |


## 0.1.6 exact fragment upload reuse

<!-- AstraEH: Worker ownership and all foreign-writer boundaries are part of correctness. -->

| Files | Purpose and limit |
| --- | --- |
| `src/video_core/renderer_vulkan/uberhar_push_constants.h` | Fixed-size, complete-byte shadow for one layout/range; upload callback completes before commit; invalidation and drained reset; worker-owned plain counters. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.{h,cpp}` | Capture dirty flags and per-draw values, then compare after real worker pipeline selection. Invalidate on specialized winners too. Queue progress snapshots, read final after drain, reset with title. Schema 17; shader ABI unchanged. |
| `src/video_core/renderer_vulkan/vk_scheduler.h` | Add FragmentConstants dirty bit; existing AllDirty submission boundary covers it. Command ordering and threading are unchanged. |
| `src/video_core/renderer_vulkan/vk_blit_helper.cpp` | Mark foreign fragment writes in depth/stencil blits and both filter helpers. Compute-only pushes remain stage-separated. |
| `src/video_core/renderer_vulkan/renderer_vulkan.cpp` | Mark both presentation push paths dirty because they share the scheduler and overwrite fragment values. No display behavior changes. |
| `tools/uberhar/test_push_constants.cpp`, `tools/uberhar/build_probe.sh` | Production helper versus independent queued command-state model: all 128 ABI bytes, failed-upload retry, real value snapshots, specialized/generic alternation, foreign writes, new buffers, title reset and ordered counters. Required host gate; not a driver-speed test. |
| `docs/releases/0.1.6.md`, `docs/UBERHAR_DIAGNOSTICS.md`, `UBERHAR.md`, `UBERHAR_VERSION` | Scope, evidence limits, exact invalidation contract and updated version. Next default architecture review remains after 0.1.7. |

## 0.1.7 — fused input and explicit recovery (AstraEH)

- `src/video_core/pica/uberhar_vertex_input.h`: conservative within-batch memory
  admission, scalar-format decoder selection and direct shader-register loads.
- `vertex_loader.h`: exposes value-only initialized attribute layout from the
  inherited decoder; its legacy LoadVertex implementation is not rewritten.
- `pica_core.cpp/.h`: pins spans for admitted no-GS batches, falls back before
  invocation on uncertainty, retains FIFO/output/assembly and reports route counts.
- `glsl_fs_shader_gen.cpp/.h`: first blocking support reason, with exactly the
  existing boolean support predicate retained. No new fragment arithmetic support.
- `vk_pipeline_cache.cpp/.h`: skips unused dynamic transport on specialized-only
  configurations and reports exclusive recovery reasons at the existing cadence.
- `tools/uberhar/test_vertex_input.cpp`, `test_tev_family.cpp`, `build_probe.sh`:
  differential input transport/range tests and legacy support/reason checks,
  wired into the existing mandatory host/shader gate.
- `docs/UBERHAR_ARCHITECTURE_0.1.6.md`: full source/evidence/alternative review,
  reconciled mode semantics, overlapping milestone gates and outstanding limits.


## AstraPro: 0.1.8 ready GPU vertex and input-bound coverage

<!-- AstraPro: Prospective attribution; inherited and earlier AstraEH work is not renamed. -->

| Files | New work and invariant |
| --- | --- |
| `pica/uberhar_index_bounds.h`, `uberhar_vertex_input.h`, `pica_core.*` | Bounded actual-index scan only after conservative range rejection; reprepare pinned spans; live escapes retain legacy input; sampled route/rescue counts and conservative no-GS complete-list GPU admission. |
| `renderer_vulkan/uberhar_gpu_vertex_policy.h`, `vk_rasterizer.*`, `rasterizer_interface.h` | Automatic-only ready GPU experiment, count/upload/range admission, complete CPU fallback and RAII speculative-state cleanup. |
| `rasterizer_cache/framebuffer_base.h` | Explicit cancellation of ownership invalidation when speculative preparation emitted no draw; normal invalidation retained. |
| `vk_pipeline_cache.*`, `vk_graphics_pipeline.*`, `vk_shader_disk_cache.*` | Exact consumed-state/module matching, bounded optional VS/GS/GPU pipeline creation, failed-completion publication, serial pipeline worker, full lifecycle drains and bounded route/admission records. Shader math and ABI7 unchanged. |
| `core/perf_stats.cpp`, Android graphics strings | Expose configured ready GPU policy separately from inherited hardware-shader flag; truthful Combo UI description. |
| Android `UberharDeviceDiagnostics.kt`, `UberharHealthValues.kt` | Thirty-second, fixed 32-node read-only CPU-frequency context; true uptime versus elapsed; zero heap unknown; no host-mode detection or hardware settings writes. |
| `test_vertex_input.cpp`, `test_pipeline_keys.cpp`, `test_ready_gpu_vertices.cpp`, `build_probe.sh` | Actual-index/rescue bitwise tests, complete-key equality, ordered decisions and production framebuffer cancellation/retry tests included in required host gate. |
| Android `UberharHealthValuesTest.kt` | Value normalization and fixed node-read budget regression tests, plus local pure Kotlin smoke coverage. |
| `UBERHAR_VERSION`, release/evidence/diagnostics docs, `AGENTS.md` | 0.1.8 version, measured 0.1.7 scope, new annotations and explicit validation/device limitations. |

Paths in the renderer rows are relative to `src/video_core/`. New GPU execution
reuses inherited translation, not an AstraPro-authored GPU vertex interpreter.


## 0.1.9 — AstraPro: independent Shader-list GPU admission

- `pica/primitive_assembly.h`: read-only pending-winding accessor, no guest state layout change.
- `renderer_vulkan/uberhar_gpu_vertex_policy.h`: typed exclusive admission classification; List and no-GS Shader lists, preserving limits.
- `pica/pica_core.{h,cpp}`: shared real assembler/debugger/topology query, accepted-topology and admission counts, CPU+GPU progress gate.
- `renderer_vulkan/vk_rasterizer.cpp`: recheck real PICA state rather than assumed-safe booleans.
- `core/perf_stats.cpp`, `renderer_vulkan/vk_pipeline_cache.cpp`, Android `strings.xml`: policy label, diagnostic revision 20 and accurate mode description.
- `tools/uberhar/test_ready_gpu_vertices.cpp`, `build_probe.sh`: production assembly/Vulkan mapping differentials and expanded contract tests.
- `docs/UBERHAR_LOG_ANALYSIS_0.1.8.md`, `releases/0.1.9.md`: eight-session evidence, owner functional gates and versioned test card.

Inherited shader translation, fragment accuracy guards, pipeline keys and driver workarounds are unchanged.


## 0.1.10 candidate — AstraPro

- `uberhar_fragment_policy.h`: bounded repeated-demand admission and explicit generic-transport consumer rule.
- `vk_shader_disk_cache.{h,cpp}`: isolated ready specialized fragment cache; immutable config/profile identity, worker publication, failure containment and lookup/cost counters.
- `vk_pipeline_cache.{h,cpp}`: fragment preflight, ready specialized GPU pipeline selection, optimized-route/transport counters; CPU fallback is unchanged.
- `vk_rasterizer.cpp`: preflight before vertex analysis/upload, final synchronized checks, generic-transport bypass only for optional GPU attempts.
- `vk_graphics_pipeline.{h,cpp}`: recoverable optional errors separated from shader-kind statistics.
- `uberhar_test_profile.h` / Android strings: Combo specialization effective preset, saved custom settings preserved; Native/Compute retain forced generic control.
- `pica_core.{h,cpp}`: sparse host attempt timing, never described as GPU execution time.
- `uberhar_frame_diagnostics.h` / `perf_stats.cpp`: frame-end setting ranges, transitions and unknown provenance without deleting timing samples.
- `test_fragment_promotion.cpp`, pipeline/profile/frame tests: bounded policy, hash collisions, reset, CPU retry, failure handling and context boundaries.
- `compare_fragment_state.py`: bounded test-driver program lifetime with the same full pixel corpus.
- `UBERHAR_ARCHITECTURE_0.1.9.md`, release/test-card docs: measured evidence, alternatives, local candidate and explicit publication limits.

No new emulator dependency, public telemetry or permission. Full compute, strip/fan continuation, programmable guest GS, physical latency, FEA ghosting and universal speed qualification are not delivered by this candidate.

- `vk_shader_util.{h,cpp}`: explicit optimizer-input overload for immutable optional jobs; old callers preserve their setting-based behavior. The glslang-dependent translation unit still requires the full CI build.


<!-- AstraPro: 0.1.11 preserves crash evidence without touching renderer routing. -->
## 0.1.11 log retention

- `src/common/logging/uberhar_log_retention.h`: two-generation rotation and
  consumed-entry flush policy, independent of UI and shader state.
- `src/common/logging/backend.cpp`: FileUtil adapter, append-only writer open,
  preserved size cap, first/periodic/error/export/shutdown flushing.
- `tools/uberhar/test_log_retention.cpp`: production-policy fault injection,
  interrupted-startup recovery and actual filesystem generation checks.
- `tools/uberhar/build_probe.sh`: run retention tests in the normal host gate.
- Android `LogExporter.kt` is deliberately unchanged: current and old only;
  `azahar_log.older.txt` is a manual filesystem emergency backup.


<!-- AstraPro: 0.1.12 pending-uniform correctness, without reverting Sonic acceleration. -->
## Pending clip/viewport transport
- `rasterizer_accelerated.cpp/.h`: accumulate VS dirty state until upload; count
  repeated sync of an unuploaded block without adding clocks or per-draw logging.
- `renderer_vulkan/vk_rasterizer.cpp`: one lifetime diagnostic for that counter.
- `tools/uberhar/test_uniform_retry.py`: extract and exercise the production sync
  body with real data types and a modeled owner/upload boundary; run in host CI.
- `docs/UBERHAR_CORRECTNESS_0.1.12.md`: deduplicated Dark Moon/Sonic evidence,
  owner lap/detour notes, source defect, alternatives and limits.


<!-- AstraPro: 0.1.13 keeps CPU and GPU work independently measurable. -->
## Native mapping reuse and targeted GPU diagnostics
- `pica/uberhar_vertex_plan_cache.h`: exact value-only one-entry mapping cache;
  `pica_core.cpp/.h` use it and report hits/builds at the existing cadence.
- `renderer_vulkan/vk_rasterizer.cpp/.h`: correct 17-vector fixed reservation,
  capacity totals and bounded synchronized draw-state snapshots.
- `renderer_vulkan/vk_graphics_pipeline.h`: immutable diagnostic stage mask.
- `renderer_vulkan/vk_pipeline_cache.cpp`: bounded optional pipeline completion
  records with captured stage/state identity; no mutable PICA reads on workers.
- `tools/uberhar/test_vertex_plan_cache.cpp` and `test_fixed_attribute_reserve.py`:
  live-payload equivalence and production writer capacity regression tests.
- `docs/UBERHAR_LOG_ANALYSIS_0.1.12.md`: mode/marker-qualified data and limits.

<!-- AstraEH: Attribution index for the Uberhar implementation, tests, packaging and documentation. -->
# AstraEH code map

**AstraEH** is the assistant attribution name requested for work on Uberhar.
Comments use `AstraEH:` at each logical change section to explain intent and
important constraints. In new Uberhar-only files, the file comment attributes
the whole file; additional section comments explain non-obvious behavior.
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
| `src/video_core/shader/generator/glsl_fs_shader_gen.{h,cpp}` | Optional dynamic six-stage TEV generation, support gate, register decoding, shared operation formulas, rounding/scales and delayed buffer writes. Other fragment behavior remains specialized. |
| `src/video_core/renderer_vulkan/vk_pipeline_cache.{h,cpp}` | Mode capture, push-constant layout, bounded family/pipeline caches, dedicated serial fallback compilation with one-pipeline warm-up admission, ready-fallback selection, per-draw register snapshots, title-switch cleanup and measured wait/build diagnostics. |
| `src/video_core/renderer_vulkan/vk_graphics_pipeline.h` | Acquire ordering when observing completed shader/pipeline compilation. |

## Settings and Android application

| Files | AstraEH work |
| --- | --- |
| `CMakeModules/GenerateSettingKeys.cmake` | Shared keys for hybrid and forced TEV modes. |
| `src/common/settings.{h,cpp}` | Defaults, settings log entries and per-game override reset. |
| `src/citra_qt/configuration/config.cpp` | Read/write both experiment flags in desktop configuration. |
| `src/android/app/build.gradle.kts` | ARM64-only build property, separate Uberhar flavor/application ID, and numeric version read from `UBERHAR_VERSION`. |
| `src/android/app/src/main/jni/{config.cpp,default_ini.h}` | Native setting reads and mandatory default-INI declarations. |
| `src/android/app/src/main/java/org/citra/citra_emu/features/settings/SettingKeys.kt` | JNI declarations matching the generated keys. |
| `src/android/app/src/main/java/org/citra/citra_emu/features/settings/model/BooleanSetting.kt` | Android boolean settings, both default off. |
| `src/android/app/src/main/java/org/citra/citra_emu/features/settings/ui/SettingsFragmentPresenter.kt` | Graphics switches and disabled upstream updater controls for Uberhar. |
| `src/android/app/src/main/java/org/citra/citra_emu/fragments/GamesFragment.kt` | Suppress the upstream update prompt in Uberhar. |
| `src/android/app/src/main/java/org/citra/citra_emu/utils/CitraDirectoryHelper.kt` | Require an empty or previously initialized Uberhar data directory. |
| `src/android/app/src/main/res/values/strings.xml` | Experiment descriptions and data-folder messages. |
| `src/android/app/src/uberhar/res/values/strings.xml` | Launcher label and flavor-specific folder guidance. |

## Validation and build automation

All files in `tools/uberhar/` are new AstraEH work.

| Files | Purpose and limit |
| --- | --- |
| `tools/uberhar/build_probe.sh` | Compile the production generator as a small host executable. |
| `tools/uberhar/shader_probe.cpp` | Reproducible TEV cases, directed edge cases, AddSigned exclusion and 64 full fragment modules. |
| `tools/uberhar/compare_tev.py` | Compare generated specialized/interpreted combiner math on Mesa; synthetic sampling inputs do not test real texture derivatives or device drivers. |
| `tools/uberhar/check_android_keys.py` | Catch missing default-INI keys that would abort Android startup. |
| `tools/uberhar/validate_apk.py` | Find AGP's actual APK, reject ambiguity, verify ARM64 ELF headers, native dependencies and ZIP integrity, and emit a versioned APK and checksum. |
| `tools/uberhar/validate_manifest.py` | Reject known install blockers and identity/authority/permission conflicts in the final decoded manifest. |
| `tools/uberhar/test_manifest.py` | Regression cases for test-only/debug/split flags, version mismatch, shared identity, provider collisions, new permissions and required external Java libraries. |
| `tools/uberhar/development-certificate.sha256` | Public certificate fingerprint pinned by AstraEH so a lost signing cache cannot silently produce incompatible updates. |
| `UBERHAR_VERSION` | Owner-requested release.beta.alpha version shared by Gradle and the release pipeline; this plain data file intentionally has no inline comment. |
| `.github/workflows/uberhar-alpha.yml` | Build/sign the isolated app, reject test-only packaging, verify manifest/native/alignment/signing metadata, and publish versioned GitHub pre-releases from a separate job. |
| `.github/workflows/uberhar-baseline.yml` | Build pinned unmodified upstream; allow its known extra x86 validation library without allowing an x86 emulator library. |
| `.github/workflows/uberhar-shaders.yml` | Compile and validate shader modules, then run differential numerical comparisons. |

The inherited workflows were moved unchanged from `.github/workflows/` to
`.github/upstream-workflows/` to prevent unrelated jobs from running on this
development branch. Their contents are upstream code, not AstraEH implementation.

## Documentation

`README.md` has an AstraEH branch overview above the upstream README.
`UBERHAR.md` describes scope, limits and device testing. `docs/releases/` holds
versioned pre-release notes. This map and
`docs/UBERHAR_DISPLAY_SYNC.md` are AstraEH documents. The display document is a
follow-up investigation plan; alpha 1 contains no screen synchronization or
model-sharpening changes.

<!-- AstraEH: Scope, validation evidence, build notes and device-test instructions for this fork. -->
# Uberhar

Unofficial experimental Azahar fork for Android ARM64 on Ayn Thor.
Baseline: Azahar 2126.1.2, commit
`9e6f523a57fac9564ac0bf8286db3c3702d301ec`.

## Current status

Experimental dynamic TEV fallback implemented. Local tests passed 49,152 exact
RGBA8 comparisons against specialized GLSL across 192 six-stage programs, using
Mesa llvmpipe with synthetic byte and fractional texture colors. All 64 tested
Vulkan fragment modules passed GLSL compilation and SPIR-V validation in CI.
**The first Alpha 1 APK is withdrawn; its replacement is 0.0.2.** The published
`0789e08bd` package passed signing and architecture checks but was marked
`android:testOnly=true`, so normal Android installation rejected it. The original
validation missed this flag. No successful on-device run has been confirmed.

The cause is verified in Android Gradle Plugin 8.13.2's `isTestApk()` source:
`android.injected.build.abi` implies a test-only APK unless explicitly overridden.
Version 0.0.2 removes that IDE option from the Uberhar build, selects ARM64 through
`ndk.abiFilters`, explicitly sets `android.injected.testOnly=false`, and rejects
any final APK whose `aapt dump badging` output still contains `testOnly=`.
The baseline build receives the explicit override and the same rejection check.
Android documents the installation restriction under
[`android:testOnly`](https://developer.android.com/guide/topics/manifest/application-element#testOnly).

Shader code is unchanged from the
[passing shader CI run](https://github.com/RegiRex/uberhar/actions/runs/35946513141).
**[Download uberhar-0.0.2-arm64.apk](https://github.com/RegiRex/uberhar/releases/download/0.0.2/uberhar-0.0.2-arm64.apk)**
from the published [0.0.2 pre-release](https://github.com/RegiRex/uberhar/releases/tag/0.0.2).
No ZIP extraction is needed. The [build and publication run](https://github.com/RegiRex/uberhar/actions/runs/35950115748)
passed all Android package and shader checks. The release tag points to
`f5bff77c37377fe2a16ddf389578df1b2b3ae493`; the public APK's SHA256 matches the tested artifact:

```text
58e4954e2d1d9ee35cbf93dfc21630321f3a9cebb04b408ce6bdfb62fa7d64e5
```

 It retains the ready-fallback safeguard
and AstraEH attribution comments. Device correctness and performance remain unverified.
The baseline workflow builds unmodified upstream code. Its APK retains Azahar's
application ID and is not intended to replace your installed Azahar. Do not
uninstall Azahar to work around a signing-key mismatch.

The Uberhar flavor uses `org.uberhar.uberhar_emu` and the launcher name
**Uberhar Alpha**, so it installs alongside Azahar. Its setup accepts an empty
data directory or a directory previously initialized by Uberhar. Use a separate
folder; copy saves only after setup. Do not move the live Azahar data directory.

## Experimental renderer scope

Enable **Uberhar hybrid TEV (experimental)** in Graphics and restart the game.
The default is off, providing the original renderer for A/B comparison in the
same APK. Vulkan is required. While the specialized pipeline compiles, a bounded
fallback cache interprets all six texture-combiner stages through draw-uniform
push constants. It preserves the specialized generator's stage-0 source rule,
8-bit rounding, DOT3 alpha, saturation, scales and delayed combiner-buffer updates.

Lighting, texture sampling modes, fog, alpha/depth tests, vertex/geometry shaders
and pipeline state are still specialized. New fallback families/pipelines warm
in the background; normal hybrid mode uses a ready fallback or waits for the
specialized pipeline. It never waits specifically for an unready fallback.
First-use stalls therefore remain. There is no vertex interpreter or startup
prewarming yet. Shadow
rendering/sampling, custom normal maps and AddSigned combiner operations use the
specialized path. Numerical comparisons found rounding-boundary differences in
AddSigned, so that operation is deliberately excluded pending a correct fix. Caps of
128 fallback fragment families and 1,024 fallback pipelines bound memory growth;
reaching a cap also uses the specialized path. Hybrid mode waits for an accurate
path when necessary and overrides upstream asynchronous draw skipping.

**Force TEV fallback for comparison** (requires hybrid mode, then restart)
keeps supported draws on the interpreter even after specialization. Use it for
image comparisons; it may be substantially slower. Neither option affects
OpenGL. Experimental fallback entries stay out of transferable shader caches.

On normal game shutdown, the log includes `Uberhar totals`: draw requests,
specialized-pipeline pending observations, fallback draws, warming/unavailable fallbacks,
skipped draws, cache sizes, and measured scheduler pipeline wait time. A pending
observation is not a unique compilation and the wait time is not total stutter.
These counters do not measure physical display synchronization or GPU frame time.

## First device test

1. Install the Uberhar APK alongside Azahar and create an empty Uberhar folder.
2. Use Vulkan, the same driver and resolution in both apps, stereo off. Start
   at a modest internal resolution so GPU load does not obscure compilation.
   For the first comparison, turn off **Enable SPIR-V shader generation** so
   both fragment paths use the GLSL generator covered by the numerical tests.
   Repeat later with that setting enabled to check the default specialized path.
3. In Uberhar, leave both new switches off and asynchronous compilation off.
   Run a repeatable scene and capture a screenshot and log after exiting.
4. Restart with hybrid on and force off; replay the scene with a fresh shader
   cache in the separate Uberhar folder, then repeat once warm.
   Use the game's **Delete cache** action and select Vulkan between cold runs;
   keep the cache for warm runs. Perform all deletion inside Uberhar.
5. Restart with both switches on and compare the same scene's image. Record
   missing geometry, lighting/texture differences, crashes and frame-time changes.
6. Include game/version, driver/version, graphics settings, APK commit, cold/warm
   status and logs with each report. Begin with Ocarina of Time 3D and Awakening.

The development APK uses an explicitly generated development signing key cached
in Actions. The cache can
expire, so seamless updates are not guaranteed; `signature.txt` records the
certificate for each artifact. A durable release signing key is still needed
before distributing regular releases. The first internal build (`f7274114b`)
used a different certificate; 0.0.2 reuses the development key cached for `0789e08bd`.
Publication now checks that certificate fingerprint. If the key cache is lost,
publication stops instead of silently distributing an incompatible update.
Do not uninstall Azahar for any Uberhar
signing problem.

## Milestones

1. Verify an unmodified ARM64 baseline build and record source provenance.
2. Add a separate Uberhar application identity, data directory, and caches.
3. Measure shader/pipeline misses, skipped draws, compilation waits, frame times.
4. Implement and validate a limited Vulkan fragment interpreter fallback.
   Unsupported states use the existing accurate path and are counted.
5. Expand fragment support and implement the PICA vertex interpreter. Only use
   a specialized pipeline when its background compilation has completed.
6. Compare cold/warm cache behavior and image correctness on Thor.
7. Investigate paired top/bottom screen presentation, including input latency.
   Matching software frame IDs cannot prove simultaneous physical refresh.
8. Evaluate model clarity through internal resolution, downsampling, and
   filtering. Measure mobile GPU cost and preserve game rendering semantics.

Awakening ghosting investigation is deferred at the user's request. Stereo was
off; neither a driver defect nor an emulation defect has been established.

The [dual-display source audit](docs/UBERHAR_DISPLAY_SYNC.md) records the separate
presentation queues and a measurement plan for the next phase.

## Build workflow

`UBERHAR_VERSION` contains the owner's **release.beta.alpha** version, currently
`0.0.2`. The unnumbered failed first attempt counts as `0.0.1`. Future alpha
iterations increment the third number. Beta and release milestones use the second
and first numbers. Gradle's independent numeric `versionCode` still increases
with build time so Android can order updates correctly.

After the ARM64 build and shader checks pass, a separate job publishes a GitHub pre-release with
the APK, checksum and validation records. It creates a draft, uploads assets, then
publishes; it never overwrites an existing version. The build job has read-only
repository access and only the publishing job has release-write permission.

The final APK is checked for test/debug/split flags, package/version, SDK metadata,
entry points, provider-authority conflicts, unreviewed permissions and required
external Java libraries. Native checks cover ZIP integrity, ARM64 ELF headers,
linked dependencies, package alignment, signature validity and certificate
continuity. These checks address known packaging problems; they cannot establish
runtime compatibility with every game, driver or third-party Android app.

The baseline workflow checks out the pinned upstream source in a separate
directory. It uses JDK 17, Android platform 35, NDK 27.3.13750724, and CMake
3.30.3. It compiles the emulator for arm64-v8a and uploads the APK, SHA256 checksums, source
revision, submodule revisions, and logs. Runs are bounded to 120 minutes.
Unmodified upstream also bundles an unused x86_64 Vulkan validation layer; the
baseline validator permits that one extra library. The Uberhar APK is strictly
ARM64, including bundled libraries.

Inherited upstream workflows are archived under `.github/upstream-workflows`
on this development branch to avoid unrelated builds and maintenance jobs.

An alpha must actually render supported shader misses with a fallback shader;
renaming Azahar or skipping draws does not qualify. No speedup is promised
before correctness and frame-time measurements.

## Change attribution

Uberhar additions and modified logic carry **AstraEH** comments at logical section
boundaries. These identify work in this fork, not authorship of surrounding
upstream code. The [code map](docs/UBERHAR_CODE_MAP.md) lists every affected area
and provides the exact baseline diff command. Preserve this convention in
subsequent AstraEH changes.

<!-- AstraEH: Scope, validation evidence, build notes and device-test instructions for this fork. -->
# Uberhar

Unofficial experimental Azahar fork for Android ARM64 on Ayn Thor.
Baseline: Azahar 2126.1.2, commit
`9e6f523a57fac9564ac0bf8286db3c3702d301ec`.

## Current status

<!-- AstraEH: Verified 0.0.12 device milestone; retain as the next comparison baseline. -->
**0.0.12 Native at 2x is now tested on Thor.** The owner reports a substantial
improvement. The warm run records 99.760% emulation speed and 89.293 ms of generic
foreground waiting, with all 37 requested generic modules loaded from disk.
Cold generic waiting remains 8.121 s and the worst observed frame interval is
806.541 ms. Extra animation and differing coverage limit direct replay comparisons.
See the [full results and next target](docs/UBERHAR_LOG_ANALYSIS_0.0.12.md).
Preserve this rendering baseline while reducing first-use fragment-program
variation; broader compute and GPU vertex interpretation remain unfinished.

<!-- AstraEH: Owner-requested diagnostic coverage added without changing the 0.0.11 renderer. -->
**0.0.12 adds run diagnostics to the 0.0.11 rendering changes.** Frame pacing,
emulation speed, pause/state-load boundaries, fast-forward ranges and late worst
hitches are captured independently of the overlay. Android health samples add
thermal/battery/memory context with bounded frequency. See the
[0.0.12 notes](docs/releases/0.0.12.md) and
[diagnostic definitions](docs/UBERHAR_DIAGNOSTICS.md). Continue with one Native
cold/warm pair at 2x; pause if called away. All earlier device tests were on Thor.
The 0.0.12 log supplies the first device validation of these combined changes.
Subsequent builds queue without
cancelling the active alpha build; publication still requires every existing gate.

<!-- AstraEH: 0.0.10 measurements and the 0.0.11 CPU/cache response. -->
**0.0.10 device testing is analyzed; [0.0.11 is published](https://github.com/RegiRex/uberhar/releases/tag/0.0.11).**
The [eight-session analysis](docs/UBERHAR_LOG_ANALYSIS_0.0.10.md) finds zero compute
coverage in Awakening, high CPU vertex-stage cost, and repeated generic shader
frontend work even with a warm driver cache. The three profiles therefore did
not exercise different rendering routes in this game. All profile runs were 1x;
custom comparisons were 2x, with unequal lengths and interruptions.

**0.0.11** restores the established cached CPU shader JIT in the three profiles,
accelerates exact indexed vertex reuse, removes heap allocations from reference
interpreter stacks, persists compatible generic SPIR-V modules, and skips optional
frontend optimization in those profiles. New bounded diagnostics expose CPU JIT
compilation, windowed vertex work, actual shader invocations, module-cache reuse,
compute blockers and startup/live host pipeline origins.

All-off still restores saved custom graphics settings; selecting a profile locks
other Graphics controls except resolution/integer scaling. Compute coverage remains
limited to validated solid rectangles; rejected draws use native rendering.
**These are incomplete prototypes.** CPU JIT and new generic GPU pipelines still
compile on first use. A GPU vertex interpreter, full compute renderer and complete
ready pipeline bank remain unfinished. The 0.0.12 results above test these combined
changes; they do not isolate the speedup from each individual optimization.

Start with **Native at 2x, cold then warm**, including the same battle. The
[release notes](docs/releases/0.0.11.md) describe the comparison. Repeating all three
pairs is not necessary while compute coverage remains zero. The next default full
architectural review stays after 0.0.13 (allowed 0.0.12–0.0.14); see the
[review ledger](docs/UBERHAR_REVIEW_CADENCE.md). Android compilation, shader, package
and signing gates must pass before a pre-release is published. Development hands
off Actions rather than waiting through APK compilation.

<!-- AstraEH: 0.0.4 changes log collection without changing the 0.0.3 renderer. -->
**0.0.4 is published as a pre-release** with **Options → Save or Share Log**. Choose the
current or previous session, choose word initials or the first three characters
for each game's abbreviation, then save through Android's file picker or share
the named text file. The preview uses the selected log's session date and games;
replayed titles appear once. Example: `uberhar_log_9_24_0100_FEA_MK7.txt`.
The unused launch flag that selected the wrong log has been removed. A native
queue barrier flushes buffered entries before a fixed export copy is made.
The [0.0.4 notes](docs/releases/0.0.4.md) explain older logs and filename limits.
No shader algorithm or shader cache format changes accompany this export update.

The original dynamic TEV fallback's local tests passed 49,152 exact
RGBA8 comparisons against specialized GLSL across 192 six-stage programs, using
Mesa llvmpipe with synthetic byte and fractional texture colors. All 64 tested
Vulkan fragment modules passed GLSL compilation and SPIR-V validation in CI.
**The first Alpha 1 APK is withdrawn; packaging was corrected in 0.0.2.** The published
`0789e08bd` package passed signing and architecture checks but was marked
`android:testOnly=true`, so normal Android installation rejected it. The original
validation missed this flag. The owner subsequently reported running Fire Emblem
Awakening with long shader-related pauses. Effective settings and a device log
were subsequently supplied in a 0.0.2 log: hybrid and forced TEV were both enabled.
The first completed run recorded 63 pipeline waits totaling 29.100457 seconds.
The exact duration of individual pauses is unavailable in that build. A 0.0.3
normal-hybrid cold/warm retest was subsequently supplied; its findings and
measurement limits are recorded in the log analysis linked above.

<!-- AstraEH: 0.0.3 scheduling correction motivated by the first gameplay report. -->
**0.0.3 is published as a pre-release.** Fallback shaders and pipelines now compile as one
job on a separate serial worker. Normal hybrid mode admits only one unfinished
fallback pipeline at a time, preventing speculative work from filling the
specialized compiler queues. Existing ready fallbacks remain reusable. Force mode
can queue additional builds because it explicitly waits for comparison. Driver
locks and CPU/GPU contention can still cause stalls; this change does not establish
the cause of the owner's pauses or a measured speedup. See
[0.0.3 notes](docs/releases/0.0.3.md) for the targeted retest.
Upstream's cache fingerprint includes pipeline-cache source files, so this update
can regenerate existing cached shader programs at launch. The stored game shader
configurations and driver pipeline cache can still be reused where valid. Do not
treat that initial cache regeneration as a measured in-game regression.

The cause is verified in Android Gradle Plugin 8.13.2's `isTestApk()` source:
`android.injected.build.abi` implies a test-only APK unless explicitly overridden.
Version 0.0.2 removes that IDE option from the Uberhar build, selects ARM64 through
`ndk.abiFilters`, explicitly sets `android.injected.testOnly=false`, and rejects
any final APK whose `aapt dump badging` output still contains `testOnly=`.
The baseline build receives the explicit override and the same rejection check.
Android documents the installation restriction under
[`android:testOnly`](https://developer.android.com/guide/topics/manifest/application-element#testOnly).

<!-- AstraEH: Preserve verified published release evidence while the next version builds. -->
**[Published 0.0.9 APK](https://github.com/RegiRex/uberhar/releases/download/0.0.9/uberhar-0.0.9-arm64.apk)**
is an earlier comparison baseline; use the newest validated pre-release for testing. No ZIP extraction is needed. Install published updates over Uberhar.
The [0.0.9 pre-release](https://github.com/RegiRex/uberhar/releases/tag/0.0.9)
targets `71657e2b58292690fd24185703197365bdab57bc`. GitHub records its APK SHA256 as:

```text
f2993dc6aae3d08861e896fe78369ab80f2369c0827a1b33deefe3e557a3498a
```

The export feature adds no Android permissions or runtime dependencies. The
private sharing provider exposes only staged log copies with explicit read
grants. Actual file-picker and share-target behavior still needs device testing.

The ready-fallback safeguard and AstraEH attribution comments remain in place.
The supplied 0.0.9 capture supports the current analysis; broader device
correctness and comparative performance still require controlled tests.
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

Version 0.0.8 also interprets alpha/scissor tests, depth mapping, fog and
compatible sampling controls. Lighting/procedural structure and cube resource
types remain specialized. **CPU vertex bridge (experimental)**, default on but
effective only in normal hybrid mode, can use the existing CPU engine for complete
triangle-list batches of at most 4,096 vertices when a compatible generic pipeline
is already ready. This allows sharing a fixed vertex interface while GPU
specialization compiles. It is not a GPU vertex interpreter; CPU JIT and
preparation can still cost time. Toggle it independently and restart to compare.
Version 0.0.9 also admits
strips/fans (at most 1,367 inputs / 4,095 expanded vertices) and list-equivalent
Shader topology without guest geometry. It uses isolated assembly for bridge
draws so later ready GPU draws can resume; ordinary CPU assembly is unchanged.

Generic pipelines still warm on a dedicated serial worker, with one unfinished
build admitted in normal hybrid mode. A ready specialization has priority.
Missing or unsupported fallbacks wait for specialization; experimental failures
wake waiters and recover through specialization. Shadow paths, custom normal
maps, gas fog and AddSigned operations retain the specialized path. AddSigned
was excluded after rounding-boundary differences in numerical tests.
Caps of 128 fallback families and 1,024 fallback pipelines bound memory growth.
Hybrid mode preserves draws instead of using upstream asynchronous skipping.
There is no complete startup generic bank yet, and first-use stalls remain.

Runtime pipeline keys share already-identical host shader objects and exclude
inactive state. Transferable records retain guest IDs and their existing layout.
The private fragment state ABI is 108 bytes in this version.

**Force TEV fallback for comparison** (requires hybrid mode, then restart)
keeps supported draws on the generic fragment path even after specialization. Use it for
image comparisons; it may be substantially slower. The experiment switches do not affect
OpenGL. Experimental fallback entries stay out of transferable shader caches.

On normal game shutdown, the log includes `Uberhar totals`: draw requests,
specialized-pipeline pending observations, fallback draws, warming/unavailable fallbacks,
skipped draws, cache sizes, and measured scheduler pipeline wait time. Version
0.0.3 also reports the longest wait, forced-fallback wait time, waits over 50 ms,
deferred warm-up observations, and fallback shader/pipeline build wall time.
`fallback_deferred` is a subset of `fallback_unavailable`, not an additional draw
count. Pipeline build time includes waiting for vertex/geometry dependencies.
The first 20 long waits and fallback builds are logged during play, while the
startup line records effective hybrid/force/async/SPIR-V settings. A pending
observation is not a unique compilation and the wait time is not total stutter.
These counters do not measure physical display synchronization or GPU frame time.

<!-- AstraEH: Separate compiler-input size and speculative-work utility from latency. -->
Version 0.0.6 adds `Uberhar fallback utility`: completed pipelines that have/have
not served a draw, and the driver-call time of the latter. At progress snapshots,
an unused pipeline may still become useful later. Bounded build records report
GLSL/SPIR-V byte sizes; zero means an existing fragment module was reused.
`diagnostics=3 compact_tev=true fast_fallback=false` identifies 0.0.6.
Version 0.0.12 uses `diagnostics=9`; see the
[diagnostics map](docs/UBERHAR_DIAGNOSTICS.md) for bridge/translation/cache counters,
capability probes, limits and diagnostic-removal markers.

## Device testing

For the current 0.0.12 comparison, follow the shorter cold/warm procedure in the
[release notes](docs/releases/0.0.12.md). The original baseline procedure is below;
keep the CPU bridge off when reproducing those initial two-switch comparisons.

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
5. Measure the broader fragment bank, ready CPU vertex bridge and host-pipeline
   sharing on Thor. Reduce unused work, evaluate startup generic preparation and
   capability-dependent pipeline libraries. A GPU PICA vertex interpreter remains
   a later coverage project; driver creation dominates the supplied wait evidence.
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
`0.0.12`. The unnumbered failed first attempt counts as `0.0.1`. Future alpha
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

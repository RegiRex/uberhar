<!-- AstraEH: Scope, validation evidence, build notes and device-test instructions for this fork. -->
# Uberhar

Unofficial experimental Azahar fork for Android ARM64 on Ayn Thor.
Baseline: Azahar 2126.1.2, commit
`9e6f523a57fac9564ac0bf8286db3c3702d301ec`.

## Current status

<!-- CodexAstraUlt: The owner's full local takeover follows completed 0.1.23 publication and real-device testing; preserve the implementation history below. -->
**0.1.23 is published; local Codex now owns the next development/test batch.**
Read the [autonomous handoff](docs/UBERHAR_LOCAL_AUTONOMOUS_HANDOFF.md) and
[submitted local device report](docs/device_reports/UBERHAR_DEVICE_TEST_2026-10-06.md).
USB testing and the compatible install are recorded, but full-speed correctness
remains open. The owner clarifies that the moon is visually clean in Native while
many moon segments flash in Combo; the earlier report's still-frame interpretation
did not establish shared flashing. Both Combo moon flashing and ghost corruption
remain targets for matched video evidence. The cloud session pauses concurrent source
work after this handoff. Earlier pending-build statements below are historical.

<!-- CodexAstraUlt: Replace the pending comparison-only summary with measured 0.1.22 cleanup and the scoped 0.1.23 work; earlier implementation history remains below and in progress records. -->
**0.1.22 now shows a large live Combo GPU footprint that is released on exit.**
The owner's Dark Moon log contains Combo and Native runs: own-process KGSL reaches
about **4,232 MiB** in Combo versus roughly **1,081 MiB** in Native, then returns to
about **12 MiB / 15 MiB** respectively. System available memory recovers. These runs
do not show a persistent post-exit leak; they do identify excessive live pressure
worth addressing. The owner still sees ghost and moon glitches, with more glitches
observed overall, while reporting a significant perceived speed improvement over
earlier builds. That visual failure remains open; the speed report is not a
matched benchmark.

**0.1.23 optimizes optional background GPU shaders**, preserving the cold generic
path and Custom behavior. Implementation, independent review and local validation
passed; CI package and shader gates remain required. Reduced memory use or higher
device speed is not yet demonstrated. See the
[new log analysis](docs/UBERHAR_LOG_ANALYSIS_0.1.22.md),
[candidate notes](docs/releases/0.1.23.md),
[architecture review](docs/UBERHAR_ARCHITECTURE_0.1.20.md) and
[current validation/build status](docs/UBERHAR_PROGRESS.md).

The [0.1.22 build](https://github.com/RegiRex/uberhar/actions/runs/37524017429)
passed all shader, Android and signing/package gates; its validated comparison
artifact is available. Its quaternion-correction guard, CPU-matching attribute
padding and bounded KGSL/system snapshots remain part of the next candidate.
The owner now authorizes normal repository prereleases through the existing
`uberhar/hybrid-shaders` release workflow in **RegiRex/uberhar only**, never the
upstream Azahar repository. This is now the primary development path; parallel
experiments use side branches. Literal integration into divergent upstream
`master` is unnecessary for repository prereleases and is deferred for separate
review, preserving the tested source and signing continuity.

For local testing, use the [Nobara/Thor readiness guide](tools/uberhar/device_testing/README.md)
and [local Codex handoff](tools/uberhar/device_testing/LOCAL_CODEX_HANDOFF.md).
These ordinary repository files replace the chat-only setup bundle. Switching the
Thor's **USB controlled by** setting is not required; check `adb devices -l`.
Actual device communication and a repeatable gameplay runner still require setup.

<!-- CodexAstraUlt-2: Separate 0.1.18 implementation and publication from device acceptance. -->
**0.1.18 was the correctness/recovery update following the 0.1.17 review.** Combo
retains CPU rendering for active zero-stride vertex loaders whose GPU input packing
differs from Native. Shader/pipeline failures publish completion and stop the failed
command stream; log-flush queue insertion is nonblocking. Interrupted logs are staged
before rotation, with large preservation copies on the existing recovery worker.
See [release and short test instructions](docs/releases/0.1.18.md),
[progress](docs/UBERHAR_PROGRESS.md), and [review](docs/UBERHAR_ARCHITECTURE_0.1.17.md).
Build/publication and Thor device acceptance are separate gates. No Dark Moon cure,
speed increase, general compute rasterizer or zero-first-use-wait claim is made.
The 0.1.17 Android, shader and publication workflow completed successfully.

<!-- AstraEH: Optional OS evidence is now explicitly accepted, with file-only access. -->
**0.1.17 adds optional Android crash evidence saved under `log/crashes/`.** A single
background startup check can save a dated exit summary and available native tombstone
or OS trace for an abnormal exit from a tagged Uberhar process. The in-app crash-report
browser is removed; ordinary current/previous/older export remains. No continuous
monitor, second log writer, new permission or runtime dependency. OS capture defaults
on and is disabled with `[Debugging] android_crash_reports=false` in `config/config.ini`.
Missing Android services or traces do not block gameplay or base log retention.
See [release and file-access notes](docs/releases/0.1.17.md).
**0.1.16 passed all release gates; owner/device acceptance is still pending.** Renderer
behavior is unchanged. Older status snapshots below preserve the superseded designs.


<!-- AstraEH: Owner requires lean logging and compatibility with stripped-down Android. -->
**0.1.16 removes the per-launch bundle system and continuous duplicate log writer.**
Current/previous/older remain the primary text logs. One small marker identifies
unfinished runs; the next launch saves incident text before rotation, without Android
exit-history collection or a separate periodic logging worker. Normal launches create
no archives. Existing useful legacy evidence is migrated, with ordinary duplicates
removed. See [release notes](docs/releases/0.1.16.md). GammaOS is a compatibility target;
actual device validation remains pending. Renderer behavior is unchanged.
**0.1.15 passed all Android, shader and publication gates; owner testing is pending.**
Older implementation/status entries below are historical and are superseded where
0.1.16 simplifies logging.


<!-- AstraEH: Device feedback exposed Android path dispatch and rejected per-launch bundle clutter. -->
**0.1.15 repairs private logging and restores the compact current/previous/older
text-log picker.** Meaningful incident evidence moves behind a separate Crash reports
action. The new host regression includes actual Android path dispatch, which the
previous stdio-only test missed. See [release notes](docs/releases/0.1.15.md) and
[0.1.14 device analysis](docs/UBERHAR_LOG_ANALYSIS_0.1.14.md). Renderer behavior is
unchanged; Dark Moon's visual failure and large system-memory decline need focused
diagnosis. Older status entries below are historical.

<!-- AstraEH: Current reliability priority supersedes historical milestone plans below. -->
**0.1.14 adds independently retained session evidence and narrow shader-worker
correctness fixes.** Saved ZIP bundles survive ordinary log rotation and remain
until explicitly deleted. Available Android exit records and exact-build symbols
support crash diagnosis; lost queued records and unavailable OS traces remain
limitations. Dark Moon graphical glitches and severe slowdown remain open.
See the [release/test notes](docs/releases/0.1.14.md) and the
[accepted full review](docs/UBERHAR_ARCHITECTURE_0.1.13.md).
Per-game settings are planned by 2.0, using one editor reached from title
long-press and the in-game Uberhar settings submenu. Broader audit experiments
are deferred until at least Dark Moon's graphical issues are fixed and validated.
The earlier status entries below are retained as milestone history.

<!-- AstraEH: Scoped beta milestone and evidence-based vertex transport optimization. -->
**0.1.0 is the Native beta update.** The owner's four 0.0.15 Thor runs show no
noticeable normal-speed gameplay slowdown. [Measured results](docs/UBERHAR_LOG_ANALYSIS_0.0.15.md)
record 98.858/99.795% normal cold/warm speed, 283.379/292.518% fast-only speed under
a 400% cap, and about 80% less cold generic waiting than 0.0.14. Brief early cold
hitches remain. This is a Native/2x/Awakening milestone, not universal zero lag.

The beta prepares input/output register maps once per no-GS draw and reuses final
96-byte rasterizer vertices in the existing FIFO. This removes repeated transport
and conversion on cache hits while retaining all vertices and primitive order.
Geometry/debugger/incomplete-map draws retain their established path. Sparse,
bounded CPU-stage samples guide further headroom work. The compact fragment
interpreter, ABI 5/128-byte state, native pipeline keys and driver workarounds
stay intact. The app label becomes Uberhar Beta; package/data/signing continuity
is retained. The six-session device results are summarized below.

See the [0.1.0 release/test notes](docs/releases/0.1.0.md) and
[full architectural review and goals](docs/UBERHAR_ARCHITECTURE_0.1.0.md).
The four-run Native/2x test is complete; the focused next comparison is below.
Higher resolution, broader virtual-PICA coverage, dual-screen matching and model
clarity follow as evidence permits. Preserve 0.0.15 as the measured baseline.

<!-- AstraEH: Device validation distinguishes CPU savings from whole-emulator throughput. -->
**0.1.0 is published and tested across three games.** Awakening stays near full
normal speed; vertex-stage time per input falls 9.3–9.8%, while unequal fast-forward
runs average less than 0.0.15. Ocarina runs at 99.912% normally and 374.628% during
the tested fast segment. Sonic uses supported generic shaders throughout but has
large sustained draw/vertex costs after shader creation ends. See the
[full analysis](docs/UBERHAR_LOG_ANALYSIS_0.1.0.md), including research on Luma L2
behavior and why it is distinct from Azahar's New 3DS model selection.

<!-- AstraEH: Continue measured Native host-command optimization after device reuse evidence. -->
**0.1.6 avoids repeated uploads of identical fragment push constants.** Complete
128-byte comparisons run after actual worker pipeline selection, with new-buffer,
foreign-writer and title invalidation. Ordered counters report uploads and exact
reuse without per-draw timing/atomic overhead. Shader math, ABI and descriptor
binding remain unchanged. Device benefit is pending; see
[0.1.6 notes](docs/releases/0.1.6.md). Next default full review remains after 0.1.7.

<!-- AstraEH: Measured host-path optimization follows the completed 0.1.3 review. -->
**0.1.5 reuses exact fallback preparation across repeated draw states.** A bounded
256-entry cache removes repeated family/census calculations while preserving the
same keys, candidate coverage, draw constants and pipeline readiness/recovery.
Device logs confirm 98.1–98.4% preparation reuse in FEA and 99.9959% in Sonic.
Sonic improves modestly in the selected sustained interval; unmatched scenes and
cache history limit attribution. FEA 2x remains near full speed and 4x still varies.
See [0.1.5 notes](docs/releases/0.1.5.md) and the subsequent evidence discussion in
[0.1.6 notes](docs/releases/0.1.6.md). Next default full review remains after 0.1.7.

<!-- AstraEH: Automatic test context reduces manual notes without inventing warm-cache status. -->
**0.1.4 records readable settings and cache context automatically.** Per-run and
observed-change snapshots identify resolution, mode/API and relevant controls.
Startup file inventory and actual compatible generic-module reuse are separate;
disabled, incomplete and opaque driver state stay explicit. Rendering/ABI are
unchanged. See [0.1.4 notes](docs/releases/0.1.4.md) and the
[completed architecture review](docs/UBERHAR_ARCHITECTURE_0.1.3.md).

<!-- AstraEH: Continue moving draw-constant interpretation out of fragment execution. -->
**0.1.3 prepares TEV operand selection once per draw.** Stage-zero Previous
redirection, modifier component/inversion selection and literal scales join the
existing stage plan. The shared shader keeps exact arithmetic, rounding, texture
reuse and delayed buffer ordering. ABI 7 remains 128 bytes; shader family keys
are unchanged. Host correctness is validated; device speed remains unmeasured.
See [0.1.3 notes](docs/releases/0.1.3.md) for tests and comparison instructions.

<!-- AstraEH: General fragment efficiency and honest loading attribution. -->
**0.1.2 prepares the generic TEV stage plan once per draw.** Runtime activity
bits and the last useful stage replace per-fragment passthrough decoding and
trailing no-op stages, while intermediate delayed buffer writes stay ordered.
It retains one generic family per existing key (128-byte ABI 6). Diagnostics
separate known frontend loading, user-marked Loading/Gameplay, Unknown and Mixed
intervals. File-read/submission activity is explicitly uncertain evidence.
The in-game **Test phase** menu optionally confirms the current phase; it never
changes emulation or compilation policy. See [0.1.2 notes](docs/releases/0.1.2.md).

**0.1.1 is a focused diagnostics update:** ongoing samples bounded to one per 50 ms,
sampled batch setup/vertex/DrawTriangles timing, one startup machine snapshot and
bounded FRD warnings. Shader math, vertex results, model selection, guest IPC
responses, Android permissions and package/signing policy remain intact. No Sonic
speed fix or automatic console-mode override is claimed. The owner's
[compatibility-profile request](docs/UBERHAR_COMPATIBILITY_PROFILES.md) is recorded
for after 1.0.0; that release remains focused on the shader-system upgrade.
See the [0.1.1 notes](docs/releases/0.1.1.md) for a controlled Sonic comparison.

<!-- AstraEH: Historical implementation and observations precede the current beta. -->
**0.0.14 is published and tested.** Its [two-run analysis](docs/UBERHAR_LOG_ANALYSIS_0.0.14.md)
shows that the 577.234-second sleep pause was properly excluded. Same-workload
families fall 21→16 and warm speed stays 99.757%, but cold generic waiting rises
2.948→3.298 seconds and the worst cold frame rises 496→582 ms. Larger unrolled
lighting programs compile more slowly, motivating the compact loop. This is not
a uniform timing improvement; 0.0.15 subsequently supersedes this cold-performance reference.

<!-- AstraEH: Historical 0.0.14 implementation; measured results are recorded above. -->
**0.0.14 consolidates lighting enable/configuration variants** into runtime data,
using the existing 120-byte push constants (semantic ABI v4). Disabled LUTs return
before table work, retaining red-channel fallback and final-light Fresnel behavior.
The log compares current families with both 0.0.12 and 0.0.13 rules for the same
observed draws, and records at most 32 remaining family shapes per title. The core
header now labels its frame schema separately from renderer diagnostics 11.
See the [0.0.14 release/test notes](docs/releases/0.0.14.md) and the measured results above.
Publication passed the Actions gates. These historical results motivated 0.0.15.

<!-- AstraEH: 0.0.13 now has three complete Thor runs with distinct cache coverage. -->
**0.0.13 moves lighting LUT controls and light-source selection into per-draw data**
to reduce first-use fragment families. It retains structural lighting specialization,
adds same-workload previous/new family counts, and expands shader correctness gates.
The prerelease passed all Actions gates. Its [three-run Thor analysis](docs/UBERHAR_LOG_ANALYSIS_0.0.13.md)
confirms 37→21 families for the cold workload under old/new rules, 2.948 s cold
generic waiting, and 99.776% fully warm speed. The extra-attack run encounters nine
new modules; the third run is the fully warm comparison. This supplied the next Native/2x reference at that time. See the [release/test notes](docs/releases/0.0.13.md).

<!-- AstraEH: Historical 0.0.12 device milestone used to assess the 0.0.13 change. -->
**0.0.12 Native at 2x is now tested on Thor.** The owner reports a substantial
improvement. The warm run records 99.760% emulation speed and 89.293 ms of generic
foreground waiting, with all 37 requested generic modules loaded from disk.
Cold generic waiting remains 8.121 s and the worst observed frame interval is
806.541 ms. Extra animation and differing coverage limit direct replay comparisons.
See the [full results and next target](docs/UBERHAR_LOG_ANALYSIS_0.0.12.md).
This result supplied the 0.0.13 comparison baseline. Broader compute and GPU
vertex interpretation remain unfinished.

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
architectural review is after 0.1.7 (allowed 0.1.6–0.1.8); see the
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
The private fragment state ABI is 120 bytes in 0.0.13–0.0.14 (108 bytes in 0.0.8–0.0.12).
0.0.14 changes its meaning to v4 without growing its size. 0.0.15 uses 128 bytes
(v5) for the additional runtime light count/operation words.

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
Version 0.0.15 uses `diagnostics=12` and separate `frame_diagnostics=2`; see the
[diagnostics map](docs/UBERHAR_DIAGNOSTICS.md) for bridge/translation/cache counters,
capability probes, limits and diagnostic-removal markers.

## Device testing

For the current 0.0.13 comparison, follow the shorter cold/warm procedure in the
[release notes](docs/releases/0.0.13.md). The original baseline procedure is below;
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

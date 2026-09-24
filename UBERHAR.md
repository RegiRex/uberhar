# Uberhar

Unofficial experimental Azahar fork for Android ARM64 on Ayn Thor.
Baseline: Azahar 2126.1.2, commit
`9e6f523a57fac9564ac0bf8286db3c3702d301ec`.

## Current status

Experimental dynamic TEV fallback implemented. Local tests passed 49,152 exact
RGBA8 comparisons against specialized GLSL across 192 six-stage programs, using
Mesa llvmpipe with synthetic texture colors. Android packaging verification is
in progress. No on-device performance or correctness result exists yet.
The baseline workflow builds unmodified upstream code. Its APK retains Azahar's
application ID and is not intended to replace your installed Azahar. Do not
uninstall Azahar to work around a signing-key mismatch.

The Uberhar flavor uses `org.uberhar.uberhar_emu` and the launcher name
**Uberhar Alpha**, so it installs alongside Azahar. Its setup accepts an empty
data directory or a directory previously initialized by Uberhar. Use a separate
folder; copy saves only after setup. Do not move the live Azahar data directory.

## Alpha 1 scope

Enable **Uberhar hybrid TEV (experimental)** in Graphics and restart the game.
The default is off, providing the original renderer for A/B comparison in the
same APK. Vulkan is required. While the specialized pipeline compiles, a bounded
fallback cache interprets all six texture-combiner stages through draw-uniform
push constants. It preserves the specialized generator's stage-0 source rule,
8-bit rounding, DOT3 alpha, saturation, scales and delayed combiner-buffer updates.

Lighting, texture sampling modes, fog, alpha/depth tests, vertex/geometry shaders
and pipeline state are still specialized. The first fallback family/pipeline
can block. There is no vertex interpreter or startup prewarming yet. Shadow
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
specialized-pipeline pending observations, fallback draws, unavailable fallbacks,
skipped draws, cache sizes, and measured scheduler pipeline wait time. A pending
observation is not a unique compilation and the wait time is not total stutter.
These counters do not measure physical display synchronization or GPU frame time.

## First device test

1. Install the Uberhar APK alongside Azahar and create an empty Uberhar folder.
2. Use Vulkan, the same driver and resolution in both apps, stereo off. Start
   at a modest internal resolution so GPU load does not obscure compilation.
3. In Uberhar, leave both new switches off and asynchronous compilation off.
   Run a repeatable scene and capture a screenshot and log after exiting.
4. Restart with hybrid on and force off; replay the scene with a fresh shader
   cache in the separate Uberhar folder, then repeat once warm.
5. Restart with both switches on and compare the same scene's image. Record
   missing geometry, lighting/texture differences, crashes and frame-time changes.
6. Include game/version, driver/version, graphics settings, APK commit, cold/warm
   status and logs with each report. Begin with Ocarina of Time 3D and Awakening.

The development APK uses a debug signing key cached in Actions. The cache can
expire, so seamless updates are not guaranteed; `signature.txt` records the
certificate for each artifact. A durable release signing key is still needed
before distributing regular releases. Do not uninstall Azahar for any Uberhar
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

## Build workflow

The baseline workflow checks out the pinned upstream source in a separate
directory. It uses JDK 17, Android platform 35, NDK 27.3.13750724, and CMake
3.30.3. It builds only arm64-v8a and uploads the APK, SHA256 checksums, source
revision, submodule revisions, and logs. Runs are bounded to 120 minutes.

Inherited upstream workflows are archived under `.github/upstream-workflows`
on this development branch to avoid unrelated builds and maintenance jobs.

An alpha must actually render supported shader misses with a fallback shader;
renaming Azahar or skipping draws does not qualify. No speedup is promised
before correctness and frame-time measurements.

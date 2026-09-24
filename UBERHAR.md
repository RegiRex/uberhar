# Uberhar

Unofficial experimental Azahar fork for Android ARM64 on Ayn Thor.
Baseline: Azahar 2126.1.2, commit
`9e6f523a57fac9564ac0bf8286db3c3702d301ec`.

## Current status

Build infrastructure only. No hybrid shader renderer or tested alpha exists yet.
The baseline workflow builds unmodified upstream code. Its APK retains Azahar's
application ID and is not intended to replace your installed Azahar. Do not
uninstall Azahar to work around a signing-key mismatch.

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

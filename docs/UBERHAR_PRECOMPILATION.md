# Uberhar precompiled-first comparison

<!-- CodexAstraUlt-2: Architecture, limits and validation for the isolated 0.1.57 candidate. -->

The `uberhar/precompiled-first` branch starts at `0723b003` (0.1.56-r2).
It prepares known Vulkan CPU-fragment pipelines before gameplay and selects ready
specializations before asking for a generic shader. This is an implementation of
observed-state preparation. It cannot enumerate every future draw of an arbitrary
3DS game, and it does not establish a performance improvement on the AYN Thor.

## Installing beside main Uberhar

<!-- CodexAstraUlt-2: The rebuilt comparison has its own Android identity and data-folder marker. -->

The rebuilt comparison installs as **Uberhar Precompiled**, with Android package
`org.uberhar.uberhar_emu.precompiled`. Main Uberhar keeps
`org.uberhar.uberhar_emu`; the comparison no longer updates or replaces that app.
Kotlin/JNI class names and the approved signing certificate remain unchanged.
Private preferences, internal files, driver installations and URI grants belong to
separate Android app sandboxes. There is no automatic migration from main Uberhar.

Select an empty **Uberhar Precompiled** data folder during setup. The comparison
uses `uberhar-precompiled-data.txt` and rejects folders containing the main
`uberhar-data.txt` marker, even if a candidate marker was copied into them. Copy selected saves after initializing the
new folder if desired; sharing a game-file directory does not require sharing the
emulator data folder. Keep main's saves, configuration and shader caches separate.

The earlier 0.1.57 artifact from run 41971435934 used main Uberhar's package ID.
Use the rebuilt artifact linked from the branch's latest build, rather than that
older download, for a parallel installation. Both have numeric version 0.1.57;
the APK's package identity and source revision distinguish them.

## What happens before gameplay

1. Load the title's driver cache using the existing device/driver identity checks.
2. Load a bounded, profile-matched inventory of complete fragment configurations
   and CPU pipeline descriptions from the new semantic recipe file.
3. Read compatible stock `.vkch` fragment/pipeline records without modifying those
   files. Accept only observed trivial-VS/no-GS pipelines with the exact software
   vertex layout. Regenerate fragment code; never execute imported host SPIR-V.
4. Rank recipes by observed use, group identical fragment configurations, compile
   each required fragment module, then build its exact Vulkan pipelines on the
   renderer's actual device, layout, render-pass manager and driver cache.
5. Retain useful pipelines as ready objects. Build excess known pipelines one at a
   time and release them after compilation, attempting to populate the driver cache.
6. Save completed driver-cache work and imported semantic records at this loading
   boundary. Gameplay then starts with the completed resident bank available.

Preparation has no time quota. Cancellation is checked between operations;
an individual driver/compiler call cannot be interrupted safely. Resource or
compilation failure stops further preparation, retaining successful earlier work.
Known device loss latches terminal renderer cancellation and prevents another
device-idle wait during teardown.

## Selection during gameplay

For eligible CPU draws, the order is ready prepared full specialization, ready
adaptive full specialization, ready static-TEV specialization, then the existing
generic/recovery route. A ready specialization bypasses generic creation and its
wait. A ready static specialization can continue admitting full specialization in
the background. Missing optional work still has a complete rendering fallback.

Existing hardware-vertex selection keeps its stage/layout checks. The new records
are collected only from actual cacheable CPU draws, including draws initially
served by a generic shader. No hardware layout is relabeled as an observed CPU
pipeline, and no draw is skipped to hide a compiler miss.

Native and full Combo use the new preparation path when their existing full-CPU
fragment option and disk shader cache are enabled. Generic-only modes, an explicit
Native full-fragment opt-out, and Custom retain their existing setting semantics.
Custom's ordinary cache loader remains separate. Driver selection is unchanged.

## Bounded memory and honest readiness

| Item | Candidate policy |
| --- | --- |
| Semantic inventory | At most 8192 recipes per title/profile; at most 3,252,288 encoded bytes |
| Resident prepared pipelines | At most 128, ranked by observed usage |
| Resident prepared fragment modules | At most 64, reduced when a revisited title already owns optional modules |
| Transient preparation | One fragment group and one pipeline at a time; reserve a module slot before starting |
| Shared optional pipeline/module limits | Existing aggregate 256 pipelines and 128 full fragment modules include prepared owners |
| Extra validated recipes | Attempt sequential driver-cache warming, unless cancelled or preparation fails |

`resident_ready` means an exact successful pipeline handle remains available for
selection. `warmed_only` means a successful transient pipeline was destroyed after
its build. The latter does **not** guarantee a later creation is nonblocking, nor
does it mean the generic shader will be avoided for that draw. Driver cache reuse
and persistence are opaque. The existing 64 MiB driver-cache persistence ceiling
and optional-file quota still apply; live object caps do not bound all opaque
driver/compiler allocations.

Pipeline owners are destroyed before their shader owners. Title/cache replacement,
arithmetic-profile changes and final teardown drain users before releasing the
prepared bank. Exact configuration and consumed-state comparison resolves hash
collisions; an address or host Vulkan handle never enters a semantic disk record.

## Persistence and validation

Semantic files use the independent name
`<title>-uber-opt-precompile-<profile-hash>.bin`. They carry schema, title,
configuration ABI, profile identity, explicit little-endian fields and a checksum.
Bad framing, identity, enum/index ranges, duplicate entries or noncanonical layout
are rejected before compilation. The checksum detects corruption; it is not an
authenticity signature for untrusted shader packs.

The compiler rebuilds semantic descriptions with the current generator. Prepared
SPIR-V cache identity additionally includes the actual compilation policy and the
existing compiler/source identity. Startup honors the user's optimizer setting;
it cannot accidentally reuse a live optional artifact compiled under a different
optimizer policy. Writes use a temporary file, flush and rename under the existing
quota. Recording stays in memory during draws and is saved at drained cache/title
transitions or orderly shutdown. A process crash can lose observations collected
since the last such save.

Stock import independently checks frame sizes, versions, footer ordinals, hashes,
bounded decompression and matching FS/PL source/profile headers. It skips SPIR-V
payloads. Startup logs distinguish missing, rejected, incompatible, cancelled and
accepted inputs; preparation logs distinguish eligible, resident, warmed, failed
and unattempted work. Actual prepared selections are counted separately.

## What this can and cannot achieve

A title with a useful prior inventory can move substantial specialized compilation
out of gameplay. Compatible old CPU `.vkch` descriptions can seed this branch
without first playing the same scenes on it. A completely cold title with neither
source has no known full pipelines to prepare. Subsequent play records additional
states for the next launch. Infrequent scenes outside the resident budget can still
use the generic path even when their driver work was warmed.

This candidate does not scan game archives for shader binaries or fabricate unseen
fragment/attachment/blend states. PICA guest vertex programs alone do not supply
those states. It also does not precompile every translated GPU vertex program into
the new CPU bank. Broader recipe coverage, portable validated recipe distribution
and independently validated game-file vertex discovery remain separate extensions.

Reducing compilation stalls does not directly fix steady-state CPU emulation,
fragment execution, bandwidth, synchronization or a game's rendering defects.
Luigi's Mansion: Dark Moon's earlier slowdown/crash is not declared fixed here.
Driver development and driver changes are outside this candidate.

## Verification and handoff

The new executable checks cover the real recipe codec and production constructor
inputs; bounded/corrupt stock import using real Zstd; production preparation-bank
policy with instrumented driver endpoints; and production device-loss publication,
scheduler cancellation and initial teardown. Deliberately broken variants must
compile and then fail their intended assertions. Recipe sanitizer checks also pass.

Existing compiler-policy, optional-cache lifecycle, workflow/publication and
production TEV-family checks passed locally. Both production fragment generator
translation units compiled. The ready-CPU full/static and related Vulkan fixtures
are extended, but local execution is blocked by the unavailable pinned 6.9 MB
`vulkan_structs.hpp` dependency. The workspace also lacks the complete Android
toolchain. These are outstanding validation gates, not passing tests.

The same-repository draft PR targets the existing gated workflow and checks the
immutable candidate head. The comparison artifact has a distinct name and retains
approved signing checks. This branch cannot publish a normal GitHub release. A
build artifact alone is insufficient: both the shader and Android jobs must pass
for the same head before treating it as a validated test build.

On the Thor, compare the same game, resolution and settings with disk cache on:
first cold launch, then an orderly exit and warm relaunch through the same scenes.
Keep startup preparation time, resident/warmed counts, actual prepared hits,
remaining generic waits, frame pacing, memory, visual parity and crashes separate.
No device installation or gameplay measurements were performed by this change.

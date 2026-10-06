# 0.1.20 architecture review: isolate correctness and memory before expanding optimization

<!-- CodexAstraUlt: Full architecture review, October 6, 2026 EDT. Anchor the source review to c75e544; prospective 0.1.21 work and device acceptance remain distinct from completed source findings. -->

Reviewed source: **`c75e544`**, the 0.1.20 comparison candidate on
`uberhar/codexastra-diag-comparison`; inherited baseline comparison:
**Azahar `9e6f523`**. The owner requests a full architectural review after
persistent Dark Moon faults, memory-related exits and concern about unnecessary
complexity. This review covers active CPU/GPU routes, shader preparation,
allocation/retirement, diagnostic overhead, alternatives and acceptance gates.
It is a source/evidence review, not a declaration that 0.1.20 or the selected
0.1.21 work has passed physical-device testing or APK publication.

**Keep the generic cold fallback, but stop treating the current Combo path as a
validated faster equivalent.** Native has a demonstrable CPU-vertex cost. Combo
substantially reduces that work in Dark Moon yet performs worse, corrupts images
and coincides with falling system available memory. The next comparison must
separate vertex promotion from optional fragment specialization and measure
allocation ownership. Additional speculative optimization is premature.

## Evidence that changes the priority

<!-- CodexAstraUlt: Reuse deduplicated primary evidence; system memory, process memory, worker time and frame time are different measurements. -->

The [0.1.17/0.1.18 device analysis](UBERHAR_LOG_ANALYSIS_0.1.17_0.1.18.md)
contains file hashes, exact line references, settings and timing methodology.
Seven attachments contain six unique text files. All attached gameplay is
0.1.17; 0.1.18 contributes an Android exit record and a 14-line next-launch log.
The owner's report establishes persistent 0.1.18 visual faults; there is no
0.1.18 gameplay body with which to assign those faults to a particular code path.

| Measurement | Interpretation |
| --- | --- |
| Dark Moon Native/2x: 29.642% speed; 322.176 s timed CPU-vertex stage / 413.304 s observed | About **78%** of observed time is inside vertex loading, CPU shader execution, conversion and assembly; this is not pure JIT arithmetic. |
| Native executes 1.751 billion vertex-shader invocations after 2.226 billion within-draw FIFO hits | Reuse already avoids substantial work; deleting the cache would move away from the goal. |
| Dark Moon Combo/2x: 13.924% across 330.576 s of captured windows; about 49.609 s CPU-vertex stage | About **15%**, with slightly different tail coverage, yet worse overall speed. CPU optimization alone does not explain the Combo collapse. |
| Combo system available memory: 9,921 → 1,270 MiB over 330.080 s | An 8,651 MiB decline, not a measurement of Uberhar-owned allocations. Native remains around 9.5 GiB after loading. |
| Both Android reports classify the process exit as low memory | Stronger evidence than the earlier suspected OOM, but no allocation owner or stack. Exact native-log/OS-record correlation has timestamp and process-identity gaps. |
| Sonic/0.1.17: stable accelerated racing blocks average 90.706% at 2x, 42.177% at 4x | Sustained performance is below the target. The 4x run has warm application cache evidence and only 41.078 ms total generic waits. |

The Dark Moon Native timings and invocation counts are at interrupted-log line
2899. Combo stage time is at 5297; it is a cumulative progress record rather than
an orderly final total. The two modes cover different-length scenes, so their
ratios identify work distribution rather than a controlled speedup.

Optional pipeline progression deserves particular attention. The following joins
each health sample to the latest preceding optional pipeline count; it is a
correlation, not per-pipeline allocation accounting:

| Combo process-time interval | Optional pipelines observed | System available memory |
| --- | --- | --- |
| 606.598 → 636.603 s | 32 → 32 | 7,205 → 7,199 MiB |
| 636.603 → 666.607 s | 32 → 50 | 7,199 → 4,959 MiB |
| 696.615 → 726.621 s | 54 → 54 | 4,548 → 4,561 MiB |
| 726.621 → 756.625 s | 54 → 70 | 4,561 → 3,307 MiB |
| 786.628 → 816.631 s | 79 → 79 | 2,346 → 2,286 MiB |

Availability is relatively stable across several pipeline plateaus and falls
around intervening growth. Logged descriptor-pool append events end around 589 s,
well before the later decline. This prioritizes optional pipeline/compiler
residency and opaque driver allocation as hypotheses; it does not prove a driver
leak. At the tail, 99 completed optional builds consume 136.383 s aggregate driver
work, not foreground stall time. System-wide availability can include other
processes, caches and allocations not visible in native allocator samples.

The same log already contains **99 completed GPU pipeline detail records**
(lines 3167–5274), with 99 distinct execution keys, **23 recorded VS identities**,
**78 FS identities** and **99 distinct VS/FS pairs**. Their driver times sum to
136.383 s; median is 1.226 s, maximum 3.710 s, with 78 builds at least one second
and 22 at least two seconds. All report `failed=false`; the hundredth admitted
pipeline has no completion in the captured tail. Of these, 75 have
`specialized_optional=true` and account for 95.153 s; the other 24 account for
41.230 s. A false flag does not necessarily mean generic fragments: unsupported
states can still use mandatory specialized recovery.

Cost is distributed across shader combinations, not one repeated pipeline pair.
VS identity `F6292E0384A10E75` appears in 16 builds totalling 20.945 s;
`38133DF991A1FC29` appears in 20 totalling 19.543 s. The maximum individual build
is ordinal 43, VS `F9C1A7D6531F4B72`, FS `B1625A49D06B2E0F` (line 4076).
All records report two bindings, sixteen attributes and topology 3, but omit full
layout values/hash: equal counts do not prove equal layouts. Their actual shader
stage mask is 3 (VS and FS); a nonzero recorded `gs` identity must not be mistaken
for an active geometry stage. The last module counters separately report 75
optional fragment builds taking 451.895 ms and 23 live VS code generations taking
15.848 ms (5288/5291). These are different compilation phases, not per-stage
shares of pipeline-driver work. The evidence prioritizes driver pipeline work
and residency, but cannot assign its cost or memory to VS versus FS compilation.
The next memory scopes should be joined to these existing pipeline identities.

## What the current architecture actually does

<!-- CodexAstraUlt: Distinguish inherited machinery, intentional alternatives and experimental overhead rather than classifying code by file size or authorship. -->

Base Azahar attempts eligible hardware vertex execution before CPU fallback.
Uberhar's presets deliberately set `use_hw_shader=false`, enable the inherited
CPU JIT and select generic fragment families. Combo then admits selected complete
lists to a separately bounded ready-GPU path. `Native` therefore does **not** mean
the usual base-Azahar accelerated route. See
[`ApplyUberharTestProfile`](../src/common/uberhar_test_profile.h),
[`PicaCore::DrawArrays`](../src/video_core/pica/pica_core.cpp) and
[`ReadyVertexPolicy`](../src/video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h).

The ARM64/x64 JIT compiler implementations are unchanged against the requested
baseline. Program hashes are dirty-cached, and compiled programs are reused.
New JIT diagnostics time compilation misses rather than every vertex. For
supported no-geometry-shader CPU draws, fused input transport loads directly into
shader registers, an exact mapping plan converts output semantics, and a 64-entry
FIFO caches final 96-byte vertices within the draw. This removes the inherited
256-byte attribute-output copy/conversion on hits. Neither guest memory nor
transformed results are cached across draws. Geometry, debugger and unsupported
mapping paths retain the inherited handling.

Generic fragments execute runtime TEV source/operation/modifier selection and an
ordered light loop. Prepared operands, inactive-stage bounds and lazy texture
reads already reduce this work. Sharing shader families lowers compilation
variation, but it does not make runtime interpretation as cheap as a specialized
shader. At 4x, more fragment work can magnify that difference. Exact GPU cost is
not available from the supplied logs.

Current Combo ties optional specialized fragment selection to a successful
GPU-vertex promotion. Covered CPU draws remain generic even after similar
fragment states have appeared. The 96-vertex threshold leaves many small draws
on this route: roughly two-thirds of Sonic draws remain CPU/generic. This is a
real architectural limit, not proof that lowering the threshold is safe or fast.
A future fragment-only promotion path could retain CPU vertex correctness while
using a ready specialized fragment pipeline, but it creates additional pipeline
variants and must wait for the present memory/correctness gates.

| Component | Actual role / cost | Decision |
| --- | --- | --- |
| CPU JIT, exact FIFO and fused input/output transport | Active reference/fallback route; avoids redundant vertex work | **Keep.** Improve pure layout preparation only with exact semantic tests. |
| Single-entry Native mapping cache | Bounded value-only cache; alternating layouts rebuild plans | **Keep.** Small associative replacement is an optional measured optimization, not the primary crash hypothesis. |
| Generic family and pipeline caches | Required to reuse demanded fallback programs and preserve full draws | **Keep.** Count caps are not byte budgets; retain retirement safety. |
| Immutable per-draw constants and command-worker upload reuse | Preserves draw state despite later register changes; suppresses repeated GPU uploads | **Keep.** Never replace immutable snapshots with mutable shared register pointers. |
| Fragment configuration preparation | Reconstructs `FSConfig` twice for covered draws, rechecks support and rebuilds dynamic controls | **Simplify later.** One raw snapshot and exact-value derived-state reuse can remove duplicate CPU work without altering rendering. |
| Full CPU-stage timing | Two clock reads per CPU draw, in addition to sparse detailed samples | **Reduce or make diagnostic-only** after preserving a useful sampled cost signal; no measured device saving yet. |
| Historical 16-dimension shader census | Cached but coupled to required family preparation, including superseded family keys | **Separate from production preparation.** Retain only evidence still needed or make detailed census opt-in. |
| Solid-rectangle compute experiment | Narrow untextured replacement subset; zero eligible draws in these captured titles | **Do not expand now.** Native cannot execute it but still pays classification/census work; bypass that dormant work in a separate cleanup. |
| Legacy CPU-vertex bridge | Disabled by all current presets; retained Custom-mode route | **Defer removal.** Dormant code is not a demonstrated per-frame bottleneck, and deleting it changes Custom compatibility. |
| Ready optional GPU shaders/pipelines | Reduces vertex CPU work but adds compilation, resident driver objects and parity obligations | **Isolate and budget.** Readiness proves availability, not correctness, lower cost or bounded bytes. |
| Existing primary logs and optional exit reports | Retains context and OS low-memory classification | **Keep lean.** Preserve 0.1.19 optional progress delivery and 0.1.20 exact-signature APT repeat bounds. |

Sources: [`uberhar_vertex_input.h`](../src/video_core/pica/uberhar_vertex_input.h),
[`uberhar_vertex_output.h`](../src/video_core/pica/uberhar_vertex_output.h),
[`uberhar_vertex_plan_cache.h`](../src/video_core/pica/uberhar_vertex_plan_cache.h),
[`UseFragmentShader`/`BindPipeline`](../src/video_core/renderer_vulkan/vk_pipeline_cache.cpp),
[`uberhar_tev_preparation.h`](../src/video_core/renderer_vulkan/uberhar_tev_preparation.h)
and [`glsl_fs_shader_gen.cpp`](../src/video_core/shader/generator/glsl_fs_shader_gen.cpp).

Large source/build files do not execute per frame. CMake source lists, comments,
host tests and separate symbol artifacts are not runtime draw loops. Release
build duration includes native compilation, shader correctness work, Android
packaging and validation; duration alone does not establish APK or runtime bloat.
Measure installed artifacts, live allocations, queue growth and repeated draw
preparation. The logged TEV preparation cache is about **213 KB**, not an
explanation for a multi-GiB decline. File splitting can improve maintainability
without materially changing runtime memory or speed.

Existing image creation/destruction through VMA is balanced in the audited
paths, surface unregister/garbage collection waits for completed GPU work, and
`ClearTevFallbacks` drains queued GPU/compiler work before destroying optional
pipelines/modules. Preserve these lifetime contracts. Two inherited follow-up
items are separate from this opening-scene diagnosis: cache `ClearAll` can drop
lookup maps without retiring slot-owned surfaces during save-state handling;
the texture `Handle` move-assignment operator can overwrite ownership, but no
active assignment callsite was found in the current slot-container path. Neither
is selected as a Dark Moon fix without a triggering path and reproduction.

Also retain an inherited **failure-contract audit debt**. `DescriptorHeap::Allocate`
retries indefinitely if allocation returns an unexpected error; only the explicit
out-of-pool and fragmented-pool cases advance/create descriptor pools. Other
failures can busy-retry, not automatically grow pools. The fence-backed semaphore
wait thread treats unsuccessful waits as unreachable, and its destructor releases
free fences before the member `jthread` performs its automatic stop/join; queued
and currently waited ownership needs a complete stop/wake/join/cleanup review.
These require injected-failure and teardown tests before choosing repairs. They
are not established causes of the observed memory decline and should not be
confused with the two narrowly reproduced defects selected below.

## Replace accumulated exceptions with one coherent draw contract

<!-- CodexAstraUlt: Preserve the proven narrow guards, but use them to define common semantics rather than treating more exclusions as a complete architecture. -->

0.1.18's zero-stride guard and 0.1.20's short-stride, default-attribute and
register-alias fallbacks address reproduced CPU/GPU input discrepancies. They
are valid containment, not a demonstrated Dark Moon cure. The architecture needs
one explicit contract for a draw: live input/default semantics, ordered register
writes, preserved shader state, output mapping/rounding, primitive continuity,
viewport/clip transport, fragment state and framebuffer ownership. Optional GPU
execution must prove equivalence to that contract before replacing the CPU draw.

A coherent preparation object should capture exact immutable state once, identify
its resources and route, and either submit one complete draw or return before
publishing framebuffer ownership so CPU recovery can do so. Readiness and
execution identity must be validated against this same snapshot. Keep draw order,
geometry state, winding and partial primitives intact. Avoid independent filters
that silently assume different subsets of the state. Build differential fixtures
for these semantics and matched device images; do not relax a guard merely to
increase promotion counts.

## Selected next comparison: 0.1.21

<!-- CodexAstraUlt: Selected implementation scope, not a claim of successful candidate tests or physical-device correction. -->

Two source-proven lifetime defects are selected for repair:

- **Pool completion refresh:** at the review anchor,
  [`ResourcePool::CommitResource`](../src/video_core/renderer_vulkan/vk_resource_pool.cpp)
  captures `gpu_tick` by value in its search closure. Refresh updates the outer
  value, but later scans still use the stale capture and may grow the pool instead
  of reusing a completed resource. An extracted production fixture reproduces the
  extra allocation. Fix the scan to use refreshed completion, preserving in-flight
  resource exclusion. Descriptor/command capacities can retain those extra chunks
  for their owner lifetime; no device impact has yet been quantified.
- **Failed stream-buffer creation cleanup:**
  [`StreamBuffer::CreateBuffers`](../src/video_core/renderer_vulkan/vk_stream_buffer.cpp)
  may successfully allocate Vulkan memory and then fail binding or mapping it.
  Its retry catch destroys the buffer without freeing that allocation. Preserve
  exact cleanup on every partial-construction path before retrying. This is a
  source-proven failure-path leak, not evidence that it occurred during Dark Moon.

Add bounded allocation evidence through the existing cadence, not another worker
or continuous monitor. Android should report process RSS/high-water at the
existing approximately 30-second interval alongside system available memory and
native heap, with unavailable fields explicitly unknown. Vulkan evidence should
separate VMA-managed allocations from raw stream-buffer device-memory allocations,
and report pool/set/command capacities as **counts**, not invented driver bytes.
Raw stream bytes must use actual allocation requirements, with current/peak and
allocation/free/failure totals. Update those counters on allocation/free events,
not every draw. VMA/explicit-byte totals do not include opaque driver compiler,
pipeline, descriptor or command-storage ownership. Do not label their sum total
GPU memory or assume all such bytes appear in process RSS.

Introduce **Combo Generic**, mode ID **4**, as a diagnostic route. Existing IDs
0–3 keep their meanings. Its intended matrix is:

| Mode | Vertex route | Covered fragment route | Purpose |
| --- | --- | --- | --- |
| Native | CPU JIT | Generic | Stable reference/fallback control |
| Combo Generic | Ready eligible GPU vertices; full CPU fallback | Generic; no optional specialized-fragment warm/select | Isolate vertex promotion with generic fragment semantics |
| Existing Combo | Ready eligible GPU vertices; full CPU fallback | Ready optional specialization where currently eligible | Existing experimental route |

Mandatory unsupported-state recovery remains available in every mode. Pending or
missing optional GPU work must retain the entire CPU draw. Generic families and
required pipelines can still have first-use waits; a new mode does not prove a
fully precompiled bank. Compute eligibility stays aligned with existing Combo.
This is a controlled diagnostic option, not a claim that generic fragments are
faster or a recommendation to change the default.

The two Combo modes share eligibility rules, but **can promote different draws**:
Combo Generic removes the optional specialized-fragment readiness dependency and
instead depends on its generic module/pipeline readiness. Compare selected,
deferred and dependency-miss counts, warmed pipeline populations and scene
coverage alongside speed and memory. A difference between modes is useful
isolation evidence, not proof that only fragment execution time changed.

**Do not add fragment-only promotion in 0.1.21.** It is the larger performance
option after correctness and memory ownership are established; adding another
pipeline population now would confound the investigation. Likewise, do not raise
cache caps, aggressively evict in-flight resources, remove driver workarounds,
approximate shaders or widen topology admission. Eventually optional warm work
needs a resource budget based on observable bytes, residency and in-flight work,
not pipeline count alone. Where driver-owned size remains opaque, explicitly
retain that uncertainty and use conservative admission/measurement rather than
inventing per-pipeline byte estimates.

## Measurements must choose the next fix

<!-- CodexAstraUlt: Each observation maps to a bounded follow-up; no counter by itself proves causation. -->

| Matched observation | Most useful next action |
| --- | --- |
| Native image correct; Combo Generic and Combo share corruption | Compare promoted vertex/output and fixed-state contracts; capture the first divergence. Do not immediately blame fragment specialization. |
| Native and Combo Generic correct; only Combo corrupts or grows memory | Isolate specialized-fragment generation, interface/state identity and its optional pipeline population. Verify equivalent draws before assigning a generator fault. |
| Process RSS rises with optional pipelines while VMA/raw-stream bytes and pool capacities plateau | Investigate opaque compiler/pipeline residency and retention; stop/budget optional admission in a focused comparison. This pattern is a lead, not an exact driver-byte attribution. |
| VMA image/allocation bytes grow with repeated targets/textures or title cycles | Inspect surface-cache identity, invalidation, replacement and fenced retirement; reclaim only resources whose ownership and completion permit it. |
| Raw stream bytes or failures rise without matched frees | Audit partial construction/retries and title teardown using actual allocation sizes; confirm the selected cleanup correction covers the observed path. |
| Descriptor/command capacity rises despite completed work | Verify the refreshed-tick repair and pool reuse; inspect owner lifetime before adding more capacity. |
| Submitted work increasingly outruns completed GPU work | Investigate command/compiler pressure and backpressure; bound work and diagnose stalls rather than growing queues indefinitely. |
| Java/managed heap grows while native/Vulkan scopes are stable | Inspect frontend references, decoded assets and retained activities; Vulkan eviction would target the wrong owner. If Java attribution is unavailable, obtain a focused sample before selecting a fix. |
| System availability falls but process and explicit scopes stay stable | Check shared/kernel/driver accounting and other processes; do not call the difference an application heap leak. |
| Memory plateaus and images match, but Native remains vertex-bound | Optimize exact loader/layout preparation or consider GPU vertex interpretation/validated promotion; retain FIFO and live-input semantics. |
| Memory plateaus and images match, but higher resolution magnifies steady fragment cost | Independently promote ready fragments for CPU-vertex draws or test bounded partial specialization, retaining generic cold fallback and measuring new pipeline residency. |

Sparse asynchronous samples do not line up perfectly. Include process/run identity,
mode, resolution, cache provenance and observation scope in comparisons. A one-time
startup allocation is different from growth after the working set has stabilized.
Do not sum overlapping worker durations or use process peak RSS as a current value.

## Acceptance and sequence

<!-- CodexAstraUlt: Device correctness and bounded working-set behavior are gates before throughput expansion. -->

1. Validate the two lifetime fixes with production/extracted failure scenarios;
   validate mode IDs, policy, full CPU recovery and unsupported-state behavior;
   complete normal shader, native, Android and package checks. Record build and
   signing status separately from device results.
2. Use a **brief matched opening at 2x** in Native, Combo Generic and Combo. Capture
   the first incorrect image and approximate time; stop on the anomaly. No full
   game-suite replay or prolonged crash reproduction is required to select a route.
3. Require no visual divergence in that scene and a stable memory plateau after
   warm-up. Check current allocations/RSS separately from high-water values.
   Repeated title start/stop cycles must return to a bounded baseline, allowing
   documented process-wide caches; ever-increasing baselines fail the gate.
4. Once 2x correctness and memory behavior are established, repeat matched content
   at 4x, then Sonic's same track/settings. Separate normal and accelerated windows,
   discard mixed toggle intervals and keep cache provenance. The goal remains
   playable cold 4x and substantial 2x headroom, not only a faster menu.
5. Require zero skipped draws and account for compilation waits at new effects,
   including generic pipeline creation and CPU JIT encounters. “Ready-only” optional
   work does not imply the whole cold path is wait-free. A zero-wait claim needs
   fresh-effect coverage and actual device evidence.

Host correctness tests and software Vulkan cannot emulate Adreno 740 throughput,
driver allocation behavior or Thor thermal/power policy. Only text attachments
were accessible in this review; no Dark Moon game image was available for an
opening-cutscene run. Retroid's exact secondary model remains unconfirmed. Keep
GammaOS compatibility and graceful absence of optional Android services; do not
make diagnostic support a prerequisite for gameplay or ordinary log retention.

This full review uses **0.1.20 / `c75e544` as its source architecture anchor**.
It does not count failed build retries or unpublished comparison candidates as
shared releases. Update the cadence ledger with the accepted implementation
sequence; review earlier again if route isolation or memory evidence contradicts
these assumptions. Prior reviews and author attribution remain historical evidence.

# Architecture review: Native beta and sustainable headroom

<!-- AstraEH: Full architectural review after 0.0.13, 0.0.14 and 0.0.15. -->

Reviewed 2026-09-27 against released `6b9769567d9680fc987099409e8a6c0bd7ae7ba5`
(0.0.15), based on upstream `9e6f523a57fac9564ac0bf8286db3c3702d301ec`.
This review precedes the 0.1.0 beta update and advances the 0.0.12 ledger after
three published alphas. The owner explicitly endorses beta after four successful
Native/2x Thor runs. Beta describes the maturity of this tested path, not a
completed universal renderer or general compatibility certification.

## Evidence and source audit

The [four-run analysis](UBERHAR_LOG_ANALYSIS_0.0.15.md) records 98.858/99.795%
normal cold/warm speed, 283.379/292.518% fast-only speed under a 400% cap, no skipped
draws or generic failures, and 0.662/0.043 seconds of normal cold/warm generic wait.
Cold wait is about 80% lower than 0.0.14. The same-workload family count is now
four instead of sixteen. Cold startup still contains 386–400 ms worst intervals;
normal gameplay smoothness is user-observed, not proof of zero waits everywhere.

Revalidated production responsibilities:

- `MakeDynamicTevFamilyConfig`, `MakeDynamicTevState` and `WriteLighting` now separate
  runtime light count/operations/LUT controls from global structural choices. ABI 5
  uses 128 bytes; the ordered runtime lighting and six-stage TEV loops stay intact.
- `PipelineCache::BindPipeline` still waits for demanded native pipelines. The
  bounded generic bank (128 families/1024 pipelines), serial build worker, typed
  resources and accurate specialized recovery are necessary correctness boundaries.
- The observed driver has no usable graphics pipeline library/shader object route;
  inherited extended-dynamic-state restrictions remain. No workaround is removed.
- `PicaCore::LoadVertices` already caches VS results in an exact 64-entry FIFO. Hits
  still copy a full `AttributeBuffer`; `GeometryPipeline` then forwards no-GS output
  through a callback that constructs `OutputVertex` anew for every input.
- `ShaderUnit::LoadInput` decodes a batch-constant mapping each invocation;
  `WriteOutput` packs selected registers before `OutputVertex` scatters them to
  fixed semantics. These two output maps can be composed once per batch.
- `OutputVertex` defaults all semantics to one, applies writes in attribute/component
  order, ignores semantic slots outside its 24-word footprint, then takes color
  absolute value and saturates **before** interpolation. These details must survive.
- `PrimitiveAssembler` retains partial list/strip/fan vertices across ordinary CPU
  draws. Geometry shaders can change output banks and winding. Fast-path admission
  must exclude GS and debugging; it must not reset this persistent assembler.

The CPU vertex stage costs about 20–21 seconds per whole run; CPU JIT compilation
costs only 5–6 ms. The source exposes removable transport/conversion work on roughly
64% cache hits. GPU execution, memory bandwidth, guest CPU and scheduling shares
remain uncertain; it would be unjustified to replace all CPU vertex execution on
these measurements alone or promise a 400%/4x outcome.

## Alternatives and implementation order

| Approach | Decision |
| --- | --- |
| Final-vertex FIFO and prepared input/output maps | Implement 0.1.0. Remove proven repeated CPU work without changing shader math, guest state, draw order or the fragment path. |
| Sparse CPU-stage samples | Implement alongside it: rotating sample, one per 128 eligible batches, maximum 8192 per title. Separate loading/remap, execution, conversion and submission; no per-vertex logging. |
| Full GPU vertex interpreter | Keep as a research branch after samples show execution still dominates. Divergence, geometry state, output banks and synchronization need a validated reference before replacing CPU JIT. |
| Reintroduce specialized GPU VS as primary | Deferred. It reintroduces first-use compilation/bridge coverage issues. Consider only a ready, correctness-matched optimization behind the stable path. |
| Collapse more fragment families | Deferred pending broader-game evidence. Only four remain in this test; global bump/shadow/procedural and typed resources need explicit support. Avoid increasing runtime cost to merge unobserved variants. |
| Finite ready program/pipeline bank | Next latency research step when initialization cost is justified. Remaining cold waits are early; measure bounded warming against startup delay and memory. Seen-state replay cannot cover unseen first-playthrough state. |
| More driver compiler threads or a driver download | Not supported by current evidence. Serial generic work avoids contention; extension availability/workarounds remain device-specific. |
| General compute rasterization | Still experimental: the present exact solid-rectangle subset covers zero Awakening draws. Clipping, depth/blend ordering, derivatives and guest memory visibility are prerequisites, not guards to remove for speed. |
| Decode all ROM shaders before play | Still insufficient: runtime code, uniforms, registers and native pipeline combinations are not a fixed list recoverable by scanning a ROM. |
| Skip late draws, reorder primitives, approximate shader arithmetic | Rejected for the beta path. Smoothness must retain visible results and guest semantics. |

## 0.1.0 correctness and performance contract

Prepare a per-batch input-register map and compose the output-mask/rasterizer maps.
Require enough written packed outputs for the requested semantic attributes. On
indexed hits, retain the final 96-byte `OutputVertex`, rather than copying a
256-byte attribute buffer and converting again. Preserve cache size, insertion and
eviction order, shader invocation count, both output banks, defaults, duplicate
maps, overflow slots, color clamping, non-indexed full-width offsets, and the same
persistent primitive assembler/triangle sink. Cache lifetime remains one draw.

Only non-Custom experimental profile draws with no GS/debugger and a complete map
use this route. Ineligible draws retain the existing attribute/geometry path.
Existing GS reconfiguration and setup still happen before admission. No guest
state or save-state format changes; no vertex arithmetic approximation, native
pipeline key, fragment ABI, resolution behavior or timing emulation changes.

The route adds fixed-size host counters and raw sparse timing aggregates; normal
batches compile without per-vertex clocks. Timed submission can include triangle
clipping/collection but does not measure GPU execution. Sampling is not uniformly
weighted by every rendered vertex and stops at a cap; do not extrapolate its sums
as whole-session stage totals. Existing broad stage/frame/wait counters remain
the primary regression measures. New log calls use `AstraEH Log Line` markers.

Required validation: all output masks and both banks versus inherited conversion,
input mapping/duplicates/untouched registers, exceptional float bits, FIFO collisions
and eviction, non-indexed offsets, all four topologies, winding and cross-draw tails,
sampled/unsampled parity, sanitizer coverage, PicaCore integration syntax, existing
host probes, shader/pixel gates and Android package/signature gates. Host tests
cannot establish Thor speed or broader game compatibility. Changes add no Android
permissions, dependencies or services; package/signature continuity is mandatory.

## Goals and acceptance

1. **Smooth, correct first playthroughs:** retain the 0.0.15 Native/2x result, expand
   tested scenes/titles, and eliminate remaining first-use waits where feasible.
   No skipped draws or unvalidated shader approximation.
2. **More sustained headroom:** compare 0.1.0 with the same four 2x runs. Require
   correct images and normal speed at least comparable to 0.0.15; fast-only speed
   and work per input/invocation should improve or remain within run variation.
   Regressions justify reverting/narrowing the CPU optimization, not hiding waits.
3. **Higher resolution:** after 2x validation, assess 3x then 4x separately. The
   target is full normal-speed 4x; a 400% cap at 2x is a useful stress test, not a
   beta prerequisite or a promise of fourfold GPU capacity.
4. **Broader virtual-PICA coverage:** measured, tested expansion of runtime fragment
   support, a ready bank, and later GPU vertices/compute only where worthwhile.
5. **Matched dual-screen delivery:** instrument guest framebuffer generations,
   emulated VBlank/present boundaries and host submission/display timing. Preserve
   intentional guest update differences; pair the matching emulated frame before
   presenting. Thor panels may have separate scanout clocks, so perfect physical
   simultaneity cannot be promised from host submission alone. See the existing
   [display plan](UBERHAR_DISPLAY_SYNC.md); Kid Icarus is an acceptance case.
6. **Model clarity:** retain accurate textures/depth/alpha and assess resolution,
   filtering and effects per title. Avoid global visual hacks based on the earlier
   uncertain Awakening ghosting observation.

The next device test stays Native/2x: normal cold/warm, then 400%-cap cold/warm.
Clear Vulkan cache before each cold run, retain it for the immediately following
warm run, and exit normally to flush totals. New builds may invalidate module
fingerprints, so do not call the first run after an upgrade fully warm. After
validation, normal-speed 3x/4x runs should be separate comparisons.

Local validation completed: 998,282 exact output comparisons, 384 FIFO/assembly
batches, address/undefined-behavior checks, PicaCore syntax, existing host probes,
1,081,344 exact full-fragment color/depth comparisons, 163 Android keys and eight
manifest checks pass. LeakSanitizer cannot inspect processes in this sandbox;
address/undefined-behavior runs pass with leak scanning disabled. The unchanged
fragment programs still run through both Vulkan optimizer modes in the publication
workflow; no new local Vulkan-driver or Thor performance result is claimed.

Count the beta promotion as the first successor build after reviewed 0.0.15.
Default next full review after **0.1.3**, allowed after **0.1.2–0.1.4**, before
starting **0.1.5**. Review sooner for correctness regressions or a failed premise.
Source/tests are completed before Actions handoff; compilation is not polled.

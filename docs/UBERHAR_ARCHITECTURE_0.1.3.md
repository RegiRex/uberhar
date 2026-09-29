# Architecture review: Native headroom and trustworthy run context

<!-- AstraEH: Full four-build review, examining released 0.1.3 before 0.1.4. -->

Reviewed September 28, 2026 (America/New_York), against
`9b331f90fd261db32f8e2671c77be13816e53107`, released 0.1.3.
Upstream reference remains `9e6f523a57fac9564ac0bf8286db3c3702d301ec`.
The 0.1.3 ARM64 workflow passed. This completes the four-successor review after
0.0.15: 0.1.0, 0.1.1, 0.1.2 and 0.1.3. Earlier results analyses did not reset it.

## Decision

Retain the working Native architecture and exact rendering. Prioritize reducing
repeated per-draw host work before another broad shader-family redesign. Ship
the requested automatic settings/cache context as 0.1.4, with rendering unchanged.
Then measure and remove repeated diagnostic/family bookkeeping under exact state
invalidation. Keep loading-time preparation on the first-use roadmap, with coverage
and resource limits as explicit prerequisites.

The evidence supports further improvement, not a guaranteed universal 4x or
full-speed Sonic outcome. A transient full-speed scene is a useful lead, not
proof that resetting accumulated backend state fixes the race.

## Evidence reviewed

The owner's detailed 0.1.2 and 0.1.3 device analyses remain separate project
reports. Scope includes FEA cold/warm 2x and 4x, prior Smash 4x/2x, and Sonic 2x.
Partner attacks expand the latest FEA combat workload; they also prevent claiming
a perfectly matched replay. Smash was deliberately omitted from the latest set.

- FEA 2x remains near full normal speed; 0.1.3 whole-run cold/warm averages are
  99.077% / 99.786%.
- FEA 4x progresses from roughly 81% in 0.1.1, through 88% in 0.1.2, to roughly
  92% in 0.1.3. Runs differ. Late five-second windows still reach about 67–73%,
  so the whole-run improvement does not establish solid 4x across all scenes.
- Generic modules remain four for FEA and three for Sonic. FEA uses 37 generic
  pipelines; Sonic 31. No skipped draws or generic build failures are recorded.
  Warm FEA generic waits total about 47–48 ms, with maximum individual waits near
  4 ms; the cold module runs total about 0.60 seconds.
- Sonic's 80.405% normal-band total includes a long full-speed pre-race period.
  A later 190–380 second selection of complete normal-speed windows averages
  61.444%. That is not an 80% sustained race result.
- One later Sonic window reaches 98.072% while nearby renderer samples show
  fewer draws/vertices per second and reduced broad CPU vertex-stage time.
  Generic compilation had already stopped increasing. The timing is compatible
  with the reported respawn, but no logged input/scene marker identifies it.
- Every in-game phase remains Unknown. User observations about menus/tutorials
  and loading-only fast-forward are valuable context but cannot assign every
  recorded interval to a scene automatically.
- Some generic-cold runs loaded existing driver-cache files. Generic-module
  cold/warm and an entirely empty application disk cache are different facts.

## Source audit and architectural constraints

| Responsibility | Rechecked source and conclusion |
| --- | --- |
| Fragment behavior | `glsl_fs_shader_gen.*`: ABI 7 is 128 bytes. Prepared activity, operand selectors, scales and stage-zero redirects retain operation arithmetic, byte rounding, lazy sampling and delayed buffer order. Global lighting/bump/shadow and typed-resource distinctions remain structural. Keep these boundaries. |
| First-use scheduling | `PipelineCache::BindPipeline`, `GetTevFallback`, `GraphicsPipeline::Build`: Native waits for a demanded compatible generic pipeline or uses accurate specialized recovery. Serial compilation and 128-family/1024-pipeline caps remain. No missing-draw shortcut is selected. |
| Draw-path bookkeeping | `GetTevFallback` computes current and historical family forms, multiple structure hashes and sixteen census-set lookups before returning a known pipeline. With millions of draws, this repeated diagnostic work is a concrete candidate for caching or moving off repeated states. Its isolated cost is not yet measured. |
| CPU vertices | `PicaCore::LoadVertices`, prepared maps, final-vertex FIFO and assembler: reuse stays within one draw; indexed hits preserve exact eviction/assembly semantics. Sonic still has heavy input and invocation volume. Samples do not justify replacing the entire CPU JIT today. |
| Command submission | `BindPipeline` captures each draw's constants and pushes them for each selected fallback. `Scheduler::AllocateWorkerCommandBuffers` resets dirty state at command-buffer boundaries. Future redundant-state suppression must respect those boundaries, utility work, layout compatibility and worker ownership. Do not merely remove uploads. |
| Persistent cache | `LoadOrCompileTevModule`: source, build revision and compiler settings identify generic SPIR-V. Driver files are device/title-specific. `ShaderDiskCache::Init` bypasses specialized disk records in non-Custom profiles. File presence cannot certify compatible generic or driver reuse. |
| Diagnostics | `PerfStats` already records resolution and separates speed bands from whole-run totals. Settings snapshots are instantaneous; phase uncertainty and pause boundaries remain visible. Renderer/vertex reports and frame windows are not perfectly aligned. |
| Presentation | `PresentWindow` performs swapchain acquisition, surface recovery and asynchronous delivery separately from emulated speed. An overlay is neither physical panel timing nor input-latency measurement. Dual-display work remains separately scoped. |
| Device capabilities | Current device records show no advertised/enabled GPL or shader-object route, and extended dynamic state remains disabled. Preserve existing driver workarounds; no extension-based speedup is assumed. |

Vulkan manages pipeline-cache contents in the implementation; supplying prior
data offers reuse opportunities, not an application-visible count of successful
hits. The diagnostics therefore report `provided_to_driver`, with internal state
Unknown. See the [Vulkan pipeline-cache reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCache.html)
and [creation semantics](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreatePipelineCache.html).

## Alternatives and next implementation order

| Approach | Decision and acceptance condition |
| --- | --- |
| Automatic settings and scoped cache context | 0.1.4. Add bounded snapshots without changing compilation, cache deletion, emulation or gameplay timing policy. Preserve missing/disabled/invalid/unknown distinctions. |
| Cache repeated census/family preparation | First optimization candidate after 0.1.4. Measure reuse and skip only work whose complete input is unchanged. Preserve actual rendering keys, caps, first-use observation and title resets. Compare counts against an uncached oracle; limit storage. |
| Suppress redundant descriptor/constant commands | Next candidate if host submission remains significant. Require correctness across command-buffer rollover, state invalidation, specialized/generic alternation, utility commands and failures. Obtain measured redundant-state frequency before a broad rewrite. |
| Bounded known-pipeline preparation during loading | Parallel design step after reliable phase evidence. Serialize reusable descriptions, not Vulkan handles; validate shader/fixed-function/layout/attachment compatibility and cap time/memory. Seen-state preparation is not universal unseen-first-playthrough coverage. |
| More fragment decoding hoists | Retain the two successful directions, but do not assume each additional CPU hoist helps draw-heavy workloads. Prefer a measured change after host bookkeeping is assessed. |
| GPU vertex interpreter / specialized GPU route | Deferred research. It must preserve register precision, geometry/assembly semantics and readiness, and beat the established JIT/cache path in total cost. No per-title switch is justified by the current trace. |
| General compute renderer | Deferred. The exact rectangle subset still has zero coverage here. Full derivatives, blending/depth, clipping and visibility are substantial unsolved prerequisites. |
| Further family consolidation | Low priority with only 3–4 observed generic families. Reducing variants can increase runtime or cold driver cost, as 0.0.14 demonstrated. |
| Respawn-triggered backend/cache reset | Rejected without causal evidence. The current rise coincides with less geometry work; speculative resets could add waits or discard useful state. |
| Approximation, skipped draws, unsafe workaround removal | Rejected. Accurate images and complete ordered draws remain mandatory. |

## Acceptance and measurement contract

1. Preserve the demonstrated Native/2x baseline and exact visual output.
2. Compare similar FEA 4x scenes, including partner attacks where practical.
   Retain late low windows even when menus or loading dominate whole-run averages.
3. Ignore temporary fast-forward and transition intervals for normal-performance
   comparisons unless the owner explicitly requests a speed test. Do not infer
   loading solely from the fast-forward flag.
4. Derive settings and cache facts from the log. Manual notes should add scene
   events, interruptions or visual observations, not repeat machine-readable values.
5. For a future CPU optimization, compare draw/input-normalized work and the
   exact census/key outcomes. Sparse samples are not unbiased whole-run shares.
6. First-use success means moving validated work into real non-interactive waiting
   or removing it, without hiding Unknown gameplay risk. Capture startup overhead.
7. Avoid a mandatory long Smash run every iteration. Retain the earlier evidence;
   repeat its effects workload for rendering changes, regressions and release gates.
8. Source changes need useful host checks and the gated Android workflow. Device
   acceptance remains separate; successful host tests do not certify speed.

## 0.1.4 implementation and validation

Add human-readable settings at launch, observed changes and shutdown. Sample
changes at existing five-second boundaries and on resume, capped at 32 details.
Resolution zero remains explicitly Auto; do not label it 0x native resolution or
claim a fixed physical rendering scale.

Inventory title-scoped generic, driver and specialized file metadata before load,
with at most 8192 directory entries inspected. Report scan duration and errors;
do not read shader contents for classification or alter files. Disabled caches
are not called empty. Inactive specialized records are shown but excluded from
the active Native inventory. Reuse labels come from actual generic hits/misses,
independently of startup presence. Driver failure/fallback reasons remain explicit.

Local host tests and the production cache-inventory test pass, including missing,
zero-sized, stale/present, unrelated-title, non-file/error, capped, disabled and
mixed-reuse cases. Cache tests pass under address/undefined-behavior sanitizers;
core and Vulkan integration syntax checks pass. Shader algorithms and ABI remain
unchanged; full shader/pixel and Android gates run through Actions.

## Review cadence and scope

Next default full review: after **0.1.7** and its device evidence. Allowed after
**0.1.6–0.1.8**, before beginning **0.1.9**. Count 0.1.4 as successor one, with
retries excluded. Review earlier for rendering regressions or a failed premise.

The roadmap records settings/cache automation and normal-speed filtering as
owner requests. Per-draw census reuse is an assistant-selected implementation
proposal. Display synchronization and model clarity remain after the first-use
shader milestone; automatic compatibility profiles remain post-1.0. No calendar
completion promise follows from a handful of improving runs.


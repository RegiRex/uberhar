# Uberhar architecture review — 0.1.32

<!-- CodexAstraLocal: Close the requested three-build review against the released source while retaining independent reviews and the owner-inspection stop. -->
This review covers .30–.32 after the [0.1.29 audit](UBERHAR_ARCHITECTURE_0.1.29.md), at released source `6486198d52df144e184165133c716ffa01a05f26`, tree `c3c8277fae078ab9b19671dd4012e77d2305623f`. It examines changed sections and active execution, ownership and failure contracts, not every inherited emulator or dependency line. The separate [cleanliness review](UBERHAR_CLEANLINESS_0.1.32.md) and [test review](UBERHAR_TEST_REVIEW_0.1.32.md) retain their own findings and limits.

**The speed and independence goals remain unmet.** All four single cold controls are far below sustained 99% normal speed at 2x. Calculated records zero compute dispatches; it still renders these draws through the CPU-vertex/graphics path. Neither 4x nor 0.2.0 is qualified. This is the requested review boundary: **stop for owner inspection; no next release is selected.**

<!-- CodexAstraLocal: Distinguish implemented changes, their demonstrated coverage and the unsuccessful workload hypothesis. -->
## What .30–.32 establish

| Change | Outcome |
| --- | --- |
| .30 ready specialized fragments for CPU vertices | The real software layout/trivial VS, exact shader/state owners and complete generic fallback are covered by helper, worker-failure, alternating-state and original-vertex tests. The initial eight-owner append-only bank filled in menus; selection counts plateaued near opening +12 seconds. |
| .31 adaptive CPU pipeline admission | Eight slots count building, ready, retired and deletion-pending owners. A bounded opening interior records 624,679 additional selections, including 377,310 lit, with 43 retirements/destructions. This establishes use beyond menus, not pixels or speedup. |
| .32 masked/replacement compute pixels | Host tests execute genuine compute writes for nonzero partial RGBA masks and proven replacement-equivalent endpoint blends. The target Calculated run prepared its pipeline but admitted no state and computed no pixels. Functional synthetic coverage did not become observed Dark Moon coverage. |

The new .32 path only reconsiders ColorWrite/Blend blockers. Both RGB and alpha must prove source factor one and destination factor zero through the supported final TEV chain. Newly admitted geometry requires bounded exact projection and identical original shared endpoints; inherited accepts retain their old geometry behavior. A 32-byte immutable packet carries the channel mask. Partial writes preserve disabled bytes and require incoming ShaderRead|Write visibility; full writes retain direct stores. Surface, format, alias ownership, ordered dispatch and graphics invalidation remain required. The area-only selector mixes full-store and partial read/modify/write observations; it does not establish equal costs.

Census schema 2 separates raw masks, effective state admission, later geometry/format rejection and recorded dispatch. A pipeline, six-vertex batch or raw mask is not computed coverage.

<!-- CodexAstraLocal: Report only complete scoped measurements, with separate vertex denominators and no sparse extrapolation. -->
## Device result and remaining gap

Each .32 method received one File 1 reset, zero application-cache opening at Vulkan 2x, limiter 100% and CPU 100%. Source/settings/counter checks and normal returns pass; no renderer draw skip or required-fallback failure is reported. Driver-private cache state remains unknown. Each row uses 40 complete frame-report windows within opening A to first Back; delayed exit/navigation tails are excluded.

| Method | Mean normal speed | p05 window average | Worst reported interval | Separate inclusive LoadVertices wall |
| --- | ---: | ---: | ---: | ---: |
| Calculated | 20.957% | 15.946% | 310.737 ms | 83.665% |
| Native | 20.964% | 16.042% | 285.255 ms | 83.831% |
| Full Combo | 23.185% | 19.986% | 387.873 ms | 76.079% |
| ComboGeneric | 19.455% | 13.791% | 345.083 ms | 58.407% |

Every accepted frame window is below 99%. Reporting windows, visible progression, recording overlap and clock/log-emission placement differ; these means do not establish causal mode/version gains. The .31 means were 20.666/20.871/22.792/19.439% respectively, also far below target. p05 is wall-weighted over reporting-window averages, not individual frames.

The independent all-batch vertex brackets are Calculated 165.711156/198.064744 seconds, Native 169.524994/202.221162 seconds, Combo 145.999848/191.905931 seconds and Generic 113.648067/194.579561 seconds. LoadVertices includes preparation, input loading, shader execution, FIFO/output/assembly and intervening scheduling. It ends before DrawTriangles and Vulkan upload/bind/submission. It is not exclusive JIT CPU time; the remaining wall time is not GPU time. Sparse nanosecond stages and missing optional reports are not expanded into shares.

Full Combo records 607,569 CPU-specialized selections between bounded opening-interior snapshots, including 404,775 lit, with 42 attempts/retirements/destructions and no failure/stale-token delta. Final totals of 64 attempts, 56 destructions and 8 ready owners include the exit tail; the first delivered exhausted-budget observation is after first Back. These are selection/ownership counts, not completion, actor or savings measurements. ComboGeneric still has 678,365 ready GPU-vertex batches over its lifecycle and zero optional fragments: it is neither all-CPU nor an isolated CPU-fragment toggle.

Consecutive review found 0/2/0/2 isolated single-frame moon events in Calculated/Native/Combo/Generic respectively, all below the owner's priority threshold. Ghost clips contained 4/2/0/0 isolated single-frame chalkboard-target disturbances; no prior dense irregular patches were identified in covered phases. Combo retains only the near-shot tail but reaches the later close electrical ghost, which Generic does not reach. These unlike recorded populations do not measure comparative event rates, physical persistence, renderer cause or whole-title correctness. Exact frame coverage remains in the test review.

At opening +40/+165, KGSL grows from 2518.8 to 2955.2 MiB in Combo and from 2493.7 to 2716.8 MiB in Generic. Fixed CPU-owner counts do not bound opaque driver allocations. The .31 delayed same-process return to about 13 MiB establishes later recovery only; no .32 exit sweep was requested or claimed. Longer active growth, pressure and crashes remain unqualified.

<!-- CodexAstraLocal: Explain why raw-state opportunity was insufficient before proposing another admission change. -->
## Review of the .32 decision

Raw mask 1026 supplied a real ColorWrite/Blend population, mainly in setup and an opening-boundary interval. It justified a bounded correctness investigation, but did not prove nonzero enabled bytes, replacement factors/equations, supported final TEV alpha, rectangle geometry or useful target pixels. No retained CPU packet provided those facts; the 48 older captures were already-GPU-ready unlit draws.

The choice had a sound host correctness plan but weak evidence of useful title coverage. In .32 Calculated, lifecycle counters independently close at **6,646,279 considered = state-rejected = graphics-fallback draws**. Geometry, format, eligibility, recorded compute draws and pixels are zero. Delivered raw1026 top rows include at least 1,126,850 six-vertex draws, yet none passed effective admission. The remaining rejection could be zero writes, logic/blend behavior or an unsupported endpoint/TEV proof; its exact subreason is unrecorded. Geometry, storage, readiness and selection happen later and cannot explain this failure.

Five complete delivered opening census intervals cover 1,239,203 attempts/197,670 six-vertex batches, with zero admissions. Missing summary 10 omits 265,938 opening attempts; missing 23 is an exit-tail gap. Orphan detail rows are excluded. Final cumulative equality establishes zero effective admission despite those missing mask intervals. Native retains its original raw gate; Combo and Generic also record no compute dispatch. Successful host kernels therefore establish no target benefit here, and loosening geometry would address the wrong rejection.

The Native decline from .29's 27.869% to .30/.31/.32 around 20.9% remains unresolved. Effective configuration matches, Native bypasses optional CPU-fragment work, seven released ARM hot bodies retain their opcode/register structure, and changed Draw/BindPipeline callers retain stack allocations. Common branches, object layout, code/cache placement and external conditions still differ. Fresh-process and placement observations do not identify a cause. No fragment-optimization blame, numerical regression or predicted fix follows from this narrowing.

<!-- CodexAstraLocal: Preserve live-state, independent arithmetic and all ownership horizons when evaluating larger changes. -->
## Contracts future work must preserve

CPU draws keep one live ShaderUnit, exact FIFO miss order, fresh pinned memory/bounds, input alias/default behavior, both output banks and persistent primitive tails. Later misses can overwrite cached outputs, so assembler snapshots cannot become unowned borrowed references. Draw-local JIT contexts borrow live uniforms and do not survive their draw.

GPU promotion retains topology, input, W, quaternion and readiness guards. Possible-write W tracking is not definite initialization. The restricted flow certificate accepts none of the 48 retained packets: that is its conservative proof limit, not proof every title shader needs carry. Interpreter, x64 JIT, A64 JIT, Mesa and actual FP environment remain separate numerical obligations. The precise-dot policy does not certify whole-shader equivalence.

CPU fragment reuse keeps exact execution/layout/attachment/profile and owner identity, plus immutable queued descriptors/offsets/constants. Generic→specialized→generic transitions invalidate overwritten state. Retirement requires command-worker drain, final compiler release and completed GPU use; the post-draw stamp covers stream-map Flush after binding. Generation tokens and drained bind-pointer invalidation prevent stale reuse, and deletion-pending slots remain occupied until release publication.

Attempts are charged before allocation, failed keys cannot retry, optional errors preserve full fallback, and existing ready hits survive exhausted budgets. The 64 CPU attempts limit future adaptation until reset; eight owners/shared caps do not bound driver bytes. Profile mismatch falls back without implying an immediate drain. Source-derived cache configuration and all existing release gates remain necessary.

<!-- CodexAstraLocal: Rank bounded falsifiable follow-up work without beginning a new release or capture before owner inspection. -->
## Next decisions after owner inspection

1. **Prioritize the CPU vertex path with a complete-runner experiment.** Its large direct wall bracket warrants investigation, not a guessed shader/loader share. The input plan already decodes format/element readers once per draw. A private executor for at most four complete input recipes would instead remove repeated per-attribute reader dispatch, retaining exact conversions, live fetch order, aliases, bounds and the original FIFO/JIT/output/assembler. Unknown recipes use the existing path. Include preparation, churn, hit-heavy cases, effective LTO and code size; preserve every output/state/tail and require stale-data, alias, W-fill and escaped-index defects to fail. Bound the screen to 96 cases/0–4096 inputs and 90 seconds of paired whole-runner timings. Actual CPU-recipe relevance and a small positive complete-path result are required before promotion; host gains cannot predict Thor speed.
2. **Require exact failed state before another narrow pixel extension.** A future default-off prerequisite could retain at most eight rejected six-vertex cases and 64 KiB in one window of at most 10 seconds: original 88-byte vertices, the small consumed register/state block, viewport/flip and rejection substep. The unchanged classifier must reproduce each failure first. Texture/lighting-dependent results cannot be inferred from that packet. This is not authorization for another .32 run or a profiling-only alpha.
3. **Develop ordered device-resident vertices as the larger Calculated direction.** First extend the conservative dependence proof only where actual interpreter scope is modeled, then obtain a relevant CPU-fallback cohort. Require live-input/carry, arithmetic, triangle/quaternion and visibility equivalence before parallel execution. GPU vertices feeding hardware fragments remain a hybrid architecture. Moving already-promoted draws does not remove the remaining CPU workload.
4. **Keep full ordered compute rasterization a separate major project.** Exact clipping, interpolation/derivatives, textures, depth/stencil/blend, alias order and bounded recovery are prerequisites. A draw that has partly modified its target cannot simply fall back and replay. The current rectangle kernel is not that renderer.

Frozen alternatives remain unselected: typed primitive submission had mixed small host gains and code growth; narrow A64 raw-load residency enlarged safe entry handling for little relevant static reduction; integer quaternion comparison was costly and cannot recover bits already changed upstream. Absence of relevant coverage or complete-path benefit is a reason to decline a candidate.

<!-- CodexAstraLocal: Retain inherited source hazards as unresolved contracts rather than asserted causes of current images or slowdown. -->
## Inherited issues still open

| Contract | Remaining proof |
| --- | --- |
| DescriptorHeap::Allocate | Unexpected non-success/non-pool errors lack a bounded terminal branch. Exercise injected errors and owner cleanup before changing recovery. |
| MasterSemaphoreFence teardown | Free-fence destruction precedes member-thread join; completion publication precedes fence return. Prove shutdown/interleaving and submit/wait failures. Normal exits alone do not close this. |
| RasterizerCache::ClearAll / Vulkan Handle replacement | Lookup clearing and slot ownership differ; raw handle move assignment can overwrite owned fields. Trace actual repeated/live-owner callers before claiming a leak or changing lifetime. |
| Capture fragment-route label | A false optional flag can mislabel mandatory specialized recovery as generic. Retained selected specialized packets are unaffected; diagnostic correction must not change routing. |
| Accelerated Strip/Fan tails | The inherited tail-transfer TODO remains; broader promotion requires explicit cross-draw tail/winding equivalence. |
| A64 return-frame/sentinel concern | Static reasoning is not executed backend proof. Generated ABI remains unchanged; the stopped runtime harness is not resumed by this audit. |

None is established as the cause of current slowdown, images or memory growth.

<!-- CodexAstraLocal: Distinguish released execution, local comment-only attribution closure and the explicit stop. -->
## Closure and stop

Local native/CTest and full host gates pass. The original-vertex compute suite retains 90 pairs, four ordered sequences, eight pixel-failure controls and four validated Vulkan modules; inherited compute tests remain. Host GL/modules/command adapters do not execute Adreno ownership or prove title coverage. All 22 required CI steps pass, with CI signing and local asset/install verification and four consolidated normal-return controls. Detailed visual, diagnostic-loss and delivery limits remain in the separate test review.

The separate cleanliness review identified three Android purpose-comment gaps. Six local standalone comment lines now cover beginRun/endRun and their NativeEmulation lifecycle pair, preserving every previous source byte and historical marker. Independent insertion-only proof passes. Those two local files were **not rebuilt or published** and are not part of the installed 6486198d APK; this attribution closure is separate from runtime qualification.

Together with the separate reviews, this closes the .29→.32 review cycle. Unobserved scenes, longer gameplay/memory growth, retained Fire Emblem Awakening beta coverage, sustained 99% and 4x remain open. **Stop development here for owner inspection with this resumable backlog; no .33 or 0.2.0 claim follows.**

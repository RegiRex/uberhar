<!-- CodexAstraLocal: Reconcile concrete architecture proposals with their executed evidence, retaining useful negative results and identifying work that has never received a hardware test. -->
# Architecture experiment ledger through 0.1.40

**The hypotheses have not all been tested.** Several changes shipped and were
exercised on Thor; several correct private implementations have only host cost
evidence; others remain source designs. In particular, cached uniform/LUT
last-use protection was identified earlier but was not fixed by .38's CPU
geometry ordering change. It is now the first authorized implementation and
hardware-test candidate. No observed moon or ghost fault has been attributed
to it.

This ledger inventories the concrete .35–.40 CPU effort, its secondary ARM and
Calculated work, and the named follow-up designs. It reconciles retained source,
test reports and measurements; writing it did not rerun those tests. It does
not claim that every historical conversation suggestion became an experiment.
The separate [architecture](UBERHAR_ARCHITECTURE_0.1.40.md),
[cleanliness](UBERHAR_CLEANLINESS_0.1.40.md) and
[device review](UBERHAR_TEST_REVIEW_0.1.40.md) retain their own scopes.

In the tables, **host** means local execution; **A64** means actual ARM64 code
under QEMU unless explicitly called Thor; **Thor fixture** means a standalone
device experiment; **title** means the installed release ran the game. A
source/model check is identified separately. A title run after several changes
does not isolate each change's performance. “Not selected” records an experiment's
decision, not a claim that the underlying idea can never help.

<!-- CodexAstraLocal: Associate integrated work with exact released revisions rather than an uncommitted prototype or an intended version. -->
## Released source anchors

| Release | Commit | Relevant integrated scope |
| --- | --- | --- |
| [.35](releases/0.1.35.md) | `7d2fb88864499dffe67bebc4bd8a9f7de3dc666b` | Independent CPU vertex batches, persistent workers, exact JIT identity and ARM64 call-contract repairs. |
| [.36](releases/0.1.36.md) | `b438a453925d27b991ecaedda89e2a66e9e51734` | Selected-output certificate under explicit backend isolation; exact partial stores. |
| [.37](releases/0.1.37.md) | `f6e0d88842ff8204e112a7368035e4565848146d` | Futex transport, ordered bulk assembly, renderer ownership guard and sampled phases. The corrected publication repaired a test's include ordering; it did not replace the runtime design. |
| [.38](releases/0.1.38.md) | `4902aa2333ae536c786168ea4d6f2776ef296f83` | Packed register getters, CPU stream reservation ordering and opt-in shell profiling. |
| [.39](releases/0.1.39.md) | `ebd770c713d2d335494578711f3b4f17e168eea7` | Owned CPU draw packets, immutable JIT leases, coherent deferred upload and conservative admission. |
| [.40](releases/0.1.40.md) | `7bc71c5c9c7e10a39558084392330faa884889ba` | Static TEV/runtime-lighting fragment tier, bounded independent cache partitions, shared optional compiler lane and removal of unused temporary recipe selection. |

<!-- CodexAstraLocal: Separate production correctness evidence from private cost observations and from actual title effectiveness. -->
## CPU execution, preparation and ordering

| ID / proposal | Implementation and finite tests | Thor evidence | Outcome and next action |
| --- | --- | --- | --- |
| C01. Distribute independent vertex invocations over available CPUs | Shipped .35. Host certificate/FIFO/batch/pool controls; actual A64 return, shared-JIT and FP controls. Default certificate retains arithmetic-read independence. | Native and Combo title runs from .35 onward. Initial useful worker coverage was limited; later .36 broadened it. No isolated transport speed claim. | **Implemented, useful work observed.** Preserve exact FIFO, state and ordered submission. Actual process CPU use still falls well short of the original broad multicore aspiration. |
| C02. Exact compiled-program identity and ARM64 return/FP contracts | Shipped .35. Exact program/swizzle identity, mutation controls, live compiled owners and A64 ABI/FP tests. | Exercised by shipped title paths; no separately identified title fault or target cost experiment. | **Implemented correctness foundation.** Keep identity/lifetime and backend-specific FP requirements when changing scheduling. |
| C03. Replay only the exact carried-state slice | Private implementation; host admission and actual A64 full-state/status comparisons, including sensitive lane/reset defects. Nearly one million shader calls and more than one million replay calls in the retained corpus. | No Thor fixture or title integration. | **Correct private alternative, not selected.** Replay/recording is extra work. The separately proved C04 contract removed its need in the eligible backend. Retain for a future backend that actually observes the carried state/status. |
| C04. Prove selected output instead of dead temporary/status equality | Shipped .36. Actual consumer closure, guest FPSR isolation, default-contract regression, 107 synthetic admission cases, 2,785,280 A64 calls and 168 composed draws with defects. | Native/Combo .36 onward exercise the qualified A64 path. Wider worker admission is observed; the package run is not an isolated speed attribution. | **Implemented.** Keep both constructed-backend capabilities, traps/observer/memory guards and the unchanged stronger default contract. |
| C05. Narrow masked A64 shader stores | Shipped .36. Actual emitted writeback/raw-state/status differentials and omission controls. | Executed in release title runs; no standalone Thor store-cost measurement. | **Implemented correctness-preserving reduction.** Do not infer per-title savings from fewer emitted instructions alone. |
| C06. Replace per-grain ShaderUnit initialization with per-job slots | Private actual input/JIT/FIFO/output comparison; host paired cost matrix over 256–4,096 invocations and 1/2/4/8 participants. NDK assembly confirms a substantial initialization, not its elapsed cost. | No Thor timing or integration. | **Not selected after mixed/slower complete-path host results.** Revisit only with a materially different ownership/cost design; do not count this as untested. |
| C07. Sleeping per-worker CV generation plus atomic completion | Private host/A64 pool correctness and one host dispatch matrix. Small/light cohorts could be slower; results were mixed. | No Thor cost or title integration. | **Not selected.** Original shared-CV behavior remains the portable fallback; this proposed replacement is not the shipped futex design. |
| C08. Direct sleeping futex dispatch/join | Shipped .37. Host/A64 old API, forced-CV, wrap, delayed wake, unavailable syscall and failure controls. One paired host dispatch matrix supported selection; actual JIT composition also passed. | Installed .37 and later title runs. No isolated Thor A/B transport timing. | **Implemented.** Startup fallback and explicit terminal-error policy remain. More wake calls are a potential cost, not evidence that removing them alone fixes throughput. |
| C09. Bulk ordered List/Shader assembly | Shipped .37. Actual assembler state, winding, tails, callback-throw behavior and composed input/JIT/FIFO/88-byte output. One complete-path host matrix improved cohort medians. | Exercised by .37 onward. No isolated title A/B. | **Implemented.** Strip/Fan retain their scalar path; the bulk getter and sink's non-observation contract remain required. |
| C10. Admit batching only for a sink that defers guest-memory writes | Shipped .37. Actual accelerated/software capability controls; source traces show software AddTriangle can write aliased guest RAM. | Installed title path uses the qualified sink. No performance claim. | **Implemented safeguard.** Never replace actual capability with a mode label or assume future renderer preparation is equally safe. |
| C11. Sample plan/pool/owner/join/submit phases | Shipped .37. Actual counter placement and sampled-clock controls; ordinary observations retained. | .37–.40 report real sampled phases. | **Implemented measurement, not a speed fix.** Sparse wall sums cannot be multiplied by the sampling stride or treated as the whole-frame CPU budget. |
| C12. Typed per-grain dispatch and stack-owned per-participant contexts | Private host/A64 actual input/JIT/FIFO/assembly composition: 3,028 comparisons per architecture, carry/freshness/drain defects. One host grain/size/participant matrix tested typed dispatch separately from context lifetime. | No Thor cost or title integration. | **Not selected after mixed/negative complete-path costs.** No grain change was selected. This supersedes earlier notes calling the context experiment pending. |
| C13. Shared futex wake cohort with registration/close protocol | Private host/A64 forced registration, late-notify, generation/stop and drain controls; actual JIT/FIFO/88-byte composition. One host 102-row/8,160-draw cost matrix was mixed. | API29 binary qualified; the attempted Thor matrix refused its eight-CPU precondition **before timing**. A later no-work context probe is not a performance result. | **Qualified private alternative; target benefit untested.** Reuse the frozen test design only with a deliberately reviewed actual CPU allowance. No affinity workaround or eight-core assumption is justified. |
| C14. Submit an acquire-ready ordered prefix while remaining vertices finish | Private original-pool host/A64 composition: 2,470/2,486 draw comparisons, six meaningful readiness/order/rollback/status defects. One 72-row host matrix favored many eligible cases but not small/owner cases; retained useful ranges crossed parity. | No Thor cost or title integration. | **Private candidate, not promoted.** Requires preserved assembler/vector/FP rollback and actual complete-path target benefit. It overlaps only the ready prefix, not all owner work. |
| C15. Direct packed register decoding for three getters | Shipped .38. Host/A64 590,400 scalar checks, 8,192 actual loader constructions and 24,576 live loads, plus three defects. | Twelve unbound Thor ABBA constructor runs across six synthetic layouts favored the candidate. CPU migration/frequency and empty-layout differences limit attribution. Title runs also occurred. | **Implemented.** The target fixture supports constructor cost, not a measured full-title gain. No layout cache or new register ABI was introduced. |
| C16. More compiler threads, a larger ready bank or relaxed lifetime attempt caps | Source topology/counter review: Vulkan already has separate pipeline/shader pools plus optional workers. Existing full-tier attempt exhaustion and later ready reuse were observed. No implementation or paired cost test of larger limits. | Existing title counters only. | **Untested capacity proposal.** First measure actual blocked compilation, useful recurrence and memory/retirement cost; more thread/owner counts alone are not the objective. .40 explicitly preserved full-tier capacity while adding its separate tier. |
| C17. Small multi-entry semantic input-plan cache | Source design distinguishes a pure exact-value plan from live mapped input. Rebuild counters exist, but no key-reuse-distance trace. All sampled setup was only part of the observed vertex/draw cost. No new cache or benchmark. | Existing title counter observations, not a candidate trial. | **Unimplemented.** A bounded exact recurrence/cost discriminator is needed before a complete-path cache comparison. Never cache guest pointers/default values as a pure plan. |
| C18. Overlap one draw's vertex work with owner Vulkan preparation | Source investigation established that texture/framebuffer validation can write guest RAM. The safe owned-input/owner-preparation direction informed C19; a separate existing-pool same-draw implementation was not tested. | No independent same-draw A/B. | **Partly realized through the different C19 seam.** Any broader overlap still needs complete immutable reads, consumed-once preparation and exception drainage. Source feasibility is not a performance result. |
| C19. Owned whole-draw CPU packet, with resource preparation synchronous and output consumed by VulkanWorker | Shipped .39 after host/A64 output/assembler/effect/failure controls and a 96-row host complete-stage cost matrix. Small/light/isolated cases lost, so admission was narrowed to substantial 96–255-input work with actual miss, arithmetic and byte limits. | .39 and all four .40 cases execute real coordinator work. Interior completed packets equal waves and nonempty boundaries, max wave one, auxiliary work zero. | **Implemented; useful multi-packet fan-out not demonstrated.** The result identifies the next dependency question. It neither proves no overlap nor establishes which reconciliation site forces one packet. Preserve large independent draws' existing parallel path. |
| C20. Immutable compiled JIT lease | Shipped .39. Actual engine/code survives source/cache/owner destruction; exact entry/program/swizzle identity, stale owner and invalidation controls. | Required by real deferred title packets. | **Implemented foundation.** New cache/scheduling work must retain owned code rather than a borrowed callable or hash-only identity. |
| C21. Coherent deferred upload with generation-bound reservation | Shipped .39. Actual source extraction, mapping/sealing, stale lease, wrap/tick and defect controls; composed real output/failure tests. | Real .39/.40 deferred draws. | **Implemented for the admitted coherent path.** It is not a general noncoherent asynchronous upload API and does not close cached UBO/LUT reuse. |
| C22. Preserve no-framebuffer geometry and final assembler state | Shipped .39. 144 retention scenarios, 3,325 checks, actual 88-byte/quaternion comparisons and omission defects; ordinary/deferred order controls. | Installed title route, with no isolated no-target title trigger established. | **Implemented.** Empty hardware storage is not equivalent to a logically empty primitive assembler. Keep no-target-to-valid-target behavior in later concurrency tests. |
| C23. Reuse capture validation/recipe work on fallback | .40 removes only unused recipe selection from the temporary checked plan; actual packet recipe preparation remains. Existing queue tests and native build pass. General reusable prepared fallback is not implemented. | .40 title runs; no isolated microbenchmark or gain claim for the one flag. | **Narrow cleanup implemented; larger proposal open.** Use one owned prepared result only before an intervening guest-memory/state boundary. Compare accepted and refused full paths. |
| C24. State capsules, bounded packet slabs and cheaper tiny-draw capture | Concrete source design only; exact uniform/default mutation epochs, slab lifetime through command release and hard credit bounds specified. The earlier cost matrix tested the old allocation/capture path, not this new design. | No candidate fixture or title run. | **Untested.** Implement reusable preparation/storage first under existing admission; prove stale-state and delayed-release defects, then assess tiny draws. Merely lowering the 96-input floor repeats a tested losing path. |
| C25. Logical-empty deferred tails to admit adjacent independent packets | Source contract only. Requires reason-coded reconciliation and exact tail/failure/IRQ/list-return/readback ownership; no implementation or cost test. | Max-wave-one observations motivate it but do not select its cause. | **Untested, conditional next CPU architecture.** First identify actual barrier/readiness populations. Preserve hard guest-memory boundaries and never wait to manufacture a wave. |
| C26. Parallel packing of independent completed triangles | Source analysis: each triangle's quaternion decisions and 3×88-byte destination are independent. No implementation or full-path cost experiment. | None. | **Untested.** Price packing first; use the existing finite CPU allowance, avoiding a nested full-size pool and retaining exact arithmetic/order. |
| C27. Include actual hardware-vertex transport in the parallel/serial baseline | Private .36 host replay uses the actual HardwareVertex constructor, quaternion decisions and AddTriangle append body: 48 cases and seven paired rounds. The larger retained shape kept a host parallel advantage; small shapes retained owner-only execution. | No Thor fixture; no new production change was represented by this harness. | **Baseline experiment completed.** It validates a more realistic cost scope, not a newly optimized sink. Changed checksum placement prevents subtracting an earlier proxy to infer AddTriangle cost. |
| C28. Pipeline next-chunk shading with current-chunk submission | Source design only: two immutable result generations, exact FIFO carry, begin/join lifetime and failure drain would be required. No implementation or paired cost experiment. | Existing counts establish many single-chunk draws, not a timing ceiling. | **Untested.** It differs from C14's ready prefix within one chunk and C19's owned whole draws. Establish a material multi-chunk population before paying extra buffer/coordination cost. |
| C29. Remove proved dead shader instructions under the selected-output contract | Source design only. The existing certificate proves independence, not permission to delete instructions. No backward-liveness emitter, distinct cache variant or execution/cost test was implemented. | None. | **Untested.** Require unioned live dependencies across every reachable context, preserved control/address uses, separately qualified status observability and exact selected output. Do not silently narrow standalone Run or remove apparently unused SIMD lanes without a cost comparison. |
| C30. Fuse shading and triangle packing in one worker phase for unindexed complete lists | Source envelope only: no-GS, empty matching List/Shader assembler, no winding, one invocation per original input and disjoint backend-owned output slots. No implementation/cost experiment. | Retained aggregate title counts do not establish this exact unindexed eligibility. | **Untested specialization.** Compare actual input/JIT/conversion/quaternion/88-byte output and final tails before measuring. Indexed/FIFO and other topologies need their original complete route. |

<!-- CodexAstraLocal: Keep the two different stream-lifetime issues separate and make the current hardware experiment's falsification target explicit. -->
## GPU ownership, shader preparation and timing

| ID / proposal | Implementation and finite tests | Thor evidence | Outcome and next action |
| --- | --- | --- | --- |
| G01. Reserve CPU geometry before final render-pass/pipeline binding | Shipped .38. Original source-derived current-tick wrap counterexample; corrected 24-case/768-assertion recording fixture, late-Map/early-Commit defects and actual native TU compile. | .38–.40 title runs still contain visible faults in some reviewed sequences. No title-level cause established. | **Implemented bounded fix.** Covers CPU geometry command order; it did not fix cached UBO/LUT last use or every accelerated/presentation ring obligation. |
| G02. Protect cached uniform/LUT bytes through the last consuming draw | Local .41 patch implements per-ring final draw-use stamping and a wait before wrap. Source-derived tests pass 12 cases/1,082 checks and six byte-sensitive defects. Real Lavapipe passes six cases/18 draws/145 checks; three protection defects expose actual fragment-output mismatches. | **No corrected title result yet.** After owner resume: one cold Native and one cold Combo, Vulkan2x/100%, timings ON, owner visual judgment. No install/title test is authorized before that resume. | **Implemented locally; final integration/release qualification pending.** The concrete old-allocation-watch defect is reproduced and corrected within the finite tests. Preserve original watches, invalidation refresh and actual-enqueue placement. Host Vulkan is not Thor or a moon/ghost cause proof. |
| G03. Combined accelerated vertex/fixed/index reservation and presentation-ring lifetime | Source concerns retained separately: several Maps can share a ring before one draw; later Map/pass changes can invalidate earlier assumptions. No combined geometry reservation fix is established by G01/G02. | No targeted device fixture or identified title trigger. | **Untested separate scope.** Trace all same-draw ranges and final bindings; add a distinct-payload wrap/consumer control before selecting a repair. Do not claim the cached-buffer fix covers it. |
| G04. Static TEV structure with runtime lighting and values | Shipped .40. 805 plan checks; 816×256 TEV/fetch comparisons and three emitted defects; 128 Vulkan module validations; 4,224 CPU-interface groups and 128 optimized binary groups. Partial/generic finite color/depth outputs are exact. | Both Native and Combo actually select partial shaders; Combo retains full selection. Four .40 timing cases are observations, not an isolated .39/.40 shader A/B. | **Implemented and exercised.** Output-preserving CPU preparation reduces GPU decisions; complete-frame benefit and opaque driver memory are still unisolated. |
| G05. Shared optional compile lane with independent full/partial admission and retirement | Shipped .40. Actual worker/optimizer/completion tests plus modeled pipeline/tick/promotion/budget/retirement controls. Legacy full policy and Native GPU refusal remain tested. | Partial and full selections appear in real .40 counters; no optional-ready wait is introduced by this policy. | **Implemented.** Full-ready wins; full promotion continues while partial renders. Object/attempt caps are not resident driver-byte caps. |
| G06. Static lighting structure, more prepared shader families or packaged fixed modules | Source proposals only beyond G04. Need exact joint costly-state coverage, dependency/precision checks and finite budgets; no extra lighting tier or prebuilt-family implementation tested. | None as separate candidates. | **Untested.** Select one demonstrated expensive structure, preserving runtime values and avoiding an uncontrolled combination of cache axes. |
| G07. Simulate 3DS GPU Timings ON | Existing setting, not a .41 toggle implementation. Source confirms Native/Combo do not override it; four cold .40 cases strictly bind both effective log snapshots and persisted flags, delay zero. | Native OFF/ON and Combo OFF plus bounded Combo ON observed. ON changes guest GSP completion scheduling; measured emulator and submission rates rise in these unlike spans. | **Setting control executed; no new default selected.** Graphics faults remain and the interrupted fourth run has unknown first Back/exit input. This is not additional CPU parallelism or a graphics cure. |
| G08. CPU tile ownership for complete shadow work | Source design only. Packed depth/shade update order matters; software f16 arithmetic is not an exact GLSL replacement. Requires known initial image, clipping/coverage/texture semantics, exact same-pixel order and ordered GPU handoff. | No CPU-shadow implementation, fixture or title result. | **Untested larger research.** First find a substantial closed epoch that avoids repeated readback/upload. Spare cores alone do not justify moving pixels to CPU. |
| G09. Skip a provably depth-rejected shadow CAS update | Source hypothesis only, conditional on all writers preserving the monotone-depth invariant and correct visibility. No shader patch or contested-pixel execution test. | Existing interlock-unavailable route evidence; no retry or GPU-time measurement. | **Untested local GPU-work reduction.** Prove original/candidate packed output and final sampling under clears, aliasing writers and contention. A generic atomicMin or new-value-equals-old shortcut is not this proof. |
| G10. Reuse texture-feedback copies across unchanged source generations | Source design only; exact image generation/subresource and all writes must be known. No reuse implementation or paired full-frame cost test. | MMJ copy-skip observations change output policy and do not test preserved-output reuse. | **Untested.** Attribute actual copy cohorts, then test generation-qualified reuse with alias/partial-write controls. Never omit a required copy on a cache miss. |
| G11. Move Vulkan CPU policies into OpenGL | Source applicability audit only. Shared JIT changes apply, but Custom OpenGL does not execute the non-Custom fused pool/ready-TEV/coherent-queue policies. No port implemented. | Owner-controlled stock/OpenGL observations exist; API/filter changes and unlike scenes prevent isolated attribution. | **Untested port.** Requires actual GL sink/context/lease contracts and complete output/cost controls. A backend setting alone does not transfer Vulkan proofs. |
| G12. Expensive menu redraw, offscreen work, shadow/CAS, synchronization or cache startup allocation | Shared renderer/timing/source and retained sensor review; no command-level GPU attribution. Near-static visible pixels do not imply few guest draws or cheap offscreen work. | Stock low app CPU with high device GPU busy and large Vulkan KGSL gauges observed; API/filter/cache/scene scopes differ. | **Competing explanations remain untested.** Use bounded route/tick/query evidence without forced pass splits; distinguish guest completion timing from actual GPU cohorts. KGSL is not physical residency and GPU busy is not shader attribution. |

<!-- CodexAstraLocal: Specify acceptance of the selected lifetime fix independently from its unproven association with a visible title symptom. -->
For **G02**, the correctness target is: allocate cached A at tick1, complete its
original allocation use, enqueue a later draw reusing clean A at tick2 without
another Commit, then force a dirty upload to wrap. The writer must not replace
A until its last queued consumer finishes. Test uniform, lighting/fog LUT and
procedural LUT rings; ordinary, deferred and actual accelerated enqueue sites;
pending/completed ticks; pass-change flushes; and no-target or pending-pipeline
paths that enqueue no draw. Missing use stamps, old pre-flush stamps, missing
wrap waits and missing dirty refresh must fail the relevant control. A second
allocation with distinct bytes must be consumed correctly after wrap.

The conservative candidate may wait for more of a ring than the exact overlap
requires. That is an explicit performance risk: long GPU queues or frequent
wraps can increase owner stalls. It must add no wait to ordinary non-wrapping
maps and no per-draw wait. On owner resume, hardware comparison will retain whole-frame speed,
submitted frames, lower windows, CPU/GPU/memory context and owner-observed
graphics, with timings ON matching the existing ON controls. The fourth .40
control is a finite observation, not a synchronized opening baseline. A correct
forced-wrap result proves the lifetime repair; it does not by itself identify
the cause of moon/ghost artifacts. If title artifacts persist, retain that
result and investigate the next exact lifetime/state boundary rather than
silently declaring success or repeating the same hypothesis indefinitely.

<!-- CodexAstraLocal: Preserve secondary correctness work without presenting it as a released fix or allowing it to replace the primary CPU architecture objective. -->
## Secondary tracks and measurement work

| ID / proposal | Executed evidence | Status and remaining action |
| --- | --- | --- |
| S01. Normalize narrow A64 memory callback result carriers at the Dynarmic consumer | Real released callback/disassembly audit; copied actual A64 consumer correction and callback/page-fallback omission controls. Later strict build-tree source replacement builds the actual 119-object library, with 118 unchanged members. Four archives execute 1,728 rows each: original 272 mismatches, corrected zero, two omissions 136 each. Corrected archive also passes guest-status controls. | **Private package qualified, not shipped.** Durable generation/CMake/source-pin/no-op controls exist. Integration, mandatory CI, app build and hardware/title qualification remain undone. No submodule edit or dependency-fork publication is needed. |
| S02. Normalize in project-owned MemoryRead8/16 callback bridge instead | Private actual A64 backend corpus: 1,728 rows and sensitive byte/half omissions. | **Tested alternative, not selected over the consumer-local package.** Incidental C++ narrow-return code generation is not a general ABI guarantee. No title/device trial. |
| S03. Independent Calculated triangle/texture renderer | Private original-input, actual interpreter/JIT/FIFO/88-byte oracles, ordered compute and host Vulkan milestones. Includes texture ownership, ETC1/A4, perspective observations, depth-plane correction and bounded clipping controls. Some finite outputs match both GL and Vulkan; rational filtering mismatches remain separately documented. | **Private finite renderer research, not complete production Calculated.** No Thor title qualification. Broader clipping, raster/depth/blend/texture/state coverage and transactional integration remain; never score missing output or Native fallback as independent speed. The separate Calculated role owns this work. |
| S04. Shell profiling and exact symbols | Flavor-scoped profileability shipped .38; release verifier joins actual APK/symbol build IDs. Actual bounded .38 Native/Combo profiles resolve part of owner kernel work to the wake loop, with substantial unknown attribution. | **Implemented measurement.** Cycle-sample shares are not wall time, blocked time or Sonic/FEA profiles. No profiler starts automatically. |
| S05. CPU scheduling, user/system split, memory/PSI and bandwidth | Existing raw TID/start-time/clock accounting and bounded endpoint helpers exercised. Passive memory package shipped .39; optional failures stay unknown. | **Implemented measurement, with limits.** App CPU, global GPU busy, process KGSL and system PSI have different scopes. Qualified DDR utilization remains unavailable; frequency/votes/free RAM are not substitutes. |
| S06. Circle Pad Pro calibration applet port | Bounded primary-source repository search and incident protocol/source analysis found no usable complete licensed implementation to port in the inspected sources. Some unavailable histories remain unknown. | **Deferred by owner until after 1.0.0.** Do not develop a new HLE now or report enums/accessory packets as an implemented calibration lifecycle. The observed loop was not a confirmed emulator process crash. |

<!-- CodexAstraLocal: Put current measured throughput beside the experiment statuses without converting unlike windows into a causal benchmark. -->
## Present outcome and recommended order

The primary objective remains complete, correct rendering with useful portable
CPU throughput, rather than CPU occupancy alone. The .40 accepted wall-weighted
speeds are **38.771% Native OFF, 33.564% Combo OFF, 68.598% Native ON and 66.361%
Combo ON finite observation**. Their observed app CPU equivalents are about
**1.919, 1.679, 2.031 and 2.050 cores**. ON's real submitted-screen rates are
37.830/s and 36.731/s, separate from emulator speed. CPU allowances/worker
populations and observed scene spans differ; the fourth case has unknown first
Back and a predeclared monitor cutoff. These are not synchronized replay gains.

All four interior deferred populations have max wave one and zero auxiliary
packets. The old independent vertex workers still do useful work. Thus C19 is
an implemented ownership/overlap boundary, while broad cross-draw CPU scaling
remains unmet. Partial and full fragment selections prove route use, not an
exclusive GPU cost or a complete-frame .40 improvement. Visible Native faults
remain; unfinished visual coverage stays unfinished under the owner's decision
to assess images personally.

The current order is **G02 implementation/regression/hardware evaluation**, then
the strongest evidence-supported CPU boundary among C23–C26. Record why packets
are reconciled and whether their work was already complete before selecting a
tail or queue change. That evidence belongs alongside a potential fix, not an
empty diagnostic-only release. S01 can proceed as a separately reviewed bounded
correctness change without displacing the CPU objective. G08–G12 need concrete
route/ownership evidence before a large implementation; the tested alternatives
C06–C14 must not be relabeled as never attempted.

Still-unexamined performance options include exact reuse of owned preparation,
allocation/slab amortization, natural packet adjacency before real barriers,
qualified triangle packing, static lighting structure, unchanged-generation
feedback reuse and reducing proved no-op shadow work. None has a hardware gain
claim. Required GPU-to-CPU readbacks, command interpretation, guest-memory writes,
presentation, compiler contention and opaque driver allocations also remain
possible limits; their mere existence is not measured exclusive cost.

Representative repository gates are [parallel output](../tools/uberhar/test_parallel_vertex_observable.py),
[guest FP isolation](../tools/uberhar/test_guest_a64_fpsr.py),
[pool transport](../tools/uberhar/test_parallel_work_transport.py),
[ordered assembly](../tools/uberhar/test_parallel_assembly.py),
[packed getters](../tools/uberhar/test_vertex_register_decode.py),
[stream order](../tools/uberhar/test_vulkan_stream_order.py),
[JIT leases](../tools/uberhar/test_shader_jit_lease.py),
[owned draw packets](../tools/uberhar/test_cpu_draw_queue.py),
[deferred retention](../tools/uberhar/test_deferred_vertex_retention.py) and
[static TEV](../tools/uberhar/test_static_tev_generator.py). Their finite assertions
and execution backends define their scope; they are not substitutes for the
separately recorded title and cost evidence.

<!-- CodexAstraLocal: Carry the existing beta acceptance contract forward and label proposed refinements, rather than inventing new accepted milestones or dates. -->
## Beta roadmap: retained gates and proposed refinements

The existing [version gates](UBERHAR_ARCHITECTURE_0.1.6.md#roadmap-and-version-gates)
and [roadmap](Uberhar_Roadmap.html) remain authoritative. These are overlapping
acceptance checkpoints, not nine mandatory rewrites or promised dates. The
following refinements are **architecture recommendations for owner review**;
they do not lower existing speed, correctness, cold-cache or device requirements.

| Milestone | Existing gate | Proposed clarification from .35–.40 |
| --- | --- | --- |
| 0.2.0 | Better demanding matched scenes, exact output, retained 2x FEA baseline, profiling-supported architecture. | Require a complete useful-path comparison, admitted-work/critical-boundary evidence and explicit graphics outcomes. Closing G02 alone or raising CPU occupancy does not earn this beta. |
| 0.3.0 | Broad game coverage and honest recovery/fallback reasons across FEA, Sonic, LEGO, MH4U and additional titles. | Keep per-title route, cache, scene and cap coverage; do not let one successful opening stand for a full title or combine warm/cold controls. |
| 0.4.0 | Input/presentation timestamps, matching dual-screen frame IDs and no latency/pacing regression. | Distinguish submitted game screens, system frames and emulator speed; include GPU-timing setting effects without conflating them with CPU scaling. |
| 0.5.0 | Per-scene 2x/400% and 4x/200% qualification, including slow windows and unmet targets. | Use fixed-cap controlled scenes and sustained samples; the current .40 normal-limit openings do not qualify these headroom targets. |
| 0.6.0 | Bounded first-use work and cold/warm convergence without dropped work or unlimited prewarming. | Qualify module/PSO attempts, retirement, compile stalls and observed memory separately; object counts are not driver-byte limits. |
| 0.7.0 | Accuracy, crash, save/load, pause and surface-loss recovery matrix. | Include delayed CPU packets, cache invalidation, stream wrap, final-use fences and failure drainage in actual renderer lifecycle tests. |
| 0.8.0 | Resolution-aware correctness, beginning with FEA and 1x/2x/4x references. | Trace exact sampling/depth/ownership rules and preserve original output; avoid unconditional offsets, skipped shadows or reduced precision as a performance fix. |
| 0.9.0 | Feature freeze, reproducible tests, documented defaults/limits and signed delivery. | Freeze renderer scopes and environmental prerequisites; unresolved findings need explicit disposition, not an unexplained green aggregate. |
| 1.0.0 | Stable declared renderer scope across a named title/device suite: correctness, cold behavior, pacing, recovery and ordinary-speed reliability. | Publish per-title headroom and independent Calculated scope honestly. Confirm any stronger all-scenes/all-games headroom requirement explicitly; it is not already met. |
| Beyond 1.0.0 | Further compatibility and product work; the owner placed calibration HLE here. | Revisit S06 only with a usable port or separately authorized implementation. Wider hardware, richer Calculated coverage and larger CPU/GPU placement changes retain exact-output and complete-path acceptance. |

The Architect owns contracts and the evidence ledger; the Developer implements
and validates the selected fixes; the separate Calculated role develops its
independent renderer. Root operates the device and release pipeline. The owner's
latest boundary is to complete the next .41 gated build/release, save the
checkpoint, then pause all work. Do not install or title-test .41 before the
owner resumes. The two timing-ON cold runs above are resume steps, not completed
or currently authorized device work. This ledger supports a reviewable handoff;
it does not transfer device or release ownership.

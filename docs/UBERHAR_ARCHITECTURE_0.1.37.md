<!-- CodexAstraLocal: Close the .35–.37 architecture review against the delivered source and finite controls while keeping graphical and throughput requirements open. -->
# Architecture review through 0.1.37

The multicore path now executes substantial, qualified CPU vertex work on real
worker threads. It still falls far short of sustained normal speed, and allocating
seven workers has not produced continuous heavy use of seven cores. The next
useful step is to increase useful work per dispatch and reduce repeated owner
preparation, supported by complete-path comparisons. Raising thread counts or
broadening already high admission coverage is not the leading opportunity.

This audit closes the source, correctness, delivery and finite test review for
.35–.37 against the completed .34 anchor. It does **not** qualify sustained 99%,
4x, general graphics correctness, long-play memory behavior or beta status.
Native .37's retained moon capture crosses the owner's investigation threshold.
Development remains authorized; audit completion is not permission to ignore that
result or treat another unmeasured optimization as an improvement.

The exact delivered .37 source is
`f6e0d88842ff8204e112a7368035e4565848146d`, tree
`2bf0a05d6b677975ca627a6af86a4a85b1c6bdff`. All 27 mandatory CI steps and three jobs
passed; exact assets, source and compatible signing were verified. The earlier
attempt failed a test fixture that hid `SYS_futex` before GCC13's standard-library
headers used it. The correction changes test injection/diagnostics and artifact
retention, with unchanged runtime code. Local host/NDK success had not covered
that standard-library prerequisite; the failed attempt remains evidence.

Detailed populations and scenes belong in the [test review](UBERHAR_TEST_REVIEW_0.1.37.md).
The separate [cleanliness review](UBERHAR_CLEANLINESS_0.1.37.md) closes its finite
150-path inventory, including 71 runtime-source paths, 51 recently reviewed paths
and 99 carried paths. This architecture review follows the ownership and semantic
contracts; it is not an exhaustive proof of every emulator configuration.

## What the three builds established

|Control|Mean normal speed|Inclusive LoadVertices share of its separate reporting wall|Certified / worker share of completed attempted CPU invocations|
|---|---:|---:|---:|
|.35 Native|27.664%|79.434%|2.10% /1.33%|
|.35 Combo|28.279%|68.114%|0.40% /0.29%|
|.36 Native|30.500%|60.882%|92.10% /66.53%|
|.36 Combo|30.411%|52.086%|93.03% /68.72%|
|.37 Native, predeclared bounded observation|32.082%|63.793%|92.99% /66.90%|
|.37 Combo|31.978%|51.590%|93.09% /68.76%|

These are unlike scene, duration, observer and process populations, not causal
version or mode gains. Combo's denominator covers its remaining CPU vertex work;
ready GPU batches have no known shader-invocation count and are not included.
Recipe coverage is also high—about 86% in the .37 contained input populations—but
counted reach never establishes time saved by a recipe.

Native .37 has an exceptional scope: unexpected operator latency left a prolonged
later dialogue wait before Back. Before speed output was inspected, the analysis
cutoff was declared at the second CPU capture's final timestamp, 175.135036 s after A.
Its 34 complete frame windows cover 171.085689 s; the p05 is 23.417%, worst 204.200 ms.
The full 711.990926 s A-to-Back result remains descriptive only. It is not substituted
for an ordinary comparable opening. Combo's actual A-to-Back is 215.382332 s, with
42 complete windows/211.153218s, p05 21.239% and worst 385.032 ms. Lower-tail values
are wall-weighted reporting-window averages, not individual-frame quantiles.

Combo reused Native's app process in a separate verified title session. Its first
CPU capture was interrupted after three raw snapshots without a host manifest;
it remains incomplete and was not repeated. Only the complete second capture
qualifies activity. A separately declared conservative envelope censors possible
unknown observation for the supplementary no-observer subset; no fake sample or
Back timestamp was created. Menus and all navigation after actual Back remain
outside opening summaries. Missing optional records remain missing.

LoadVertices includes preparation, input loading, unchanged JIT arithmetic,
conversion, FIFO/assembly, synchronization and scheduling. It ends before Vulkan
DrawTriangles upload/submission. It is not exclusive shader CPU time, and its
remainder is not a GPU budget. Different diagnostic families retain different
reporting boundaries.

## Correctness is a chain of contracts

.35 established exact FIFO planning, immutable bound JIT execution, sleeping
persistent workers and ordered owner submission. Its initial full-arithmetic-read
certificate conservatively included discarded SIMD lanes that can affect host
status. Retained programs biased toward earlier GPU-ready shaders did not predict
the actual remaining CPU population: real .35 admission was small. The low worker
activity was useful negative evidence, not a reason to manufacture utilization.

.36 added a distinct SelectedOutputValues proof. It tracks possible initial
state dependencies through the complete original graph, snapshots sources before
alias writes, unions joins, rejects carried control/address dependence and checks
selected output lanes at every END. Shader arithmetic is unchanged. Dead local
values and host sticky status may differ only behind actual A64 shader and A64
Dynarmic guest-status capabilities, with traps disabled. Unknown backends retain
the original contract. Executed guest-status isolation and actual A64 output
controls support that boundary; ABI caller-saved wording alone would not suffice.

Truly never-written temporary/input lanes remain initialized at draw entry and
can be read safely. That does not permit retaining a previous draw's ShaderUnit.
A future participant context must start afresh for each job and preserve the
applicable carry contract within that job. Per-grain, per-participant and cross-draw
lifetimes are distinct choices, not interchangeable caches.

Exact proof bytes must identify the actual generated program. .35 replaced
hash-only compiled-source selection with exact code/swizzle ownership and cheap
same-setup/revision bindings. ShaderSetup copying preserves guest fields, clears
foreign compiled pointers and advances the destination revision. The certificate
cache also keys entry, boolean uniforms, output mask and proof contract. Its
8×16 program/profile storage is bounded. The compiled JIT cache remains retained
and unbounded, with 32 KiB of extra exact source per compiled entry; that is open
memory debt, not a bounded-cache claim.

The A64 return repair uses saved X19 for the active guest return position, preserved
with LR around nested CALL. It closes the inherited main/CALL stack-marker mismatch
under actual generated-code helper/loop controls. .36's narrow destination stores
preserve disabled raw lanes, output-bank selection, aliases and unchanged arithmetic.
Neither repair is a demonstrated cause or cure of a specific title disturbance.

Workers borrow immutable draw code/uniforms/defaults and validated mapped input/index
spans until a synchronous join. Exact64-slot FIFO discovery stays on the owner;
only misses are shaded and converted. Actual threaded DSP, RPC and pending async
IPC writing configurations refuse parallel work. The finite source inventory
also traced renderer downloads and owner-thread callbacks. It is not certification
of arbitrary future providers or dynamic race detection.

.37 adds the actual submission sink guard. Software AddTriangle may synchronously
write guest framebuffer memory that aliases later vertex/index input, so ahead-of-
submission batching is refused there. Current accelerated sinks only append owned
hardware vertices. Their bulk opt-in additionally promises synchronous consumption,
no retained references and no assembler observation/reentry/reconfiguration. A
future sink must satisfy both obligations, not merely report “no guest writes.”

## Sleeping transport and ordered output

The owner still plans a chunk, joins all shading, submits in order and copies
surviving FIFO payloads before result reuse. There is no hidden cross-chunk or
cross-draw pipeline. The Linux/Android transport uses acquire/release generation
publication and completion; workers sleep rather than busy-wait. Old delayed wakes
cannot complete a new job. Exceptions drain all borrowed work before propagation,
and teardown wakes/joins owners before storage disappears.

An initial nonblocking probe selects the original condition-variable path when
futex support is absent or denied. That fallback retains its own underlying
synchronization requirements. Unexpected permanent errors after successful futex
selection are terminal, not graceful recovery. This is a coordinator-reviewed
fail-stop boundary, not an error policy specifically chosen by the user. Finite
host/A64 schedule, wrap, delayed-wake and error controls support the implementation;
they do not exhaust all interleavings or future kernel policies.

Bulk List/Shader assembly borrows complete interior triplets after join. Existing
tails use scalar submission; exact final buffer bytes, winding and throwing-handler
state are preserved. Strip/Fan and scalar callers retain inherited behavior. The
getter must be stable and noexcept. A partially submitted draw is never retried
serially. The merged actual-input/JIT/FIFO/HardwareVertex control compares 112 full
vectors and final assembler states over 56 constructed cohorts; durable gates cover
state/throw/winding, both callback forms and current CPU/memory capabilities.
Synthetic public gates contain no private title payload.

The selected implementation had positive finite warm host evidence; an earlier
external state-slot candidate and a CV transport alternative were rejected after
neutral or worse results. Those decisions remain scoped to their exact experiments.
Host/QEMU correctness, warm host timings, counted device work and retained title
pixels are different evidence and cannot substitute for one another.

## What the new phase and scheduling evidence says

|.37 sampled population|Native bounded observation|Combo contained opening|
|---|---:|---:|
|Draws /chunks /invocations|3,386 /3,729 /2,919,694|3,093 /3,637 /3,496,975|
|Recorded outer wall|349.036ms|394.485ms|
|FIFO planning share|9.212%|9.759%|
|Pool share|72.026%|71.879%|
|Ordered submission share|18.762%|18.363%|
|Nested pool owner /join /remaining wall|135.753 /58.146 /57.497ms|145.390 /70.472 /67.688ms|

Schema3 samples completed admitted ordinals 0, 257, 514 and their complete chunks.
The sample is deterministic and may correlate with workload. **Do not multiply
these values by 257.** The nested rows are inside pool time, not extra phase totals.
Join is not worker CPU duration; the remaining pool bracket includes publication,
FP setup/collection and other work, not pure wake-syscall cost. Per-draw preparation,
certificate/input setup and some bookkeeping are outside these clocks.

Seven stable workers consumed 13.95/17.44 CPU-seconds in Native's two roughly 20 s
captures and 15.62 in Combo's single qualifying capture. Whole-process accounted
cores averaged 1.484/1.938 and1.922 respectively, already including owner/workers.
Those finite intervals show real worker activity, not a demonstrated capacity
ceiling or continuous heavy use of seven cores. High opaque GPU busy windows are
device-wide and have unknown interval alignment; frequency endpoints are not
residency or title GPU saturation.

Independent scheduling endpoints add a useful discriminator. Over roughly 43.8 s
Native and 41.7 s Combo spans, the seven workers accumulate 31.618/21.644 s running,
19.670/13.825s summed runnable queue wait, and 1.184/0.922 million timeslices.
Ratios of totals are 26.708/23.464 µs running and16.616/14.988µs runnable wait per
slice. These are wider spans than the CPU captures. Wait sums can overlap across
workers and exclude sleeping without work; timeslices are not identified futex
wake calls or jobs, and averages are not tail latency. The field meanings follow
[Linux scheduler documentation](https://docs.kernel.org/scheduler/sched-stats.html#proc-pid-schedstat);
exact vendor kernel implementation remains unpinned.

This supports investigating useful work per dispatch, but does not establish that
scheduler delay dominates. Both controls retain substantial owner processing.
Endpoint nice 0, allowed CPUs 0–7 and top-app membership show no observed endpoint
mask/nice restriction; they do not prove residency or absence of contention.

<!-- CodexAstraLocal: Record the completed context rejection and rank the next measurable direction without promoting a private candidate. -->
## Ranked next work

1. **Qualify a shared-wake cohort while retaining the current shader-state policy.**
   The completed context experiment compared current dispatch, typed dispatch with
   per-grain state, and typed participant-local state on actual input/JIT/FIFO/ordered
   hardware output. Host and A64 correctness passed, but the bounded host cost matrix
   was mixed and usually unfavorable to context reuse at four/eight participant
   limits. Neither context reuse nor typed dispatch is selected. Cross-grain results
   were descriptive rather than paired and do not select a universal grain change.
   Shared wake is a separate next experiment: it must prove registration, late-wake
   ownership, shutdown, FP restoration and exception drainage before a complete-path
   cost comparison. Preserve the current per-grain state and arithmetic so those
   rejected changes do not confound it. No target speed gain is established.
2. **Reduce or measure excluded per-draw preparation.** VertexLoader and input-plan
   construction, descriptor/recipe preparation, mappings/bounds and admission occur
   on the owner before phase clocks. Exact revision fast paths avoid repeated 32 KiB
   comparisons on unchanged draws; only 14–16 proof builds occurred in the selected
   .37 populations. General certificate expansion is therefore lower priority.
   Inspect optimized descriptor extraction first, then consider caching value-only
   layout preparation while refreshing all live addresses/defaults/aliases/ranges.
   Neither cost nor benefit is established yet.
3. **Weigh deeper output transport against its synchronization cost.** Output
   conversion already runs on workers; f24-to-float here returns a stored float.
   The remaining owner sink performs two quaternion decisions and three 88-byte
   appends per triangle. Prepacking can add traffic while 96-byte semantic results
   remain necessary. A second packing dispatch may cost more than it saves.
   Direct stream writes move GPU-storage waits and ownership earlier. Preserve exact
   topology tails, winding, arithmetic order, serialized state and exceptions.
4. **Consider broader overlap only with a new dependency model.** At least 90.63%
   of Native and85.71% of Combo admitted draws are single-chunk by count, not cost.
   Same-draw double buffering misses most draws by count. Cross-draw overlap needs
   immutable uniforms/input/state snapshots and exact guest-memory/renderer ordering.
   Spare cores alone do not prove that another queue is safe or useful.

<!-- CodexAstraLocal: Add the independently reproduced source-order defect without assigning a captured fault or claiming a released correction. -->
The Native moon threshold is a parallel correctness priority. A separate exact-source
host model reproduces a conditional CPU upload defect: a current-tick stream wrap
can flush after render-pass/pipeline setup, then record the draw in a fresh command
buffer without restoring either state. Seven bounded cases and 128 assertions
distinguish this from ordinary and older-tick reuse. This is a source-contract
finding, not real Vulkan execution or proof that the condition occurred in a title
capture. A private correction is under review; the released source remains unchanged.
Final resource-use ticks, optional owners and other upload paths need their own
scope checks. No capture cause is assigned, and this does not close an observed
graphical fault.

## Separate renderer and inherited obligations

Calculated still has no demonstrated guest rendering coverage for this title.
Strict .33/.34 controls omitted unsupported draws with zero guest compute or
native-graphics fallback, while shared CPU vertex work still ran. The successful
.34 scratch benchmark qualifies only its supported rectangle workload, owned
resources, exact readback and bounded GPU timing. It cannot qualify title speed.

Private research now runs original input bytes through guest compute shaders,
88-byte vertices, FIFO/ordered persistent tails, original tiled textures and joint
pixel/state commit, including software Vulkan execution. A perspective extension
passes its rational oracle but retains a graphics texel-boundary mismatch: actual
graphics interpolation lies just below the ideal boundary. That blocks general
pixel-parity claims. A small ETC1/ETC1A4 decoder checkpoint is also private; full
filtering/lighting/state/surface integration and Adreno performance remain open.
These executable milestones are not shipped Calculated coverage.

The inherited byte/half memory-callback return-carrier hazard remains outside .37.
Static production MMIO paths and real poisoned-carrier backend controls establish
a conditional ABI-consumer defect, not a title trigger. A repository-owned checked
build overlay now has finite CMake/archive/link and executed correction controls;
unknown build modes, project integration and delivered APK qualification remain
future work. Consumer normalization is preferable to relying on incidental C++
callee upper bits. No dependency-fork publication is required or performed.

Other carried obligations remain explicit: unexpected descriptor-allocation error
termination; fence-worker teardown/publication; cache clearing with live handles;
the diagnostic label for required specialized recovery; accelerated GPU Strip/Fan
tail transfer; and unbounded JIT ownership/exact-source snapshots. CPU bulk tails
do not fix the GPU path. Optional fragment CPU compilation has a 64-attempt lifetime
cap and an 8-ready bank; selection/dependency/cap counters do not measure savings or
justify simply increasing compiler threads. Existing pipeline/shader compilation
is already concurrent.

The one Combo cleanup observation, 40.096 s after normal return in the same process,
shows KGSL 13.340 MiB/RSS 483.949 MiB after during-run KGSL 3170.918 MiB/RSS 1027.688 MiB.
It supports later recovery, not immediate release, peak use or long-play safety.
Cold application caches do not establish cold driver caches. Native's 536 reviewed
moon frames contain 12 events/13 affected frames, exceeding the count threshold;
its 733 ghost frames contain six isolated disturbances. Combo's 548/639 frames show
none in its different observed population. Unequal scenes prevent causal fault-
frequency comparisons; neither tolerated flashes nor numerical closure constitute
a full graphics pass.

The next selection must show useful complete-path improvement and preserve exact
output, ordering and smoothness. The released batch and its finite audit are
complete; the owner's throughput and graphical goals remain unfinished.

<!-- CodexAstraLocal: Close the three-build architecture review with actual device evidence, preserving the interrupted fourth case and the owner's reporting boundary. -->
# Architecture review through 0.1.40

The .38–.40 changes have qualified source ownership and finite correctness
controls, but they have not met sustained 99% normal speed or complete graphics
qualification. The existing GPU-timing switch produces materially different
recorded behavior in .40. It does not add CPU parallelism, and Native timing-on
still crosses every agreed moon-flash threshold. The next work should establish
resource lifetime and useful overlap before widening concurrency.

This review covers .38 (`4902aa2333ae536c786168ea4d6f2776ef296f83`), .39
(`ebd770c713d2d335494578711f3b4f17e168eea7`) and .40
(`7bc71c5c9c7e10a39558084392330faa884889ba`). The separate
[cleanliness review](UBERHAR_CLEANLINESS_0.1.40.md) carries the completed
independent purpose/consumer audit. Source validation, numerical evidence and
visible rendering remain separate qualifications.

<!-- CodexAstraLocal: Describe what each released intervention actually moves or removes; static TEV is not a new CPU-to-GPU transfer. -->
.38 reduces repeated vertex-layout decoding and moves CPU geometry reservation
before Vulkan preparation that can submit a command buffer. .39 gives eligible
CPU draws owned input, uniforms and immutable compiled-code lifetime, then lets
a coordinator produce hardware vertices while the owner prepares Vulkan
resources. Large certified draws retain the existing within-draw CPU pool.
.40 prepares the fixed TEV structure on CPU and compiles an optional fragment
shader with runtime lighting. That tier still consumes CPU-produced vertices;
fragment processing was already on GPU. Full Combo specialization remains
available and can supersede the partial tier.

All 28 required release steps passed in
[run 37993185551](https://github.com/RegiRex/uberhar/actions/runs/37993185551).
The actual .40 APK and installed bytes were verified. The observed handover
found .40 already installed; it does not establish who installed it or when.

<!-- CodexAstraLocal: Report all four requested cases while distinguishing three witnessed opening boundaries from the finite interrupted observation. -->
The device cases used Vulkan 2x, a 100% limiter, zero explicit render-thread
delay, reset File 1 and empty title-specific application-cache inventories.
Timing expectations were checked in both lifecycle settings records and the
persisted configuration. Driver-internal coldness remains unmeasured.

| Case | Accepted window time | Mean speed | Slowest / best fixed window | Game submissions/s | System frames/s | Process core-equivalents |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Native, timings off | 246.28 s | 38.77% | 26.35% / 90.01% | 23.17 | 23.20 | 1.919 |
| Combo, timings off | 261.48 s | 33.56% | 18.88% / 62.27% | 20.06 | 20.08 | 1.679 |
| Native, timings on | 175.52 s | 68.60% | 57.05% / 90.46% | 37.83 | 41.04 | 2.031 |
| Combo, timings on — limited observation | 280.77 s | 66.36% | 38.26% / 90.35% | 36.73 | 39.70 | 2.050 |

The first three witnessed A-to-Back spans were 250.07, 265.23 and 180.01 seconds.
The fourth was interrupted by an approximately 92-minute session gap. On
resumption the app was already at its library/title panel; first Back, exit
input and actor were not observed. Before inspecting its speed results, analysis
was capped at the last valid endpoint of the original finite monitor, A+284.10
seconds, or an earlier title lifecycle endpoint. Its later logged teardown is
retained as lifecycle evidence. The long intervening session is excluded from
this table. The fourth row is recorded rendering evidence, not a qualified
complete fourth opening.

Means use logged window duration as weight; extrema are complete fixed windows
of at least five seconds, not rolling minima. The corresponding weighted fifth
percentiles were 28.156%, 19.468%, 58.790% and 45.162%. Excluding video and heavy
observer enclosures gave means 38.44%, 33.27%, 70.81% and 68.44%, over different
scene populations. This sensitivity check cannot estimate observer overhead.
Every primary window remained below 99%.

Game submissions count top-screen buffer-swap calls. System frames count
renderer frame endings; speed measures guest-time progress per host time.
These are not interchangeable with physically displayed unique complete frames.
Durations, scene progression, observer overlap and available worker populations
differ, so the table does not establish a matched-scene causal multiplier.

CPU values use whole recorded intervals, spanning 245.115, 260.100, 175.105 and
280.085 seconds respectively. They correspond to about 24.0%, 21.0%, 25.4% and
25.6% of eight logical CPUs, not heterogeneous-machine capacity. Worker and
coordinator time are already included in the process total. System per-core
activity is not app residency. No selected extreme has a wholly enclosed CPU
interval under the clock bounds; overlapping whole intervals remain context.
GPU samples have opaque device-wide accumulation windows. RSS, own KGSL kernel
bytes and available system RAM are distinct, non-additive gauges. DDR bandwidth
remains unavailable.

<!-- CodexAstraLocal: Preserve completed temporal counts and explicitly stop qualification where the owner ended agent visual review. -->
Completed consecutive-frame review found three isolated opening-moon events
and seven isolated ghost disturbances in Native timing-off. A later exterior
moon clip had one additional isolated event. Combo timing-off had no identified
moon or ghost event in its two clips; later scenes were not covered. Native
timing-on's completed moon clip had 56 events across 78 affected decoded frames,
with a maximum run of three frames and 19 events lasting at least two frames.
It crosses the occurrence, maximum-length and repeated-multiframe thresholds.
Native timing-on ghost review and all Combo timing-on visual review remain
unfinished after the owner stopped agent frame analysis. Missing review is not
a pass. Capture populations differ, and these observations do not identify a
specific renderer fault or establish a version regression.

<!-- CodexAstraLocal: Tie the existing timing option to guest interrupt behavior without attributing a host GPU or multicore speedup. -->
Settings > Debug > Simulate 3DS GPU Timings is already editable and neither
preset overrides it. No .41 toggle implementation was needed. In
`GSP_GPU::SignalInterruptForThread`, it changes scheduled guest GPU-completion
delays; VBlank retains its existing scheduling path. It does not wait on a
physical GPU timestamp or grant extra CPU workers. A changed completion schedule
can change guest work submission and scene progression. The higher recorded
timing-on rates therefore warrant keeping this control in future qualification,
but they do not resolve correctness or identify which host stage became cheaper.

<!-- CodexAstraLocal: Separate established worker activity from the deferred queue's absent auxiliary fan-out and from unmeasured wait causes. -->
The existing vertex workers did real work: their aggregate CPU equivalents were
0.783, 0.569, 0.865 and 0.775 across the respective telemetry populations. The
pool reported five, six, six and five auxiliary vertex workers, consistent with
construction-time CPU allowances of six, seven, seven and six. The source uses
the calling thread's allowed CPU mask where available; eight online system CPUs
do not promise eight CPUs available to that caller. Endpoint CPU IDs do not
explain migration, scheduling or why the allowances differed.

The separate deferred-draw executor still had no auxiliary fan-out. Interior
counter spans recorded 129,632, 129,205, 84,696 and 86,729 completed packets,
respectively. Every case had maximum wave size one, one coordinator packet per
wave, zero auxiliary packets and zero failed packets in those spans. Completed
packets also equaled nonempty reconciliation boundaries. This establishes one
packet per reported boundary in the successful histories; it does not identify
the boundary reason or prove that reconciliation actually waited. The existing
capture/submit wall field includes owner Vulkan resource preparation and waits,
so it cannot be labeled copying cost or shader CPU time.

Static TEV was exercised. Separate interior selection spans reported 655,933
and 393,194 partial selections in the two Native cases, with no optimized GPU
vertex draws. Combo reported 553,023 partial plus 846,046 full selections with
timings off, and 327,815 partial plus 486,296 full in the limited timing-on
observation. These counter families have their own endpoint populations and do
not price their fragment work. Both tiers' actual selection confirms retained
full promotion; it does not prove a complete-frame improvement from .40 alone.

<!-- CodexAstraLocal: Keep ownership and current admission constraints explicit before proposing a wider CPU pipeline. -->
The deferred route keeps command interpretation and live framebuffer, texture
and Vulkan preparation on the owner. Its initial admission requires an eligible
complete List/Shader batch, no geometry shader, bounded input/work and an
isolated A64 floating-point contract. Each packet owns copied input, defaults,
uniforms and its compiled-code lease. Within-draw FIFO misses and carried shader
state remain serial. The command worker holds the output ticket and coherent
allocation lease, waits for completion, copies output and draws in order. Final
primitive state is reconciled on the owner; missing targets preserve geometry.

Guest-memory dependencies constrain widening this seam. Future command words
remain live guest memory; texture or framebuffer validation can download GPU
data into that memory. Capturing later inputs before those effects can change
values. A deferred primitive tail must preserve its exact raw pair, logical
empty state, ordering, failure precedence and eventual materialization. Raising
worker counts or delaying dispatch to form artificial batches does not prove
those contracts.

The partial route has eight module owners/16 attempts and eight partial CPU
pipeline owners/16 attempts, alongside eight full pipeline owners/64 attempts.
Pending and retiring owners remain charged. Separate probation prevents one
tier from erasing the other's history; a ready full pipeline wins, and full
promotion continues while a partial route renders. One optional worker handles
compile, pipeline and retirement work without waiting for optional readiness
on the render path. These limits bound objects and attempts, not driver-resident
bytes or GPU execution time.

<!-- CodexAstraLocal: Record the owner's later authorization for a real lifetime fix and hardware validation, superseding the earlier proposal-only pause. -->
After reviewing the four cases, the owner authorized implementation and hardware
testing of potential fixes. The Architect has handed the first candidate below
to the separate Developer. Publication still requires a concrete validated fix;
an empty or diagnostic-only release is not selected. The remaining candidates
retain their stated investigation and proof requirements.

1. **Prove resource use through the actual consuming draw.** Trace allocation
   identity, range, generation, publication, final command tick and retirement
   for cached uniforms/LUTs, dynamic fragment transport, descriptors and geometry.
   A concrete source concern is that clean uniform/LUT offsets can be reused
   without another `StreamBuffer::Commit`, while wrap protection records the
   original allocation tick. First use a bounded delayed-consumer/wrap control
   with distinct contents to determine whether an older watch can permit reuse
   before the last draw consumes the range. The initial correction records the
   cached rings' last successful draw-use tick after final preparation and waits
   through that use before wrap exposes writable reused bytes. It preserves the
   existing allocation watches and must prove invalidation refreshes every
   referenced cached offset. Include ordinary/deferred geometry,
   pass-change and no-target controls. Require exact output and unchanged order;
   a forced wait that merely changes scheduling is not proof of the fault.
   If all sampled lifetimes remain valid and the defect persists with distinct,
   retained allocations, that result weakens this hypothesis and directs the
   investigation toward texture feedback or shader/state identity. No observed
   flash is attributed to this concern yet.
2. **Measure why useful packets cannot coexist.** Use bounded reason and
   readiness counts at existing nonempty reconciliation sites, distinguishing
   already-complete work from actual waiting. Pair these with real pending
   packet populations and whole-path cost. If topology/tail materialization
   dominates and memory dependencies permit it, prove a logical-empty deferred
   tail that admits naturally adjacent independent packets. If interrupts,
   command-list return or readback dependencies dominate, preserve those
   barriers and reject this concurrency candidate. Auxiliary participation
   alone is insufficient; exact output and improved complete-frame cost are
   required. No new GPU offload is proposed.
3. **Remove proven duplicate owner preparation.** A refused packet can repeat
   input mapping and state preparation before ordinary fallback. The .40 cleanup
   removes only one unused recipe selection. A future owner-local prepared
   object could serve both admission and immediate fallback, provided no live
   memory/state boundary intervenes. Compare complete accepted and refused
   paths, including changed layouts and aliases. Reject the change if ownership
   bookkeeping exceeds the avoided work; refusal counts alone are not a cost.

Shadow work remains a separate GPU hypothesis. The shared CAS path couples
previous depth and shadow intensity, so arbitrary reordering or `atomicMin` is
not equivalent. The software renderer uses different explicit arithmetic and
is not an established byte-identical replacement. CPU tiling would first need
owned state, exact same-pixel order and arithmetic, and a measured budget for
readback/upload and render-to-texture dependencies. Disabling shadows or skipping
texture copies is not an output-preserving optimization. GPU busy, clocks and
large KGSL gauges do not identify shadow retries, copy cost or bandwidth.

The Architect owns these design boundaries; the separate Developer implements
and validates the authorized candidate. The owner now judges visible artifacts,
with completed evidence preserved. The earlier .40 report-and-pause boundary is
retained as history and superseded by this explicit fix/test authorization. A
following release requires its own source review, regression and hardware
qualification. The existing timing switch needs no replacement build, and this
review makes no beta or complete-graphics claim.

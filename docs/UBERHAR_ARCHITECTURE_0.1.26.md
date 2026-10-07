# Architecture review: 0.1.24–0.1.26

<!-- CodexAstraLocal: Close the required three-build source/evidence review after integrating two cold runs and independent reviews; audit completion is separate from device qualification. -->

**Full three-build review completed October 7, 2026; sustained-speed and beta
qualification remain unmet.** Reviewed released
0.1.26 source `e83e8f4f1738d4883829cc06689ce1ab075a5ac0`, exact tree
`ec9c5cbf111ccdefde20b3773a936e189a243cf3`, against the audited 0.1.23 anchor
`99fe4a8851ff696219dddda58fb154fbf8ce1149`. The three completed successors are
0.1.24, 0.1.25 and 0.1.26. The integrated review establishes released 0.1.26 as
the new [review-ledger](UBERHAR_REVIEW_CADENCE.md) source anchor. A completed
0.1.27 build will be successor one; no .27 implementation is selected here.

The reviewed changes preserve complete CPU fallback, bounded optional GPU work
and accurate fragment recovery. No new release-blocking renderer, Android or
delivery defect was found. One diagnostic route-label defect and inherited
failure/ownership debts remain below. Two clean 0.1.26 openings repeat positive
results for the separately reviewed moon and ghost intervals, without proving
whole-game correctness. Their complete logged windows average **23.227% and
22.339% of normal speed**: the throughput goal is substantially unmet.

Retain the conservative hybrid architecture while closing those evidence gaps.
Do not promote 0.2.0, expand GPU admission or select an unrelated optimization
because host tests and publication succeeded. A full architecture review can
complete with unmet product gates; it must record them and select an evidence-based
next step rather than imply that an audit certifies the product.

## Scope and change inventory

<!-- CodexAstraLocal: Attribute new logical work without relabeling earlier contributors or claiming every inherited line was independently reverified. -->

This review combines independent renderer/source, Android/delivery/diagnostics,
performance and temporal-evidence reviews. It examines changed sections and
selected active route, lifetime, cache, arithmetic and recovery contracts. The
private source inventory records 40 production/tool/workflow paths in the
0.1.23-to-0.1.26 interval and exact hashes for the reviewed material. It does not
claim a line-by-line audit of all inherited emulator and dependency code.
The earlier [post-0.1.22 recovery audit](UBERHAR_AUDIT_2026-10-07.md) and
[0.1.20 architecture review](UBERHAR_ARCHITECTURE_0.1.20.md) remain part of the
review history. Historical author comments and the original local device report
are preserved; new explanations use `CodexAstraLocal`.

| Build | Logical changes | Established result and limit |
| --- | --- | --- |
| 0.1.24 | Conservative never-written output-W guard, exact program/swizzle memo, regression coverage, and gated alpha/beta publication classification. | Production semantic tests expose CPU zero versus GPU default-one transport. The real title rejected no draws for that proof, and both visual symptoms persisted. |
| 0.1.25 | Default-off bounded discovery/capture, immutable uploaded input and actual-bound uniforms, submission/completion evidence, strict private inspector and production-interpreter/Mesa replay. | Two discovery and six eight-packet payload artifacts survived eight real cold openings. They are vertex evidence, not full Adreno frame replay. Both temporal symptoms persisted. |
| 0.1.26 | Immutable Combo-only precise pairwise DP4/DPH/DPHI generation when a supported CPU JIT is enabled; typed capture/replay policy compatibility and focused regressions. | Both reduced mixed-route depth failures pass with the correction. Two real openings repeat bounded positive visual results; complete correctness and sustained speed remain unqualified. |

App/JNI sources, permissions, dependencies, ordinary logging and core CPU engines
are unchanged across this interval: the diff under `src/android`, `src/common`
and `src/core` is empty. This does not exempt their active contracts from review.
The .26 generator change does not alter Native/Custom behavior, CPU execution,
topology eligibility, draw order or the lit-vertex quaternion safeguard.

## Delivery and validation already established

<!-- CodexAstraLocal: Record the exact published artifact and compatible update, separating CI cryptographic verification from local certificate extraction. -->

[Actions run 37587379116](https://github.com/RegiRex/uberhar/actions/runs/37587379116)
passed shader, Android/package/signing and publication jobs for the exact source
and tree above. The [0.1.26 prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.26)
was verified and installed by the sole device coordinator with `adb install -r`;
the install succeeded and a fresh probe reports the expected version. No
uninstall or application-data reset was used to bypass signing compatibility.

| Proof | Verified result |
| --- | --- |
| Installed package/version | `org.uberhar.uberhar_emu`, 0.1.26, code `33975176` |
| APK SHA-256 | `0215110ac382958c55d5aa96c0b5aac3d860041593620f036f9b91c1c2d29d1c` |
| Signing certificate SHA-256 | `078cba1dd4b9f194d2f34b687a0440f7a706eeb7df294444e40315be1f65207c` |
| Provenance/assets | Release tag targets the exact commit; APK/checksum/validation-archive asset digests match; ZIP integrity passes. |
| Android/native package | Nondebuggable ARM64 package, eight little-endian AArch64 libraries, gated dependencies/alignment and 11-permission merged manifest. |
| Cryptographic scope | CI `apksigner` v2 verification passed. Local embedded-certificate extraction matches the repository pin and prior installed release; local `apksigner` is unavailable, so a second local cryptographic verification is not claimed. |
| Configured shader-cache identity | `7158be2829809dc1e22582ed023b0a19`, changed from .25's `bd05922e5f6e8bec80a0bd48c30c5069` after fresh CMake configuration. |

Local host gates passed before publication: complete probes, 2,132 fragment
module validations, exact TEV/fragment comparisons, attribute/quaternion/output
guard controls, and the existing compute Vulkan control. Capture passed 404
checks and 13 real-producer artifacts; replay passed 83 checks across 15
interpreter/Mesa cases. Native compilation and CTest passed: 61 distinct passing
Catch cases, 1,096 assertions, with five existing firmware-dependent DSP/audio
cases skipped. CTest's 67 entries include an aggregate invocation and must not be
reported as 67 distinct tests.

Independent arithmetic regression passed nine numerical cases, both fixed
mixed-depth failure/repair directions, 40 Vulkan and eight OpenGL SPIR-V module
validations, and four actual OpenGL SPIR-V depth executions covering both
optimizer modes. Ten legacy/native generated sources match exact published
0.1.25 source byte for byte. The old generator fails the new behavioral gate.
Vulkan module validation is not Vulkan execution of that arithmetic witness.
None of these host results measures Thor throughput or certifies Adreno pixels.

## Active rendering contracts

<!-- CodexAstraLocal: A usable shader alone does not establish complete-draw equivalence; preserve admission, live state and fallback ownership together. -->

`pica_core.cpp` distinguishes the experimental presets from Custom's upstream
hardware-vertex route. Combo attempts an eligible complete GPU draw and returns
to normal CPU processing if optional preparation fails. Admission still requires
independent List/ShaderList batches, no guest geometry shader or debug context,
no assembler tail or pending winding, and matching topology. Bounds remain
96–65,535 vertices, at most 4 MiB speculative vertex upload, 128 VS/GS
configurations each, 256 optional pipelines and one pending optional build.

Input guards preserve requested register writes, aliases, defaults, zero strides,
short tails and complete mapped spans. Index ranges are checked before scanning
and vertex spans before speculative copying. Texture/fragment state is synchronized
and the final bind checks the complete execution key and actual shader owners.
Preparation rejection or a final key mismatch cancels speculative framebuffer
invalidation and retains the CPU draw. Lit GPU promotion still requires the
geometry/barycentric machinery for quaternion interpolation; the actual Thor
configuration keeps those lit draws on CPU. This guard is not optional simply
because unlit tests pass.

CPU processing retains a fresh zeroed ShaderUnit per draw, state across actual
FIFO misses, original index order and the existing 64-entry FIFO with fixed
bucket chains. The fused loader freezes only safe per-draw mappings, reads live
inputs/defaults/uniforms on misses and falls back on uncertain ranges. Cached
values are final 96-byte output vertices; there is no cross-draw transformed
vertex cache. A presumed linear FIFO lookup is therefore not a valid optimization
target in the current implementation.

The .24 output guard composes packed semantic mapping and overwrite order,
including aliases/padding, then scans all 4,096 words, even unreachable/stale
words after END. An unknown opcode cancels the proof. Exact code/swizzle snapshots
are compared on hits, so collisions and in-place mutation cannot alias the
result. The 128-entry memo retains about 4 MiB of source words plus bounded
metadata. Its 32 KiB hit comparison has no established Thor cost attribution.
Only absence of *any possible* physical W write proves the guarded zero/one
difference. Possible writes do not establish invocation carry parity, and emitted
output semantics do not establish fragment liveness. The guard can be conservative
and does not claim general CPU/GPU equivalence.

Generic fragments retain the supported cold CPU route. Unsupported shadow, gas,
custom-user, invalid lighting and AddSigned states retain mandatory specialized
recovery. Live TEV data and descriptor/offset state are copied into queued work;
cached classification does not freeze draw constants or readiness. Optional
specialization requires 16 observed demands, exact config/profile agreement,
at most 128 modules and one pending build. Generic families/pipelines have
separate 128/1,024 record limits. These counts do not bound opaque driver bytes.
Cold generic misses can still wait; background compilation does not make all
rendering asynchronous or wait-free.

Title reload/change drains existing GPU work and relevant compiler workers before
clearing pipelines and borrowed module owners. Failure completion and terminal
scheduler cancellation retain accurate recovery or controlled failure, not
fabricated successful draws. No arbitrary in-flight cache eviction was added.
The named `skipped_draws=0` counter remains necessary evidence, but it measures
its specific asynchronous skip branch rather than proving every pixel or every
inherited early-return path.

## Arithmetic correction and replay limits

<!-- CodexAstraLocal: Preserve the exact demonstrated arithmetic scope and distinguish interpreter, x64 JIT, AArch64 JIT and GPU execution. -->

The .26 `precise_jit_dot` flag is frozen by the Combo ready-worker owner only when
the supported x64/AArch64 CPU JIT is enabled. DP4/DPH/DPHI use precise products
and `(p0 + p1) + (p2 + p3)` with the selected multiplication sanitation; DPH uses
homogeneous W=1. DP3 remains unchanged because x64 and AArch64 association differs
at a signed-zero edge. This is a specific arithmetic correction, not full JIT
parity. Guest shader-config identity remains stable; source identity distinguishes
the new generated module, and renderer ownership makes the policy immutable.
Combo does not load/write Custom transferable cache records.

The motivating synthetic regression executes the real x64 JIT, production
generator/native transport and a real verified D24 framebuffer with LEQUAL.
Fixed witnesses reproduce zero of 1,024 color pixels in each mixed-route order,
while same-route controls cover all 1,024. Precise pairwise generation restores
both directions, including both SPIR-V optimizer policies. It adds no depth bias,
tolerance, skipped draw or GPU wait. AArch64 source has the same DP4/DPH tree;
physical AArch64/Adreno execution is a separate claim.

The public packet replay executes the production **interpreter**, whose sequential
DP4 differs from JIT association. It preserves actual index/FIFO order, zeroed
draw-local state and within-draw carry, validates source/config identities, and
compares actual versus intended PICA uniforms and carry versus fresh-unit controls.
Mesa uses typed inputs and generated shaders; the production trivial VS transports
CPU output. Results are mapped vertex semantics before primitive/fragment
processing. Textures, LUTs, destination pixels, full fragment execution, hardware
interpolation and Adreno invocation behavior are not reconstructed.

A separate private prototype now executes the production x64 `JitShader` over all
48 retained payload packets: 23,832 submitted vertices and 524,304 mapped scalar
values. Fourteen retained synthetic controls, exact old-interpreter reconstruction,
identical input/source identities, exact CPU-output retransport and independent
lifecycle checks passed. Its source/binary/CPU/FP-control provenance is retained;
it does not overwrite the .25 worker or reports. It bypasses `JitEngine`'s cache
wrapper, so it does not test cache invalidation. All original packets were actually
recorded, queue-accepted and completed. Comparisons reuse retained .25 Mesa
vertex outputs; the new Mesa work only transports CPU output, rather than
rerunning the captured guest GPU programs.

That x64 host uses approximate `rcpss`/`rsqrtss`; AArch64 uses FDIV and
FSQRT/FDIV. Larger x64 differences therefore cannot be renamed Thor results or
used to change GPU/ARM arithmetic without a separate reduced proof. The selected
ghost entry344/count234 and depth entry0/count756 instances remain wholly beyond
the right clip plane under both host routes. Other selected instances remain
unassigned. The unlit selected color shader does not consume its differing
quaternion/view outputs. None identifies the visible faulty actor. Actual
depth480 and color480 captures share mesh data but have different cross-run
position uniforms; same-frozen-state depth/color counterfactuals agree within
the interpreter and within Mesa, not between them. That counterfactual did not
execute either CPU JIT backend.

## Capture, diagnostics and memory ownership

<!-- CodexAstraLocal: Keep bounded diagnostic evidence distinct from a frame replay, a performance benchmark and an application-memory guarantee. -->

Capture reads one strict, at-most-8 KiB sidecar at eligible title initialization.
Absent configuration allocates no Store, copies no payload and creates no capture
worker; ordinary null checks remain. Native/Custom do not load it. One explicit
manual phase edge opens a finite window. There is no polling or automatic scene
replay. Config changes require a new run, exact title/evidence identity and
exclusive output creation. Unsupported storage providers reject capture alone.

The current selector uses one exact guest entry and a global optional-attempt
ordinal counted before filtering. Failure censors that occurrence rather than
substituting a later instance. Discovery deduplicates families only within each
swap and retains their first ordinal; this is not actor identity. Bounds are
eight packets, 4,096 submitted vertices and independently bounded span, 496 KiB
raw payload plus 16 KiB packet metadata, 3.75 MiB aggregate payload with 256 KiB
metadata reserve, 128 KiB manifest, 4 MiB artifact, 32 selection attempts and
8 MiB counted lifetime payload writes. Discovery has 128 rows with per-interval
quotas and at most 1,024 examined attempts per interval. Fixed Store/Writer sizes
are checked; serialization creates no second payload-sized JSON tree. These
are diagnostic bounds, not exact RSS or libc/kernel-copy bounds.

Vertex/default/index snapshots are the actual uploaded bytes copied at their
existing upload points. Intended uniforms are separate from final bound UBO
semantic bytes and offsets; padding is zeroed. No guest or stream pointer escapes
into the queued owner. A token records only after the actual draw command, then
uses the existing post-submit watermark and separate completion tick under the
same run identity. Existing drain points suffice; capture adds no GPU wait.
Normal exit writes once, while a kill may lose RAM evidence. Swap intervals are
not guest/video frames, and missing intervals are unknown/censored, not clean.
Armed capture can perturb readiness and timing; it cannot qualify normal-speed
performance. Both clean .26 runs had the sidecar removed.

The existing current/previous/older log writer, lifecycle marker and separate
bounded incident collection are unchanged. Rotation failure appends rather than
truncating current evidence. The approximately 100 MiB ordinary per-process-file
threshold is not an incident-directory storage bound; flush/barrier success is
not power-loss durability. Optional Android exit/thermal services remain optional,
with bounded reads and unknown-on-failure semantics. GammaOS and API-wide device
compatibility are not newly certified by the Thor run.

The retained .24 log reports 44 omitted optional records; the final .25 log
reports 410 across eight sessions. These are diagnostic omissions, not skipped
graphics draws. Copied log snapshots are not separate session/storage growth.
Process RSS, native/JVM allocations, KGSL, Vulkan accounting and system available
memory overlap and must not be added. Short normal-return recovery is useful
evidence but does not establish full-game residency or failure-path cleanup.

## Two clean .26 observations and performance attribution

<!-- CodexAstraLocal: Record repeated bounded positive temporal evidence alongside the independently measured throughput deficit, preserving unlike scene and observation scopes. -->

The coordinator ran full Combo, Vulkan, 2x, normal 100%, with File 1 reset and the
title application Vulkan cache deleted. The next launch's empty application-cache
inventory is the cold-start evidence; driver-private cache state remains unknown.
The capture sidecar was removed. Native's process was unchanged and was not
retested; the owner's Native reference has no observed moon glitch.

Independent consecutive-frame review found no prior moon flashing in the fully
visible first-run frames 0–154 (PTS 0–7.1309 s); later frames crop the moon.
Ghost review found no previous dense irregular dark corruption in the matched
near-ghost core, frames 64–119 (PTS 5.758622–10.036144), foreground core 360–389
(30.595867–33.402478), and right-side core 410–449 (35.435611–39.194200).
Wider foreground 350–429 and right-side 410–489 intervals were inspected;
the coordinator independently corroborated subsets. Broad coherent transparency
or background bands were retained as a separate observation. These are bounded
positives, not a whole-scene cure or exact attribution to one shader change.
The second independently reset cold run repeats these bounded observations:
fully visible moon frames 0–350 (PTS 0–16.326978 s), near ghost 86–145
(7.764378–12.363100), foreground 380–409 (32.588433–35.406744), and right ghost
430–469 (37.532189–41.403533). Every core frame was reviewed; wider context
and conservative cropping boundaries are in the
[device report](UBERHAR_DEVICE_TEST_2026-10-07.md). Root corroborated selected
consecutive sheets in both runs, not the complete independent review. Neither
lossy screenrecord nor host replay supplies physical Adreno readback.

Normal return succeeded in the same process, PID 4159. External readings are
shown below in MiB; their overlapping accounting scopes are not added.

| First-run sample | KGSL kernel | Process RSS | System available |
| --- | ---: | ---: | ---: |
| Before launch | 10.38 | 204.72 | 12,255.86 |
| Before opening, after title/menu setup | 1,939.10 | 933.96 | 9,567.02 |
| After moon | 2,445.46 | 967.77 | 9,019.66 |
| After ghost | 2,834.73 | 990.26 | 8,650.33 |
| Verified normal return | 13.32 | 319.32 | 12,028.63 |

These are discrete recovery observations, not established whole-run peaks or a
full-play memory bound. The second run also returned normally in PID 4159, with
sampled KGSL falling from 2,787.19 MiB after its ghost clip to 13.32 MiB after
verified return; returned RSS was 478.22 MiB and system availability 11,816.13 MiB.
Its later in-opening health samples reach 2,955.35 MiB KGSL; the full lifecycle
reaches 3,342.75 MiB only in the excluded post-first-Back tail. These scopes must
not be conflated or called actual peaks. Battery was 80%, AC power connected,
first-run battery temperature
25 °C, second-run 26 °C during play and 27 °C after return, power saving off and
reported thermal status zero. First-run charging status changed from 3 to 2
before the opening; second-run samples remain 2. AC presence and charging status
are separate. Thermal HAL readiness was false; SoC temperature, headroom, fan
state and GPU clocks remain unknown. Ordinary health retains instantaneous CPU
frequency samples, not sustained residency or headroom. First-run final ordinary
totals record 6,053,127 draws; the second records 9,044,767. Both have zero named
skipped draws and zero fallback failures.

The completed independent timing analysis uses the actual opening and first-Back actions,
constant normal-speed/2x complete windows, and retained clock anchors. It excludes
one ambiguous boundary window rather than prorating it. The 33 accepted windows
cover 166.293 of 172.192 seconds, leaving 5.898 seconds unscored at the boundaries.

| First .26 opening scope | Complete-window duration | Wall-weighted logged speed |
| --- | ---: | ---: |
| All accepted windows | 166.293 s | 23.227% |
| No video overlap | 100.762 s | 23.869% |
| No video or external-observation overlap | 90.708 s | 22.516% |

The subsets contain different scenes and do not measure recording overhead.
The wall-weighted fifth percentile of window means is 16.829%; it is not a frame
percentile. Every accepted window mean is below 95%; their summed duration is
not an exact per-frame time-below-target measurement. The worst recorded system
interval is 225.751 ms. Software submissions/intervals are not physical display
timing. No matched speed improvement over the differently instrumented .25
diagnostic sessions is established.

The second opening's 44 accepted windows cover 221.617 of 228.904 seconds,
leaving 7.287 seconds unscored. Wall-weighted speed is 22.339%, the fifth
percentile of window means is 16.649%, and the worst included interval is
233.936 ms. All accepted window means are below 95%. The 140.999 seconds
without video/other-observation overlap average 21.266%; unlike scene mixtures
prevent interpreting that difference as recorder overhead. The longer second
opening includes more post-clip play, so run-to-run means are not a matched
speed comparison. Both whole-session totals report zero named skipped draws
and zero fallback failures.

Over separate retained PICA report boundaries, 97,675.307 ms of broad
`LoadVertices` time occupies 59.493% of 164.180 s. This includes setup, input/FIFO
work, shader execution, conversion, assembly, allocation and preemption; it is
not JIT time alone. A separate scoped 164.945 s report interval contains 185.548 ms
foreground generic waiting and 89.318 ms scheduler pipeline waiting. Fifteen
optional builds consume 9.142 s background elapsed time, not a serial frame
budget. The first-run log reports 38 optional records omitted; reports falling
inside the opening account for 16, but their reporting times do not identify
each omitted event's exact time or counter family. Some CPU progress reports are
absent, while the 33 selected frame windows are complete. Preserve the distinct
denominators and do not invent the missing progress samples.

The second separately scoped CPU interval records 133.675 s in broad
`LoadVertices` across 222.725 s, or 60.018%. Its lifecycle has 39 omission
warnings reporting 42 optional records; 17 are reported inside the opening.
All 44 selected primary frame windows are complete. This corroborates a large
broad CPU-stage cost but does not isolate JIT execution or establish GPU cost.

<!-- CodexAstraLocal: Keep sparse per-vertex timing local to its selected samples; extrapolation contradicts the independently measured broad stage. -->
Fine-grained samples rank the selected JIT bracket above loading/conversion,
but cannot partition the workload. Their first-eligible-after-50-ms batches and
deterministic indices differ from the population, and clock/template overhead is
uncalibrated. Expanding the second run's sampled shader mean to all invocations
would predict 143.731 s, exceeding the entire matching 133.675 s broad stage;
expanding all sample means is also inconsistent with wall time. Do not use those
means as contribution percentages. The sampled submit bracket is CPU primitive
assembly, not Vulkan queue submission.

Across eight earlier .25 sessions, nominal moon windows observed 32.12–33.97%
speed with broad CPU stage 84.13–84.97% of its own PICA-window wall; early-ghost
windows observed 16.53–18.73% and 42.62–50.13%, respectively. Capture, UI,
recording and scene alignment prevent treating them as matched benchmarks.
Small counted compilation waits do not explain the whole sustained slowdown.
The unmeasured remainder includes command preparation, stream/fence waits,
surface/cache work, presentation and scheduling; it is not measured GPU fragment
time. The overlay's host GSP-processing “GPU time” is not Adreno execution time.
Compute has zero eligible rectangles/timestamp samples in the retained named
scenes, so it supplies no relevant measured acceleration.

## Findings and retained debt

<!-- CodexAstraLocal: Separate confirmed diagnostic errors, unmet acceptance gates and inherited untriggered failure concerns from the observed title's cause. -->

| ID / priority | Source or evidence | Assessment and bounded follow-up |
| --- | --- | --- |
| R26-01 / P2 diagnostic | `vk_pipeline_cache.cpp:938–940`, `vk_vertex_capture.cpp:675–681`: capture's specialization boolean represents optional specialization; false is serialized as generic. | Mandatory specialized recovery may also have false. A future capture can be mislabeled although raw shader identity/rendering is unchanged. All 48 retained payload packets are specialized=true and independently reconstructed. Add a precise route classification and focused regression when diagnostic work is next changed. |
| R26-02 / P1 qualification | Both .26 temporal reviews are positive within their bounds, but opening windows average 23.227% / 22.339% and broader gameplay is untested. | No sustained-100%, 2x qualification, general graphics cure or 0.2.0 claim. Use the bounded thread-cost discriminator below before choosing a performance implementation. |
| R26-03 / P2 inherited error path | `vk_resource_pool.cpp:187–208` retries unexpected descriptor-allocation failures indefinitely. | Preserve as error-path debt; use bounded injected failure to test terminal ownership before changing recovery. Not observed in these openings. |
| R26-04 / P2 inherited lifecycle | `vk_master_semaphore.cpp:133–136` destroys free fences before its member worker joins; the worker publishes completion before returning its fence. | Source-visible shutdown interleaving/failure concern. Normal exits are not injected submit/wait-failure proof. A narrow stop/wake/join and failure test is separate from an image fix. |
| R26-05 / P2 inherited ownership | `rasterizer_cache.h:1243–1265` clears lookup structures without all slot owners; `vk_texture_runtime.h:65–79` move assignment overwrites owned raw resources. | Retained prior-review debt, not introduced by these three builds. Harmful live assignment or repeated-state growth is unproven. Audit call paths and test synthetic ownership before cleanup changes; preserve saves. |

The delivery review found no new blocking Android or logging issue. Existing
signing-key cache custody remains a durability concern: a missing approved key
must fail the certificate pin, never trigger a changed pin or reinstall without
data. The workflow retains exact branch/commit, independent shader and Android
gates, same-run artifacts/checksum, existing-version/wrong-tag rejection and
publication only after both jobs. Positive alpha versions are prereleases;
zero-alpha beta versions are full GitHub releases with `latest=false`. This
classification does not enforce the product's beta evidence threshold.

## Conditional next work and architecture decision

<!-- CodexAstraLocal: Compare actionable discriminators without treating proposals as implemented work or broadening renderer eligibility to improve a counter. -->

The repeated cold confirmation supports moving to a focused throughput
discriminator while retaining the reviewed scene checkpoints. The broad CPU stage is material but already contains
fused input and a hashed FIFO. Distinguish loader/JIT/output/assembly/command work
before choosing another optimization. Preserve full draws, exact input/state,
normal speed and cold application-cache behavior. No performance implementation
is selected by this audit.

<!-- CodexAstraLocal: Select a finite existing-build measurement before another candidate, and record the observed profiling capability and restored tool side effect. -->
The next experiment uses installed .26 with unchanged rendering and cold setup:
two finite process/thread CPU-counter endpoint pairs around ten-second moon and
early-ghost windows, without video. Retain exact PID/TID/starttime identities,
an observed USER_HZ value, bounded reads, actual timestamps and missing-thread
coverage. Thread CPU time can distinguish emulation, Vulkan/compilation, logging
and presentation costs; it cannot split loader/JIT/assembly within one thread
or explain blocked time. Compare ordinary timing against the retained cold runs
without claiming identical scenes or zero observation overhead. A finite
scheduler trace remains conditional on actual advertised sources and permissions.

The release is neither debuggable nor marked profileable. The device's raw
`simpleperf stat` attachment was denied. Its capability invocation also changed
`security.perf_harden` from the retained original 1 to observed 0; root restored
1 and verified it at 08:26:47 UTC. Do not repeat that tool or weaken platform
controls. A future profileable marker only addresses app eligibility, not kernel
policy or guaranteed stack/GPU support, and is not sufficient reason alone to
build .27. No profiling marker, global workaround or new renderer instrumentation
is implemented by this review. If endpoint evidence cannot distinguish the cost,
design a narrow default-off measurement with independent rendering/lifetime review.

If either visual fault recurs, use the following discriminators for that symptom
instead of repeating arbitrary first-family payloads:

| Option | New evidence | Important boundary |
| --- | --- | --- |
| Existing-packet x64 JIT replay | Actual FIFO/carry data under a second production CPU engine; the private prototype is complete. | x64 approximation and FP behavior are not AArch64 execution; retained Mesa data is not Adreno output. Offscreen instances remain offscreen. |
| Bounded same-swap depth/color selection | Actual same-moment inputs, uniforms, attachments and ordering, if matching geometry is established. | Current parser requires one entry; two entries require a narrow explicit selector-set extension within existing caps. Same swap/tick is not a guest frame or proof of adjacent depth history. Current capture sees optional GPU draws, so CPU-only lit color needs a separate explicitly reviewed hook. |
| Exact AArch64 JIT replay | The actual CPU backend's emitted arithmetic over retained inputs with recorded FP controls. | Requires its own runtime/worker and provenance. QEMU tests emitted logic, not physical ARM behavior; neither reproduces Adreno fragments. |
| Full-Combo GPU-vertex isolation | Whether the optional promotion package is necessary for the symptom. | Disabling GPU vertices also removes attached optional fragment specialization; a clean result would not isolate vertex arithmetic. A matched generic-fragment mode-4 control could hold fragment policy constant. No existing `use_hw_shader` toggle provides this diagnostic. |
| Bounded presentation/instance metadata | Better association between projected geometry and the visible target. | Existing viewport dimensions do not prove final transfer orientation. CPU-known transfer/layout metadata could improve the join without new GPU synchronization; actor/pixel association still requires evidence. |

Fragment-only ready specialization for CPU-vertex draws remains an alternative
if measured fragment cost justifies it; it adds PSO/residency and exact-state
ownership demands. Wider lit/topology admission, generalized compute, cross-draw
transformed caching, unbounded prewarming and arbitrary eviction remain behind
their missing correctness/performance proofs. No depth epsilon, skipped draw,
speed-cap change or warm-cache substitution is an acceptable improvement.

## Acceptance gates and completion record

<!-- CodexAstraLocal: Preserve the owner's cold-first-run target, retained beta evidence and fixed three-build cadence while assigning closure to the coordinator. -->

Every opening resets Dark Moon File 1 through its own UI and deletes that title's
application Vulkan cache, verifying Empty and a complete zero-file/zero-byte
startup inventory. A deletion toast alone is insufficient. Each tested game's
cache must be deleted before its test; preserve other saves and the existing
0.1.22 emulator state. Driver-private caches remain unknown. Warm performance
cannot qualify the first-run target, and zero named skipped draws is required.
The coordinator remains the only device operator; independent agents analyze
and review retained evidence.

Qualify full Combo Dark Moon at 2x before 4x and broader gameplay. Repeat Native
only if its process changes. The retained [0.2 throughput gate](UBERHAR_ARCHITECTURE_0.1.6.md)
requires measurably better demanding matched scenes, exact output, the retained
2x FEA baseline and a profiling-supported CPU/GPU/command architecture. Current
Dark Moon-only test scope does not silently waive the FEA baseline or authorize
claiming it has passed. Correct selected opening images alone do not earn beta.

| Completion item | State at completed review |
| --- | --- |
| Renderer, arithmetic, capture/ownership and inherited-debt review | Completed; independent findings integrated above. |
| Android, ordinary logging, package/signing/publication review | Completed; actual .26 gated publication and compatible installation verified. |
| Host regressions and native integration | Passed with explicit firmware skips and host/runtime limits above. |
| First clean .26 temporal result | Bounded positive moon and ghost intervals; ordinary cleanup verified. |
| Wider second cold confirmation | Completed; bounded moon and ghost improvements repeat; independent ordinary-log and temporal reviews integrated. |
| .26 performance decision | Two scoped timing audits integrated; finite existing-build thread CPU endpoints selected next. Sustained 100% is unmet. |
| Full three-build audit closure and review-ledger/roadmap refresh | Completed with this coordinated documentation checkpoint; .26 is the new source audit anchor. |
| 2x qualification, 4x pursuit and 0.2.0 beta gate | Not satisfied by these two bounded openings. |

The source inventory, independent delivery/performance reports, retained release
verification, original videos/logs, actual packets and private JIT provenance
remain under ignored `build/device-testing/20261007-recovery/`. Their summaries
inform this document; guest-derived programs, payloads and visual artifacts are
not published. This document adds no renderer change, device operation or new
build. With full-audit closure recorded, the next completed
candidate becomes successor one; same-APK selectors, documentation checkpoints
and retries do not advance the build counter. Continue the authorized work toward
0.2.0 with an audit every three builds, rather than reinstating the superseded
four-hour stop.

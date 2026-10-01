# Uberhar 0.1.8 device analysis and 0.1.9 decision

<!-- AstraPro: Source-derived evidence, exact admission extension and owner benchmark update. -->

## Decision

Develop 0.1.9: enable ready-only GPU vertices for independent no-guest-GS Shader
triangle lists, with real assembler winding/topology checks and detailed bounded
admission counters. Retain Native as the CPU control. Do not remove fragment
accuracy guards, increase GPU caches speculatively, or combine this with a broad
shader/renderer rewrite. The new device data identifies an actual coverage blocker.

Analyzed binary: `79bb8bc780480bb5843bd69eafc6d5d510d7256f` / 0.1.8.
Development parent: `f40e01c8935cf7269e5434f6677e7853be4988a4`, which only adds the
supplemental 0.1.7 report after that release. Preserve that report in the parent.

## Evidence identity and method

File: `uberhar_log_9_30_1708_FEA_LSMDM_KIU_MH4U_SASRT_LCUTCB.txt`.
SHA256: `20a7a89cc80de2511df4f63a8128b86ae7e09e4e64a8f61edd9df0715d52ccb5`.
Eight complete sessions, six titles, 3536.515 seconds of observed emulation; all
Native or Automatic (Combo), Vulkan, fixed 2x, guest CPU clock 100%. There is no
4x measurement in this upload. Previous 0.1.7 files remain separate; repeated
session numbers across process launches are not duplicate runs.

Use final normal and fast bands independently; exclude mixed-limit windows from
window comparisons. Complete capacity windows require min=max=400 and every
frame temporary. Do not sum cumulative progress into totals, discard slow windows
because they are slow, or infer scenes from speed. All phase labels remain Unknown.
Recognized pauses are already excluded. Device operating conditions and sequence
lengths differ from older tests: version-to-version speed ratios are descriptive,
not controlled treatment effects. Lowest five-second mean is NOT a 1% low, a
physical display FPS, or a proven gameplay floor.

| Session | Title / mode, all 2x | Normal % / observed seconds | Commanded400 % / seconds | Lowest complete normal-window mean |
| --- | --- | --- | --- | --- |
| 1 | Fire Emblem Awakening / Native | 99.586% / 74.886 | Not sampled | 97.451% |
| 2 | Luigi's Mansion: Dark Moon / Native | 27.545% / 793.417 | Not sampled | 14.670% |
| 3 | Kid Icarus: Uprising / Native | 82.012% / 430.989 | 240.338% / 3.220 | 42.629% |
| 4 | MONSTER HUNTER 4 ULTIMATE / Native | 81.162% / 549.213 | Not sampled | 36.760% |
| 5 | Luigi's Mansion: Dark Moon / Automatic | 27.085% / 742.719 | Not sampled | 15.522% |
| 6 | Kid Icarus: Uprising / Automatic | 84.599% / 564.639 | Not sampled | 47.754% |
| 7 | Sonic & All-Stars Racing Transformed / Native | 72.516% / 212.644 | 303.253% / 14.346 | 53.588% |
| 8 | LEGO® City Undercover: The Chase Begins / Native | 97.422% / 2.814 | 276.091% / 147.516 | Not sampled |

Kid Icarus Native's 3.220-second fast band and LEGO's 2.814-second normal band are
too short for qualification. Sonic's 14.346-second fast section does not establish
race capacity. FEA starts with existing driver files supplied to the driver but
zero encountered generic hits/four misses; call this mixed cache context, not a
fully cold-driver benchmark or a fully warm-module run.

## Main architectural evidence

<!-- AstraPro: Count actual routed work rather than the UI mode name. -->

Dark Moon Automatic reports 7,199,403 CPU batches, **zero GPU attempts**, zero GPU
batches and topology counts [0,0,0,7199403,0]. Source enum 3 is `Shader`, not proof
that a guest GS is active. Its native transport reports geometry_fallbacks=0.
The released policy admitted only enum 0 (`List`), so it excluded every recorded
Dark Moon batch before dependency, upload or cache checks. Zero promotion here
cannot be blamed on a saturated pipeline cache or pending compilation.

Kid Icarus Automatic records 2,919 GPU batches / 2,014,472 total batches (0.145%),
280,224 GPU inputs, and 1,985,986 enum-3 batches. Its two ready GPU pipelines did
select successfully: no failed attempts, key mismatches or capped outcomes are
recorded. This validates basic route activation in a very small subset, not broad
GPU shader precision or a meaningful speedup from promotion.

Dark Moon Native accumulates 648.643 seconds in CPU vertex processing (81.75% of
observed wall); Combo 609.044 seconds (82.00%). The corresponding normal-band
means are 27.545% and 27.085%; lowest complete window means 14.670% and 15.522%.
Those runs do not reach full speed, and are not a comparison of CPU versus broad
GPU execution because the GPU path did not run in Combo. Stage durations include
descheduling/waits and are not CPU utilization, GPU timing or a critical-path
speedup prediction. Its generic waits (~494/502 ms totals) cannot explain the
many minutes of sustained slowness alone.

## Index rescue is exercised successfully

All relevant recorded no-GS CPU input invocations now use the fused loader;
legacy_vertices=0 and escaped_vertices=0 in all eight sessions. Rescues include:
Kid Icarus Native 214,978,322 invocations, MH4U 390,251,158, Kid Icarus Combo
347,580,116, Dark Moon Native 3,318,559 and Dark Moon Combo 1,266,973. Sum across
these distinct runs: 957,395,128. This is observed coverage, not measured time
saved. FEA/Sonic/LEGO have no rescue opportunity in this file.

MH4U's 81.162% normal band and Sonic's 72.516% are higher than their prior reported
bands, but scene mixtures, cache state and host conditions differ. Do not claim
those differences as isolated 0.1.8 gains. MH4U still has 76.535% specialized
fragment recovery and 64.11% host vertex-stage wall. Sonic has zero fragment
recovery, 42.57% vertex-stage wall and no GPU promotion because it was Native.
LEGO's 147.516-second accelerated band averages 276.091%, but was also Native.
Their results preserve three distinct optimization workloads.

## Source-grounded extension and constraints

`PrimitiveAssembler::SubmitVertex` treats List and Shader as independent triples
unless Shader has pending winding, which reverses the first two vertices of the
next triangle and consumes the request. `IsEmpty()` does not include that flag.
`PicaToVK::PrimitiveTopology` already maps both enums to Vulkan triangle lists.
Vulkan defines consecutive independent triples for this topology; see the Khronos
Vulkan Specification, Drawing Commands / Triangle Lists. No guest register or
host pipeline key is canonicalized/rewritten by this update.

0.1.9 admits Shader only with no guest GS, no pending Shader winding, matching
register/assembler topology, empty assembly, no debugger, complete triples within
existing size bounds. A new read-only winding accessor does not reset/consume
state. PICA and the Vulkan ready-only entry point query the same real state.
Pending, failed, capped, invalid or unsupported candidates still execute the full
CPU draw; readiness/pipeline identity and framebuffer cancellation are unchanged.
Strip/fan continuation and guest-GS acceleration remain excluded.

Bounded admission reasons explain eligibility, geometry/assembly/winding/topology
rejection and size/partial-triple limits. Accepted topology counts separately
report actual submission. Progress reports count both CPU and GPU batches so a
GPU-heavy run does not starve PICA diagnostics. These are draw counts, not GPU
invocations, pixels or time. No new per-draw clock calls are added.

## Validation and open risks

The full local host suite passes. New tests: 4,992 policy cases including pending
winding, topology mismatch and unknown enums; 200,000 ordered readiness choices;
10,000 draw sequences compared to real uninterrupted PICA assembly, with 6,392
simulated promotions (3,088 Shader), indexed/non-indexed values, partial tails,
winding transitions, deferrals/failures, reset and topology changes. Production
Vulkan topology mapping and framebuffer cancellation/retry checks pass. The new
suite passes ASan/UBSan. Six integration C++ units pass syntax. An initial syntax
attempt preceded generated setting headers; rerun after generation passes.

256 compute/native rectangle pixel checks pass with SPIR-V validation on llvmpipe.
Local TEV pixel comparison was interrupted at the tool time limit after reporting
8,448 outputs; it is incomplete, not a pass. Full fragment/pixel/Vulkan and Android
compilation/unit/package/signing gates remain required in the normal CI workflow.
Host assembly tests do not establish game GPU-vertex numerical/image parity or
Thor performance. Broader admission can increase compilation/upload/driver costs;
existing bounds and Native fallback remain. No full-speed claim is made for 0.1.9.

## Device context and retained work

Battery-temperature snapshots range 24–31 C; Android status remains 0 and thermal
headroom/GPU clock are unknown. CPU current/max samples vary and do not establish
why clocks changed. Do not diagnose or rule out throttling from these values.
There is a large inter-title idle interval; do not count it as game slowdown.
Keep host power/fan/charging settings fixed for short comparisons and avoid a new
marathon just to increase log size.

Prior SRV warning aggregation and modal-wait instrumentation remain queued, not
silently implemented. First-use blocking, ShaderList GPU coverage, later GPU
precision, command overhead and resolution-aware effects remain explicit work.

## Owner milestones and next tests

<!-- AstraPro: New owner emphasis adds functional gates, not a silently reduced headroom goal. -->

Primary performance guides: MH4U, SASRT and Dark Moon. First qualify stable 100%
emulation at 2x on named segments; then qualify 100% at 4x with correct output and
stable pacing. This is a strong Thor performance acceptance milestone, not proof
of maximum optimization, universal game compatibility or other-device support.
Earlier 400%/2x and >=200%/4x headroom remain separate stretch qualifications.
FEA stays a correctness/cold-transition control; Kid Icarus stays the touch and
route-transition control. Cold behavior and image correctness are never traded
away to turn a performance score green.

For 0.1.9: one FEA Native 2x normal-speed check; a short matching Dark Moon Native
2x / Combo 2x pair at 100%; short MH4U and Sonic Combo 2x normal-speed segments.
Use the same in-game start/end landmarks, not identical wall-clock lengths. Exit
and relaunch the title after switching modes. Do not clear all caches for these
throughput comparisons. Test intentional cold behavior separately. An accelerated
segment uses a fixed400 limit throughout and is reported separately; it is useful
when normal speed reaches its limiter. No 4x marathon, character creation or
all-mode matrix is requested. Stop on visual corruption and record the title/mode.

At each future build, include an explicit versioned required/optional test card
based on the changed subsystem. Routine builds: FEA plus the affected stress
title. GPU route builds: matched Native/Combo scenes. Shader/cache builds: matched
cold/warm scenes. Latency builds: Kid Icarus at 100%, not fast-forward. Milestone
builds: all three stress titles, 2x first, then 4x, and a longer steady-state check.

This is a focused admission audit and implementation, not a new full-review reset.
0.1.9 is successor three after the full 0.1.6 review; default next full review after
0.1.10 evidence, allowed after 0.1.9–0.1.11 and before 0.1.12.

Audit anchors (physical source lines): final bands 582, 5145, 7636–7637, 10912,
15181, 18417, 19998–19999, 21048–21049. PICA routes 576, 5139, 7630, 10906,
15175, 18411, 19992, 21042. GPU pipeline totals 15146 and 18387.

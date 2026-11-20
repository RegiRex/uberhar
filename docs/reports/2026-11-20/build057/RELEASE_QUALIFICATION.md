<!-- CodexAstraLocal: Final performance report, sanitized for the owner-authorized public archive. -->
> Public report copy, archived November 20, 2026. Measurements and historical qualifications are retained. Private paths, device identifiers and operational closeout details are omitted; raw captures were deleted during the authorized storage cleanup. References to retained raw evidence describe the original analysis, not its present availability. This archive does not authorize device operations or settings restoration.

# Uberhar 0.1.57 — release and hardware qualification

**.57 is published and installed, including the requested cleanup. The first hosted submission passed every required gate.** This cycle produced useful implementation and diagnostic findings, but does not establish a broad speed improvement over .56. The strongest practical result is a lean Dark Moon configuration; Xenoblade favors Gen8 V38 with Combo CPU Vertices OFF.

[Release and APK](https://github.com/RegiRex/uberhar/releases/tag/0.1.57) · [Successful first workflow](https://github.com/RegiRex/uberhar/actions/runs/42194042949)

The downloaded and installed APK match SHA256 `b964ffb2d22ee77ad9ae7df0cd79eb22330f6579373d136b65bc961c4fab2b76`. Signing compatibility was verified and installation retained application data. No uninstall or signature bypass was used. Published source `d25afc00d9b2379fa60f7a378c095343ae0ed2d5` and the reviewed local release have the same tree `ccf0cc65d34f8c61302ed61148bae8eb578bda85`.

## What changed

Implemented changed-state reuse, compatible contained render passes, typed CPU vertex append, Include-scissor narrowing, revised adaptive histories, independent GPU timing and workload census controls, the V38 static-state comparison, displayed-eye-only presentation preparation and guarded early right-eye suppression. Experiments remain individually accessible in alpha Testing with explicit per-game precedence. Optimize generic shaders is back in quick Testing. The failed Hardware Shader experiment was retired; Linear Filtering remains an ordinary Graphics preference.

The cleanup removed **41 recorded cleanup units across eight areas**, including two old explanatory examples, and archived them by feature outside compiled/packaged sources. Purpose comments and removable diagnostic markers were reviewed. The archives preserve source origin, licenses and restoration dependencies. This improves maintainability; deleting already-commented code does not improve frame rate. Two archive documentation corrections were applied locally after publication and preserved for the next documentation commit; they do not change the APK. Cleanup review (source not included in this compact archive) · Applied corrections (source not included in this compact archive).

## How to read the measurements

These tests use Standard mode, closed known background apps, Wi-Fi OFF, Vulkan and verified settings/cache state. Cold runs have empty application shader caches; driver-internal caching is not observable here. Warm runs retain the immediately preceding application cache. Turbo 400 removes the normal speed ceiling; reported values are **emulation speed percentages**, not FPS. Low/high are the slowest/fastest complete approximately five-second windows, not percentiles or instantaneous peaks.

**Historical comparisons match the shared settings, not an all-new-features-OFF runtime.** The main .57 references enable Reuse unchanged PICA state, Reuse contained render passes, Typed primitive append, Narrow included scissor, Adaptive CPU workers and Prepare displayed eye only. Detailed GPU pass timing also remains ON; its timing machinery already existed in .56. Census, static-state and early-eye experiments are OFF. Even the six-switch package-OFF control retains .57's new register accounting/generation and configuration-storage work; it is not a restored .56 execution path.

**Subsequent matched-baseline correction:** the six-switch OFF control is nevertheless a valid settings-matched build comparison where the common settings match. For stock Dark Moon Combo CPU Vertices OFF, .56 D054→.57 Q08 longer-scene means change+0.13/+2.87/+0.54%, and lows+2.92/-0.60/-0.39%: broadly stable. Unconditional .57 code is intentionally included in this build difference. Do not generalize the all-ON regressions into a claimed OFF-baseline regression. The owner's final clarification preserves all PRE-EXISTING preferences in both prior stock and prior optimal profiles, with NEW performance features OFF. Q08's Optimize generic shaders ON and inherited Detailed GPU pass timing ON match the old behavior. Missing work includes standalone new-feature tests on BOTH prior anchors and interactions; the reduced one-anchor matrix is superseded. Exact baseline and interaction synthesis (source not included in this compact archive).

Dark Moon's opening moon, first lab, ghost/moon and second lab are separate. Xenoblade's titan opening, battle cinematic and stationary playable scene are separate; stationary gameplay is not active combat qualification. Screenshots establish scene interiors and pauses/transitions are excluded. Individual shots and asynchronous shader residency vary between runs, so a single minimum is interpreted alongside means, matched scene content and repeat controls.

CPU0–7 are whole-device utilization, while app core equivalents come from process ticks. The raw CPU accounting sometimes reports physically inconsistent elapsed totals. Those specific core/context cells are marked **N/A—accounting**, never rescaled or presented as extra worker activity. App CPU, speed and GPU measurements are independent of that identified fault. CPU hotspot readings were commonly around 94–95°C, with changing clocks; battery temperature rose during the long sequence. Unchanged frequency caps do not prove absence of thermal throttling. Accounting audit (source not included in this compact archive) · Condition review (source not included in this compact archive).

## Dark Moon: practical result and historical context

**Recommended candidate: stock driver, Combo, CPU Vertices OFF, 2x.** Keep Reuse unchanged PICA state, Typed primitive append and Prepare displayed eye only ON. Turn Reuse contained render passes, Narrow included scissor and Adaptive CPU workers OFF for this candidate. Keep Optimize generic shaders ON, Linear Filtering ON and the existing simulated 3DS GPU timing setting ON. Guest 3D slider 0%, original right-eye disable OFF, early suppression OFF, V38 static state OFF and Frame workload census OFF.

The lean run's first-lab low/mean improves from the nearby default's 57.38/61.49% to 65.15/68.53%. Against the earlier later-condition default reference 63.66/66.59%, the improvement is smaller but still in the useful direction. Ghost/moon also improves; the short second-lab sample does not beat every prior reference. This supports trying the lean profile, not claiming that one of its three disabled options caused all the gain.

The early stock Native gain did not repeat. Later Native long-scene means 58.69/57.53/57.10% closely match .56's58.66/57.67/57.28%. It would be misleading to report the early 19–22% improvement as a durable build gain.

Warm cache reuse is real: three generic module hits, 64 validated optional shader hits and a compatible driver cache were observed. Required generic waits fell 876.8→44.9ms. Most compilation occurs before the scored scenes, explaining why much lower compilation cost does not guarantee correspondingly faster gameplay. Lean warm performance has not been tested.

At 4x, lean first/second-lab means 39.69/35.89% remain essentially the same as default 39.73/35.52%, with GPU activity near 98–99%. The lean CPU-side savings do not resolve that GPU limit. Full scene and low/high tables (source not included in this compact archive) · Lean4x (source not included in this compact archive) · Warm-cache mechanisms (source not included in this compact archive).

## Xenoblade: mode, driver and eye findings

Across the stock/v27/V38/T30 cold 2x matrix, **V38 + Combo CPU Vertices OFF** is the strongest balanced candidate: initial battle low/mean 50.69/93.59%, stationary 97.94/100.66%. Stock CPU-OFF is a useful fallback. Turnip Native and CPU-Vertices-ON stationary means are substantially worse than stock, despite real shader specialization; this is not simply an absent cache or missing CPU worker pool. Complete baseline matrix (source not included in this compact archive).

Optimize generic shaders helps v27 Native's stationary mean 66.33→77.72%. On V38 CPU-OFF it improves one matched demanding battle section but initially lowers stationary throughput; the final cold/warm comparison follows below. V38 static state is clearly unhelpful in this sample: battle mean 94.58→81.15%, stationary 99.25→86.77%, with more generic PSOs and less full specialization. Keep it OFF.

Original Disable Right Eye Render evaluates thousands of commands but skips **zero** lists/transfers at both 0% and 1% Guest3D in these Xenoblade controls. The early suppression probe also omitted no draws. Therefore the faster 0% original-eye-ON run cannot be credited to actual suppression. Keep slider 0%, original right-eye disable OFF and early suppression OFF for the baseline; Fire Emblem's separate 1% behavior still needs direct validation.

The final repeated optimizer-ON cold run did not preserve the initial low advantage: battle low 48.99%, versus 63.90% in the first ON control. Its warm repeat has essentially identical battle/stationary averages 95.86/101.26% versus cold 95.87/101.35%. Real warm reuse is verified: two generic hits, 78 optional hits and a compatible driver blob; required generic waits fall 480.45→5.48ms. Keep Optimize generic shaders OFF in the balanced V38 baseline rather than promoting it from an unrepeated minimum. Final comparisons (source not included in this compact archive) · Warm-cache diagnostic (source not included in this compact archive).

The census reveals a concrete workload change: the selected battle low has about 3.18× as many submitted draws and 2.33× as many vertices per rasterizer callback as the selected high. Two packed target regions have matching draw/vertex counts; that supports investigating duplicated eye work, but does not prove either region is safely disposable. The current whole-target suppression policy does not solve packed subrectangle rendering. Texture descriptor overflow prevents claiming a complete texture-quality or bandwidth explanation. Census (source not included in this compact archive) · Eye controls (source not included in this compact archive).

## What the new mechanisms actually achieved

| Mechanism | Observed result | Recommendation |
|---|---|---|
| Reuse unchanged PICA state | Avoids many repeated fragment-configuration builds; isolated Native speed change small | Retain; useful work reduction, not a claimed large FPS gain |
| Typed primitive append | Executes and has favorable CPU-ON control results | Retain |
| Prepare displayed eye only | Omits one mono presentation preparation; does not suppress guest geometry | Retain; keep distinct from early-eye rendering suppression |
| Reuse contained render passes | Zero scored Dark Moon merges; real Xenoblade coverage but no clear isolated throughput win | OFF in Dark Moon lean candidate; redesign admission around evidenced pass-area behavior |
| Narrow included scissor | No area reduction in these scored Dark Moon interiors | OFF in lean profile; do not equate attempts with saved pixels |
| Adaptive CPU workers | Heavy history activity and no trials in the reviewed Xenoblade references; no accepted change in scored Dark Moon Combo CPU-OFF scenes. Stock Dark Moon Combo CPU-ON did accept one adjustment during the second lab, and its OFF control was slower | Simplify admission/history cost; preserve the CPU-ON exception for comparison before global retirement. Keep static workers |
| Detailed GPU pass timing | OFF control shows no useful speed gain here | Retain for current attribution; switchable |
| Frame workload census | Useful target/draw evidence, but missing summaries and descriptor overflow; Dark Moon slowed in the diagnostic run, while Xenoblade's ordered pair does not isolate overhead | Diagnostic-only; improve compactness/coverage before wider use |
| V38 static state path | Material Xenoblade regression, worse specialization coverage | Keep OFF; candidate for retirement |
| Early right-eye suppression | No eligible omissions in the measured scope | Keep OFF; packed targets require a different qualified design |
| Optimize generic shaders | Driver/mode-dependent result; useful v27 Native and possible V38 battle benefit | Retain as a per-game experiment |

## Recommended next development priorities

**Owner follow-up: resolve the regression before layering on the larger candidates below.** The archived 41-unit cleanup removed commented text only; separate live adaptive/state/pass changes remain plausible causes. Dark Moon V38 CPU-OFF's Adaptive CPU workers OFF control improves first-lab low46.75→51.99% against the nearby ON repeat, but remains below .56's59.73%. Its adaptive sample timer begins after history selection/preparation, so reported work time excludes that bookkeeping cost. First qualify a genuinely cheap OFF path and measure net adaptive cost, including selection/eviction and actual applied decisions. Do not treat counters of attempted optimizations as measured savings or globally retire functioning static workers. Exact cleanup/runtime diff audit (source not included in this compact archive) · Existing adaptive control (source not included in this compact archive).

The low-window review strengthens the CPU-side investigation. Dark Moon first-lab V38 low59.73→46.75% coincides with app2.224→2.585 cores, CPU7 95.1→97.9%, and GPU busy82.2→75.2%; all new driver lows occur in the first scored five-second window. The v27 low57.68→51.75% and T30 low65.61→57.09% also show higher app CPU and lower GPU busy. Old T30's minimum is a different shot, so its full13% deficit is not an exact-workload attribution. No discrete generic wait overlaps these lows; V38/T30 scheduler GPU-wait increments are zero and v27's6.94ms is too small to account for its five-second speed deficit. Generic selection does not rise uniformly (V38's bracket falls). Xenoblade V38 stationary low107.13→97.88% has app1.407→1.460 cores, GPU86.1→82.3% and a sampled CPU7 clock2726→2477MHz; its generic-route bracket rises35.94→41.06%. Source bookkeeping, actual clock residency and readiness/route composition remain separate candidates; the sparse clock endpoints and counters do not allocate a percentage of regression to each.

1. **Reduce actual GPU shader work.** Full CPU-layout fragment PSOs have a resident 8-entry bank; hot entries can fill it while credits remain available. Trial a bounded 8→16 bank with existing compilation/lifetime safeguards. Separately port the existing GLSL dead-work analysis to the direct-SPIR-V full shaders that actually execute. CPU preparation of a shader is not CPU execution of its pixels.
2. **Reduce avoidable pass breaks.** The active pass area is derived from the current viewport. Alternating disjoint packed regions may end otherwise-compatible passes. Test a safely chosen union before beginning the pass, preserving real scissor/viewport, attachment validity, required barriers and submission opportunities. Larger regions can increase tile load/store cost, so expose both saved boundaries and new attachment-area cost. This inherited behavior predates .48; it is an opportunity, not an established explanation of the later regression.
3. **Move useful CPU work off the owner thread.** The actual profile confirms roughly 46% of user CPU in vertex workers and 45% in the owner. Input/output transport and owner-side packing are actionable; scheduler self-time is small. Reuse the existing pool and preserve output ordering, primitive tails and GPU lifetime ownership.
4. **Make adaptation earn its overhead.** Xenoblade generated many samples/evictions without a trial. Fix the admission/identity lifecycle and measure bookkeeping cost versus accepted benefit, or bypass it. Keep static multicore execution regardless. The stock Dark Moon CPU-ON control is an important exception: one accepted adjustment falls inside the scored second lab, while OFF lowers longer-scene means by about2–6% and lows by about4%. That accepted change cannot explain earlier scenes, and acceptance may allow a lower helper count with up to2% extra local latency; it is not automatically a measured frame-rate improvement. These results do not support claiming that every adaptive path is inert or that global removal is already qualified.
5. **Improve evidence where it changes decisions.** Stable shader/config IDs joined to pass samples, bounded complete census aggregates, and explicit early-eye refusal reasons would distinguish expensive code, pass boundaries and unsupported target layouts without another indiscriminate logging flood.

These are targeted follow-up candidates, not new implementation claims. No additional alpha is being built as part of this closeout. GPU route audit (source not included in this compact archive) · Pass-area proposal (source not included in this compact archive) · CPU profile and guardrails (source not included in this compact archive).

The shader-bank ceiling is not a complete explanation of Native versus Combo CPU-OFF. Their sampled pass-change ratios and draws per pass are nearly equal, and only about 2.6% of Combo CPU-OFF draws use ready GPU vertices. The pixel-weighted cost of those promoted draws remains unknown; draw-count shares cannot establish equal GPU costs. The census's timed reporting samples also do not measure its entire runtime overhead. Prioritized implementation proposals (source not included in this compact archive) · Closed early-eye probe (source not included in this compact archive).

Memory review found ample available RAM, negligible pressure stalls and no new OOM in its finite reference set. RSS and KGSL allocation overlap and must not be added. Actual DRAM bandwidth and per-GPU-core utilization are not available from these captures; guest transfer byte counts are not substitutes. Memory/conditions (source not included in this compact archive).

Owner follow-up remains Fire Emblem and Sonic, especially Fire Emblem 0/1% with matched enemy-attack sections and Sonic's later track. Dark Moon/Xenoblade controls cannot qualify those games. Input-latency research was completed after submission and is recorded separately; no latency change was included in .57. Research (source not included in this compact archive).

## Final resolution and resource comparison

The matching Xenoblade V38 optimizer-OFF 4x run completed all three phases. Low/mean/high speeds are 24.53/53.09/83.18% for the titan opening, 18.36/41.32/79.12% for battle cinematics, and 40.29/41.01/42.36% for the stationary scene. GPU means are 98.81/96.27/99.59%, while app CPU falls to 0.41/0.65/0.58 core equivalents. This is a strong GPU-throughput limit for the selected profile; it is not evidence that CPU workers stopped functioning. Its resolution-only reference is optimizer-OFF 2x, not the optimizer-ON cold/warm pair.

Selected Dark Moon tables, low/high resources and All-Core table (source not included in this compact archive)

Dark Moon scene comparison (source not included in this compact archive)

Dark Moon first-lab All-Core comparison (source not included in this compact archive)

Final Xenoblade tables, low/high resources and All-Core charts (source not included in this compact archive)

The full session contains 59 completed runs: 30 Dark Moon and 29 Xenoblade, including three dedicated diagnostic runs. The separate setup-only attempt and partial early-eye probe are excluded from full-run performance rankings. All requested stock/v27/Gen8 V38/T30 baseline modes, targeted feature controls, cold/warm comparisons and selected 4x follow-ups are retained. Run inventory (source not included in this compact archive).


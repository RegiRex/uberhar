# Uberhar 0.1.7 evidence and 0.1.8 implementation decision

<!-- AstraPro: Source-grounded seven-session review, preserving owner annotations and unknowns. -->

Date: September 30, 2026. Device-tested source: `e8b53b12db77b7b8f0345cbcb81038947782baf7` (0.1.7).
Source log: `uberhar_log_9_30_1337_FEA_KIU_MH4U_LSMDM_MLSSBSM_SK2DC.txt`.
SHA-256: `35669b5afb96704cff9a9fba1501a003191f66a4888ffa01962f10265fefedba`. Raw text has 16,101 non-trailing lines; the file retrieval surface reports 16,102 including its trailing line.
This report distinguishes observations, inferences, implemented changes, host tests, CI gates and device acceptance.

## Scope and comparability

Seven sessions cover six games, all Native/Vulkan, guest CPU clock 100%, CPU vertex JIT enabled, hardware-vertex setting false. The two FEA sessions are **4x**, not 2x. All remaining sessions are 2x. Approximately 52m29s of observed emulation time is retained, excluding recognized pauses. No Sonic or LEGO gameplay session appears in this log; deleting their caches is not a gameplay test.

Owner annotation: the Thor felt hot during Kid Icarus; enabling host Performance mode late in the run appeared to help the boss. Performance mode was also tried during the Dark Moon opening cutscene, and that run was stopped early because it was too slow. These observations are retained. There is no timestamped host-mode switch or matched before/after scene in the log, so neither the size of its benefit nor thermal throttling is established.

All logged scene phases are Unknown. The Dark Moon cutscene label comes from the owner, not an automatic detector. Menus, loading, cutscenes and play are not silently relabeled from speed. Normal and fast bands exclude cross-limit intervals; constant-400 window selections require identical min/max=400 and all frames temporary. Repeated cumulative progress counters are not added to final totals. Explicit long pauses remain excluded; unexplained no-submission stalls remain visible and unclassified.

| Session | Title / Native resolution | Normal speed / observed seconds | 400%-commanded speed / observed seconds |
| --- | --- | ---: | ---: |
| 1 | Fire Emblem Awakening / 4x | 92.529% / 81.971 | Not sampled |
| 2 | Fire Emblem Awakening / 4x | 98.025% / 1.159 | 119.275% / 63.926 |
| 3 | Kid Icarus: Uprising / 2x | 76.043% / 559.479 | 89.625% / 1.660 |
| 4 | MONSTER HUNTER 4 ULTIMATE / 2x | 58.689% / 472.626 | 43.612% / 119.645 |
| 5 | Luigi's Mansion: Dark Moon / 2x | 21.974% / 282.638 | 19.252% / 93.935 |
| 6 | Mario & Luigi: Superstar Saga + Bowser's Minions / 2x | 99.753% / 552.618 | 248.641% / 549.720 |
| 7 | SENRAN KAGURA 2: Deep Crimson / 2x | 97.560% / 313.973 | 304.178% / 54.524 |

The 1.159 s normal band in FEA session 2 and 1.660 s fast band in Kid Icarus are too short to qualify normal-play stability or sustained fast-forward, respectively. Game-submission FPS is not emulated-system speed, panel presentation rate or touch-to-photon latency. The normal and fast bands often contain different scenes. Lower performance in the fast band does **not** prove enabling fast-forward caused the slowdown.

For complete constant-400 five-second windows: FEA warm 4x has 11 windows / 55.131 s / 105.712% weighted mean; MH4U has 22 / 110.450 s / 42.658%; Dark Moon has 15 / 75.530 s / 17.371%; Mario & Luigi has 102 / 510.426 s / 248.518%; Senran Kagura has 6 / 30.026 s / 300.365%. Kid Icarus has none. These are window means, not frame-time percentiles or an asserted gameplay floor.

The prior MH4U accelerated intro averaged 87.785% over 706.835 s. This run's fast section is 119.645 s with different scene/time and device conditions. A 43.612% result is important but is **not a measured version regression of a matched workload**. The prior log started near 70% battery, unplugged and 21 C battery reading; this log starts near 9%, charging and 36 C. Battery temperature is not the CPU/GPU temperature, and those conditions do not establish a cause.

## Architecture evidence

| Session | CPU vertex-stage wall time / observed wall | Fused share of no-GS invocations | Specialized fragment recovery / all bound draws |
| --- | ---: | ---: | ---: |
| 1 | 17.024% | 100.000% | 7.498% |
| 2 | 24.129% | 100.000% | 6.808% |
| 3 | 31.629% | 31.357% | 0.000% |
| 4 | 68.989% | 73.456% | 72.523% |
| 5 | 81.248% | 99.906% | 4.515% |
| 6 | 6.703% | 100.000% | 0.000% |
| 7 | 14.196% | 100.000% | 20.235% |

The first numeric column measures accumulated **host wall time inside CPU vertex processing**, including descheduling/stalls. It is not utilization, pure shader arithmetic, or GPU execution time. Draw counts and first-failing recovery reasons are not pixel- or cost-weighted.

### Kid Icarus: conservative transport rejection

Of 337,442,423 no-geometry shader invocations, 231,632,048 (68.643%) used the legacy input loader. 140,028 batches failed with `short_mapping`; the admitted 1,282,155 batches still carried fewer invocations because batch sizes differ. CPU vertex-stage wall time was 177.506 s. This exposes a real missed-optimization mechanism independent of the unproven heat attribution.

Source inspection shows 0.1.7 conservatively asks whether the full 8/16-bit index domain fits each pinned attribute mapping. A draw referencing a much smaller mesh may fail that guard even though all its actual accesses fit. This is a hypothesis about which device failures can be rescued, not proof all 140,028 will be rescued. 0.1.8 records actual outcomes.

Kid Icarus had 4,190.875 ms aggregate generic waiting, largest 342.294 ms. Its 7,352.724 ms maximum system-frame interval occurs early, roughly 29.4 seconds after launch, with no game submission in that interval. Do not identify it as the late boss thermal event, shader compilation, or touchscreen latency without a trace.

### Dark Moon: sustained vertex execution, not just cold shaders

The opening generated 2,376,483,750 input indices and 1,066,656,323 actual CPU shader invocations. Fused input already covered 99.906% of no-GS invocations. Vertex processing accumulated 306.829 s of host wall time, about 81.248% of observed emulation wall time. Generic fragments handled 95.485% of bound draws. The 765.017 ms aggregate generic wait and 300.890 ms worst generic wait are real first-use issues but do not explain a multi-minute 16–22% speed region by themselves.

Therefore, extending fused-input coverage alone cannot be our main answer for this title. A bounded, reversible experiment moving eligible vertex work onto the host GPU is the selected architectural step. Its scene topology, driver cost and precision must still be measured; the existing log cannot establish future GPU coverage or a speedup.

### MH4U: a different fragment coverage problem

4,554,195 of 6,279,673 bound draws (72.523%) used specialized recovery. Exclusive first-failing reasons: Shadow2D 2,436,240; AddSigned 2,011,574; SPIR-V incompatibility 104,014; gas fog 2,367. No unknown/generic-failure bucket explains this count. **These draws were rendered, not skipped.** Removing admission guards to force them generic would be an accuracy change, not a proven performance repair.

The log separately reports unimplemented gas rendering twice. Keep it on the compatibility ledger; 0.1.8 does not claim a gas-fog fix. CPU-stage wall time is also large (408.645 s); 26.544% of no-GS invocations used legacy input. Thus a shared vertex experiment can be relevant without pretending that it implements missing fragment behavior.

### Lighter and draw-heavy controls

FEA retains its 4x limitation: cold normal mean 92.529%, warm accelerated mean 119.275% at a 400% limit; no new 2x regression sample exists. Cold generic waiting remains 834.562 ms total / 227.188 ms maximum, versus 75.687 / 7.321 ms warm. The enlarged suite does not certify first-use completion or high-resolution ghosting.

Mario & Luigi sustains near normal speed and supplies a long headroom sample (248.641% fast band), while CPU vertex processing is only 6.703% of aggregate observed time. Its roughly 19 input indices per draw on aggregate make it a useful small-batch/control workload rather than evidence every game needs GPU vertex promotion.

Senran Kagura supplies another near-normal control and a short fast band. Its recognized 1,000.622 s frontend pause is already excluded. An additional early 7,200.524 ms no-submission system interval remains unclassified; do not erase it as a pause or call it physical display latency.

## Thermal and measurement limits

All 107 device-health observations return thermal status 0 and unknown thermal headroom. This is **inconclusive**, not confirmation the Thor was not throttling. Android's official Thermal API documentation explicitly warns that some devices return THERMAL_STATUS_NONE regardless of actual throttling. Source: https://developer.android.com/games/optimize/adpf/thermal?hl=en (reviewed September 30, 2026).

0.1.8 adds read-only current/max CPU frequency context, explicitly reports host Performance mode as unobserved and GPU clock as unknown, and corrects the previous `uptime_ms` label by logging elapsed realtime and true uptime separately. Zero-valued native heap samples become unknown. Frequency snapshots can still be unavailable, policy-limited or momentary; a lower frequency is not alone proof of heat. No root, new permission, hardware overclock, fan/power adjustment or safety override is performed.

## Implementation decision: 0.1.8

<!-- AstraPro: Two reversible execution changes, plus measurement needed to judge them. -->

**Native / Compute input rescue.** Only after conservative range rejection, validate and scan the actual index buffer once (maximum 262,144 indices), then retry bounded input preparation using the observed largest index. Live indices above that bound take legacy decoding. Do not persist memory references beyond a batch or change scalar conversion/defaults/register aliases/FIFO/assembly/order. Per-batch setup and scans have costs; rescued coverage and net benefit remain device measurements.

**Automatic/Combo ready GPU vertices.** Use Azahar's inherited programmable GPU vertex shader and fixed geometry fixup for eligible no-guest-GS independent triangle lists. Keep Native and Compute CPU paths as controls. Admission requires an empty CPU assembler, no debugger, 96–65,535 input indices, complete triangle lists and a 4 MiB maximum speculative vertex upload. Preserve legacy handling when source ranges/layouts are uncertain.

Only completed, successful, full execution-identity-matching shader/pipeline objects may replace CPU rendering. One optional vertex shader build and one dedicated serial GPU driver-pipeline build may be in flight; at most 128 programmable VS configs, 128 fixed GS configs and 256 optional GPU pipelines per cache. Failed or pending preparation returns to the full CPU draw. Hashes are followed by exact consumed-state/module comparisons. Failed speculative framebuffer preparation explicitly cancels ownership invalidation, so a CPU retry sees the correct surface. Title/reset/shutdown drains optional consumers before destroying shared modules or driver cache objects.

The route reuses the existing generic or specialized fragment implementation, full per-draw runtime state, pipeline layout and ABI7. It does not implement a full compute renderer, a GPU vertex interpreter, a calibrated fastest-path scheduler, generalized Shadow2D/AddSigned/gas support, a 4x ghosting correction or latency improvement. Foreground VS source generation, vertex/index preparation and uploads can still cost time; generic CPU first-use waits remain; driver compilation can contend even without a foreground wait. Device precision and visual parity against CPU remain open.

**Alternatives not selected now.** Another input-copy-only iteration misses the almost-fully-fused Dark Moon workload. Broad generic fragment admission changes risk output accuracy and do not address Dark Moon's measured CPU stage. Globally enabling ordinary blocking hardware shaders risks losing cold behavior and inherits topology limitations; the bounded ready-only route is intentionally opt-in. Full compute rasterization/vertex interpretation, command/descriptor batching and bank prewarming remain separate designs requiring their own evidence and tests. A title-specific CPU-clock or model override is not justified by this log.

## Validation and acceptance

Implemented and locally passed: 4,099 actual-index/rescue differential checks plus 36,864 fused-input bitwise comparisons; 704 routing admission checks; 200,000 ordered GPU-or-CPU decisions including failures/mismatches; production FramebufferHelper cancellation/retry ownership checks; address/undefined-behavior sanitizers on new test paths; existing complete host suite including 998,282 bitwise vertex conversions, 23,318 preparation comparisons and 175,223 queued push-state model draws. These are host correctness tests, not measured device speedups.

Six PICA/Vulkan/core integration translation units pass syntax checks using actual repository-generated shader headers and dependency headers. The final repeated-cache-load drain guard also passes a separate final pipeline syntax check. Pure Kotlin sensor-value/call-budget smoke tests pass; the Android wrapper and JUnit tests require Actions. 1,066 unoptimized and 1,066 optimized Vulkan fragment modules pass validation, preserving DontUnroll and the 128-byte ABI. Full Mesa pixel comparisons are tracked separately in the handoff validation record; generating a corpus or validating SPIR-V is not a pixel-execution pass.

Existing Actions shader/pixel/Vulkan, Android compilation/unit tests, package/signature and prerelease gates remain required and unchanged. Do not mark an APK ready until publication succeeds. Do not count the read-only dependency-toolkit runs as renderer builds. Thor 0.1.8 performance, graphics and real screen/input timing are not yet tested.

## Device acceptance and roadmap

Let the Thor cool and use the same host performance/fan/charging setup across each paired comparison. Stop if the device becomes uncomfortable; do not force a long heat-soak run. Test a short identical Dark Moon opening at Native 2x versus Combo 2x, then a repeatable Kid Icarus segment and FEA control. Keep each measured speed limit fixed; normal 100% answers usability, 400% answers available capacity, and 4x/200% is a separate intended-experience check. A short matched MH4U scene is useful; do not repeat creation or another full seven-game marathon.

Native alone evaluates input rescue. Combo adds GPU promotion; route counters must confirm nonzero accepted GPU draws before claiming that architecture was tested. Ordinary Compute remains a rectangle-subset experiment and does not need another full matrix. Report missing/shifted geometry, animation/lighting changes, shadow problems, new stalls and pause/resume/title-change issues.

The 0.2/0.3 throughput and compatibility tracks advance in implementation, not in device acceptance. Keep 2x/400% and 4x/>=200% as named-scene objectives, not a promised universal capability. Touch/screen latency remains 0.4 and resolution-aware FEA effects remain 0.8, with hardware feedback continuous. 1.0 requires stable declared scope plus honest headroom qualification; difficult titles stay visible. This focused design/evidence update does not reset the last full 0.1.6 review: 0.1.8 is successor two, next default after 0.1.10, allowed 0.1.9–0.1.11, before 0.1.12 if the beta remains 0.1.

## Attribution and audit

New sections/comments use **AstraPro**, as requested by the owner for this development turn. Existing AstraEH and upstream authorship are preserved. The marker is project attribution following the owner's stated selector, not a claim that code can automatically detect ChatGPT product settings.

Source-log audit anchors (1-based raw lines): settings starts 171 and subsequent session headers; normal/fast summaries 599,1127–1128,3898–3899,6885–6886,8891–8892,14344–14345,16082–16083; recovery totals 578,1106,3877,6864,8869,14323,16061; vertex input totals 593,1121,3892,6879,8885,14338,16076. Raw private logs are not committed to the public repository. The conversation review package retains reproducible extraction and exact source hashes.

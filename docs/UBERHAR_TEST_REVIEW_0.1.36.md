# 0.1.36 Thor test review

<!-- CodexAstraLocal: Close one control per method against the delivered source;
private game-derived evidence stays outside the public repository. -->
Released source `b438a453925d27b991ecaedda89e2a66e9e51734` passed all 27 required
steps in [Actions run 37924207205](https://github.com/RegiRex/uberhar/actions/runs/37924207205).
Independent source, APK, checksum and signing checks preceded a compatible
`install -r`. App data and the protected .22 state were preserved.

Native and full Combo each completed one Dark Moon opening: fresh app process,
File 1 reset in game, title Vulkan cache deleted, Vulkan 2x, normal 100% limiter,
CPU clock 100%, CPU JIT and accurate multiplication. Ordinary launch inventories
confirm empty application caches; driver-internal state is unknown. Menus/setup
preceded each Start, including about seven minutes in Combo. Both returned
normally; idle timeout was restored to 60 seconds and both panels verified off.
No Calculated run or routine post-exit memory sweep was performed.

## Speed and useful CPU work

<!-- CodexAstraLocal: Compare observed populations without declaring a causal
version/mode gain; invocation fractions and inclusive wall are different units. -->

| Measure | Native | Combo |
|---|---:|---:|
| Start to first Back | 261.511 s | 220.996 s |
| Complete frame-report windows | 51 | 43 |
| Reporting wall | 256.620 s | 216.482 s |
| Mean normal speed | 30.500% | 30.411% |
| Wall-weighted p05 window-average speed | 17.553% | 19.412% |
| Worst reported frame interval | 205.410 ms | 403.560 ms |
| Separate inclusive LoadVertices / wall | 60.882% | 52.086% |
| Certified fraction of same-span completed attempted CPU invocations | 92.097% | 93.034% |
| Worker fraction of that same CPU population | 66.533% | 68.723% |

All complete reporting windows remain below 99%. Different scene progression,
temperatures, setup and observer populations prevent a causal comparison with
.35 or between methods. P05 describes window means, not individual frames.
LoadVertices includes preparation, worker synchronization and ordered submission;
it is not shader-only CPU time, and its remainder is not measured GPU time.

Excluding possible video/observer overlap leaves 28 Native windows averaging
30.856% and 16 Combo windows averaging 29.275%. These are different scene subsets,
not estimates of recording overhead. Native's remaining worst interval is
123.912 ms. Combo's 403.560 ms event remains outside those observer windows;
its cause is not assigned. Optional log omissions remain explicit rather than
zero-filled: 70 Native and 90 Combo records reported over their lifecycles.

The aligned CPU-counter spans contain 1,163,700,351 Native and 853,512,122 Combo
completed attempted invocations. All certified work uses the selected-output
contract; carry refusals are zero. This establishes broad admission without
claiming full unused-local-state equivalence. No allocation/startup failures or
reported draw skips/fallback failures occurred. Combo's ready GPU vertices have
an unmeasured invocation count and are excluded from these CPU denominators.

Seven stable named workers consumed 13.68/13.80 CPU-seconds in Native's two
roughly 20-second samples, and 10.18/15.84 in Combo's. Their combined average is
about 0.684/0.690 and 0.510/0.792 accounted cores, respectively. Whole-process
CPU time was 29.56/31.11 and 30.02/37.80 seconds and already includes the workers.
Created thread count, invocation share and high device-wide GPU driver windows
do not establish heavy use of all cores or maximum GPU capacity. The primary
multicore throughput objective remains unmet.

## Graphics and conditions

<!-- CodexAstraLocal: Keep decoded-frame counts, missing scenes and ghost
disturbances distinct from the owner's moon-flash priority threshold. -->
Every retained decoded frame was reviewed: Native 540 moon/734 ghost;
Combo 530 moon/661 ghost. Native contains three isolated one-frame moon
block/band events, below the owner's moon-priority threshold. Native ghost
footage contains seven separate rectangular events: six singles and one lasting
three consecutive captured frames. The latter clears in the next frame; the
PTS span to that clear frame is about 184 ms. Captured frames are not proof of
guest-frame counts or physical-panel persistence. No cause is assigned.

No discrete moon/ghost block event or prior dense irregular ghost corruption
was identified in Combo's covered footage. Native lacks the early quiet near
ghost sequence; Combo lacks the later bucket/left-book close shot implicated in
Native. These unequal populations do not establish a mode/version frequency
improvement or complete visual correctness. Ghost findings remain a separate
correctness issue regardless of the tolerated moon threshold.

Battery readings were 23–25°C Native and 26–27°C Combo, AC powered, saver off,
reported thermal status zero. Battery temperature is not chip temperature;
fan policy and maximum thermal headroom are not qualified. During-play KGSL
was 1075.297→1077.406 MiB Native and 2812.156→3173.465 MiB Combo; RSS was
885.352→905.730 and 966.375→983.695 MiB. These endpoints establish observed
growth, not a peak or long-game leak bound. No post-exit recovery claim is made.

## Next engineering step

<!-- CodexAstraLocal: Admission is no longer the leading count-based obstacle;
retain cold-run correctness while measuring and reducing real dispatch cost. -->
At least 90.2% of Native's certified draws fit in one storage chunk. Within-draw
chunk overlap therefore misses most draws by count, although that is not a
time-weighted bound. Investigate sleeping worker dispatch/join, per-grain state
setup and ordered-owner preparation with exact output checks and representative
small batches. Add sparse phase timing so the next device run can discriminate
these costs. Do not add busy waiting merely to increase CPU usage.

A separate source audit found that software AddTriangle can write guest memory
aliasing later vertex inputs. Parallel plan-ahead must require an actual sink
that defers those writes; this is not an observed Vulkan/Thor failure. The next
candidate includes that conservative guard. Independent Calculated texture
and ARM64 narrow-return work remain privately validated secondary tracks,
without title-performance claims. Sustained 99%, 4x and broader beta coverage
remain unqualified. Both .36 controls are closed and will not be repeated.

# 0.1.58 reconciliation and implementation decisions

<!-- CodexAstraLocal: Reconcile approved priorities against existing code before introducing new mechanisms. This is implementation scope, not measured hardware improvement. -->

The goal remains correct cold-cache rendering without skipped visible draws,
with sustained 99% normal speed, first at 2x and ultimately 4x. CPU utilization
is useful when it completes necessary work faster; additional busy workers alone
do not qualify an improvement. Existing static workers, the whole-draw coordinator,
input recipes and deferred uploads are reused rather than duplicated.

| Recommendation | Decision in .58 | Work saved or limitation |
|---|---|---|
| Cheaper CPU vertex conversion | Separate **Fast CPU vertex conversion** control; complete input/output recipes, exact irregular fallback | Fewer repeated address calculations and intermediate output writes. Both ordinary CPU execution and captured packets consume it. |
| Fuse conversion and packing | Under the same control, complete independent packet triangles use bounded 96-vertex scratch instead of a full converted-output vector | Retains the existing primitive/quaternion writer. General direct-to-hardware conversion is not equivalent because assembly consumes the intermediate vertices. |
| Reuse repeated shader results | Separate **Reuse independent vertex results** control, within already certified immutable independent chunks | Bounded exact-index memo supplements, but preserves insertion/eviction of, the existing 64-entry FIFO. No cross-draw result cache. |
| Reduce repeated packed vertices | Separate **Indexed CPU output** control | Exact packed-byte equality and original triangle order; reduces uploaded vertices when savings exceed the admission threshold. Does not save the original shader executions or packing. |
| Move ordinary upload copies off the owner | Separate **Worker CPU vertex upload** control, existing recorder worker | Move ownership of the packed vector rather than copy it to submit another copy. Four slots, 8 MiB retained allocation budget, nonblocking admission and synchronous fallback. Indexed scratch is excluded. |
| Eliminate provably invisible triangles | Separate **Reject proven invisible CPU triangles** control | After all guest vertex work, stable compaction of finite positive-W triangles wholly beyond one common X/Y clip plane. Unknown/nonfinite/subnormal/depth cases retain the original draw. No approximate area test. |
| Repair adaptive bookkeeping | Existing **Adaptive CPU workers** control | Less frequent qualification, bounded rejected-trial cooldown and stale-trial expiry. OFF bypasses the history and keeps static workers. |
| Get useful small work off the owner | Existing overlap + adaptive controls jointly enable cost-qualified coordinator seeds | At least 48 actual FIFO misses and the existing total arithmetic floor; no general tiny-job wakeups, new pool or wait to collect batches. Coordinator work and helper work are distinct metrics. |
| Full pipeline bank 8→16 | Separate **Full pipeline capacity: 8 / 16** control, OFF=8, ON=16 | Partial bank remains 8. Attempt limits, compiler credits and GPU lifetime guards remain; larger capacity does not promise useful hits. |
| Apply dead-work pruning to actual specialized SPIR-V | Separate **Prune unused specialized shader work** control | Uses existing TEV dependency knowledge in the SPIR-V emitter, including stage-zero Previous, delayed buffers, Dot3 alpha and live lighting. Profile enters disk identity. |
| Repair contained render-pass reuse | Existing **Reuse contained render passes** control | The next compatible pass may start with a bounded gap-free union. Preserve LOAD/STORE, exact scissor, hazards and actual submission opportunities. Never expand an active pass. |
| Reuse unchanged PICA state | Retain existing control and consumers | Existing generation checks already remove repeated configuration construction. No second state cache is added. Coverage does not alone establish speed. |
| Typed primitive append | Retain existing control | Already avoids erased getter dispatch for eligible ordered work. No demonstrated source cause yet explains its section-specific regressions; .58 does not claim those are cured. |
| Narrow included scissor | Retain and measure actual effect | Report before/after bounds, empty/no-change counts and avoided bound area. These are not measured shaded pixels. |
| Mono guest 1% path | Separate **Mono guest 1% path** control | With actual mono-left and saved slider 0%, both guest input interfaces receive 1%. Saved depth and real stereo win. This selects guest behavior; it does not automatically eliminate second-eye draws. |
| Reduce early-eye checking overhead | Existing early suppression control | Cheap title/mono/address/shape refusals precede texture enumeration; no read-range scan when nothing was omitted. |
| General offscreen right-eye elimination / Dark Moon | Not enabled by this build | Transfer destinations and paired rectangles do not prove absence of shared color/depth/texture consumers. Existing title veto remains. Safe extension needs content generations and materialization before all reads/writes, including CPU/transfer aliases. |
| Observe eye consumers | Existing census now includes sparse actual LCD consumer snapshots | Join addresses/active slots/extent/format with transfer/target evidence. Explicitly not complete content lineage or permission to omit a draw. |
| Adjacent draw merging | No duplicate implementation added | State, uniforms, textures, topology and ordering must all remain equivalent. Current bounded pass reuse removes transitions without pretending it merges independent draws. General merging lacks an established compatibility witness. |
| V38 static-state override | Archived and removed | Feature-specific restoration patch retains provenance outside built sources. Other Turnip and optimized-generic controls remain. |
| Genuine short dips and peaks | Compact one-second sections inside retained five-second windows | Six shortest and longest paired host/guest intervals per section and session; actual durations and partial/context-crossing flags. No fabricated subdivision of old data. |
| Turn testing into recommendations | Advisory comparison engine and session journal | Baseline primary, explicitly selected optimum secondary; no settings application/restoration or guessed optimum. |

All eight new experiment controls default OFF, are launch-owned, appear in alpha
Testing, and retain global/title inheritance. Existing controls keep their saved
values; repairs beneath an existing ON control intentionally change its behavior.
Fast conversion also controls the packet scratch change. Disabled experiments
retain their old algorithm, but this is not a claim of a byte-identical .57 binary
or zero overhead from added fixed bookkeeping and telemetry.

CPU-layout full fragment specialization still executes fragment work on the GPU.
It must not be described as CPU pixel rendering. Its lifetime token does not by
itself invalidate packed CPU vertices, so the transport optimizations preserve
that token while excluding actual incompatible geometry/compute/capture consumers.

## Measurements and next hardware decisions

<!-- CodexAstraLocal: Record each optimization's consumer and cost so a higher coverage count cannot hide a new bottleneck. -->

Use existing inclusive vertex/packing/upload times together with new recipe, memo,
deduplication, upload ownership, clipping and rejection counters. Worker upload
reports owner admission and recorder copy separately; lease capacity and final
completion matter. Memo hits must join FIFO hits and actual shader executions in
the accounting. Pass unions report requested versus started area and refusal
reasons. Pipeline coverage must distinguish full, partial and generic routes.

Keep five-second Low/Overall/High averages separate from actual interval extrema.
The new paired guest-tick/host-time intervals are system frame boundaries, not a
claim that every game produces a unique rendered frame at each boundary. Compact
logging uses the existing writer, finite nonblocking queue and chunked files;
drop/error/delivery counters make loss visible. Chunking is not a recording cutoff.

For the first device comparison, match the .57 baseline's driver, renderer,
resolution, legacy settings and diagnostic settings with all new controls OFF.
Then compare relevant single additions and evidence-selected pairs. Keep driver
selection separate from the settings optimum. Preserve the selected reference
profiles rather than launching an exhaustive combination sweep:

- Sonic FIVE/R25 uses R3 as primary reference; retain FOUR/R18 for late-channel
  performance. Do not substitute FOUR for the matched baseline.
- Xenoblade retains FIVE and FIVE+T3; separate opening, battle cinematic and gameplay.
- Fire Emblem retains THREE+T1 and candidate FOUR+T3; separate map and battle.
- Dark Moon keeps its four scene boundaries; no claim that its right-eye veto is fixed.
- A 4x candidate compares to its 4x baseline. Only the otherwise-identical 4x
  baseline bridges to 2x. Nonbaseline candidates also show deltas to the explicitly
  selected optimum where one exists; no redundant reciprocal baseline row.

Source tests and review qualify implementation contracts, not Thor speed or visual
output. Submission uses existing shader, Android, signing and publication gates.
The coordinator stops after submission; installation and hardware tests await the
owner's next session.

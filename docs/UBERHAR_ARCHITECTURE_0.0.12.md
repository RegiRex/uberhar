# Architecture review: 0.0.12 Thor milestone and the 0.0.13 step

<!-- AstraEH: Full review after three alphas, using device evidence and production source. -->

Reviewed 2026-09-25. Device baseline: released `42cb2e5aefa2ccd081fce2395a59d3378328615d`;
analysis checkpoint `f30171ae40d7e3d4622c15df3618c5e62823dae9`. The new 0.0.13
implementation accompanies this review; it has no Thor performance results yet.
This review advances the ledger from the 0.0.9 review, after three published alphas.

## Evidence and remaining uncertainty

The [0.0.12 analysis](UBERHAR_LOG_ANALYSIS_0.0.12.md) covers two Native/Vulkan/2x
Awakening sessions on the Thor. Warm emulation speed is 99.760%; every complete
five-second window exceeds 98.45%. Generic module reuse is 37 hits, zero misses;
generic foreground waiting totals 89.293 ms. This is the successful reference.

Cold waiting is still 8.121 s across 114 generic pipelines and 67 fragment families.
66 modules require frontend compilation. Generic driver calls consume 6.920 s;
one 768.609 ms frame includes six driver builds totaling 601.281 ms. The longest
frame is 806.541 ms; ten intervals exceed 500 ms. These overlapping wall times must
not be added. Cold and warm coverage differ, including an extra ally attack, so
aggregate percentages are not controlled replay benchmarks.

CPU shader translation totals only 6.355/5.441 ms cold/warm. Existing CPU JIT,
indexed-vertex reuse and persistent generic modules successfully removed the
previous sustained slowdown in the tested workload. GPU execution time is not
measured. Compute eligibility remains zero; all three profiles still route this
game through native rendering. Temperature/status readings do not rule out throttling.

The logs prove a fragment-family/driver-compilation problem. They do **not** record
which lighting register fields created all 67 families. Lighting is a source-based
optimization candidate, not a demonstrated explanation for every observed miss.

## Source revalidation

- `PipelineCache::BindPipeline` must wait for a demanded generic pipeline if it is
  not ready. The serial generic worker preserves dependency order and limits
  contention; neither the cache nor the worker guarantees a ready first encounter.
- `MakeDynamicTevFamilyConfig` already moves TEV instructions, alpha/scissor/depth,
  fog and several sampling choices to runtime. In 0.0.12 every lighting LUT input,
  sign convention, scale and physical light selector still enters the family key.
- `WriteLighting` unrolls the enabled light sequence and specializes bump mapping,
  attenuation/geometry operations and LUT enable/configuration support. Those
  structural choices limit runtime cost but produce distinct shader bodies.
- `GetTevFallback` remains bounded to 128 families and 1,024 pipelines. Exceeding a
  limit or rejecting an unsupported state uses accurate specialization. No missing
  shader may cause an omitted draw. AddSigned, custom normal and existing unsupported
  shadow/gas cases retain recovery.
- The Adreno log reports no usable graphics-pipeline-library or shader-object
  feature. Extended dynamic state remains disabled by the inherited driver workaround.
  The review does not justify removing those constraints.

## Alternatives and decisions

| Approach | Decision and reason |
| --- | --- |
| Runtime lighting LUT controls and source selection | Implement 0.0.13. Remove data-only permutations while retaining the unrolled structural lighting path. Measure the family reduction on the same run. |
| Completely universal fragment interpreter | Continue incrementally. Runtime light count, bump, procedural and resource variants could merge more families, but instruction size, GPU branches and typed resources require separate correctness/performance evidence. |
| Seen-state pipeline prewarming | Useful later for remaining warm initialization. It cannot anticipate unseen first-playthrough state, and the warm baseline already waits less than 90 ms in this replay. |
| More compiler threads | Not the primary fix: a demanded pipeline still blocks, and competing driver builds can contend. Preserve the isolated serial generic worker. |
| Graphics pipeline libraries/shader objects | Capability-gated future option, unavailable in the observed Thor driver. Never assume fast linking from extension names alone. |
| GPU vertex interpretation | Keep on the roadmap, but CPU JIT compilation is now tiny. Avoid replacing a successful path before fragment first-use costs are reduced. |
| General compute rasterization | Continue only after coverage/correctness foundations. Depth, blend, clipping, texture derivatives and ordering reject almost every current draw; removing guards would be incorrect. |
| Scan a ROM to compile everything | Insufficient: dynamic registers, runtime programs and render state are not a finite shader list that can reliably be read from the game file. A finite emulator-owned program bank is the relevant longer-term design. |
| Skip late draws | Rejected; missing geometry, effects and guest-visible behavior are not an acceptable smoothness optimization. |

## 0.0.13 implementation and validation contract

The private fragment push-constant ABI grows from 108 to **120 bytes**. Seven LUT
control bytes encode input selection, signed/unsigned mode and exact hardware
scale. Another word carries eight physical light selectors plus the original
slot-indexed two-sided flags. The implementation preserves the inherited distinction
between diffuse slot indexing and LUT two-sided indexing; it does not silently fix
that separate hardware-accuracy question. All values are copied with each scheduled
draw before family canonicalization, retaining duplicate/remapped light order.

Light count, directional/positional operations, geometry factors, bump/shadow paths,
LUT enable and hardware configuration support stay specialized. Typed textures,
fixed-function pipeline state, CPU vertex processing and driver workarounds stay
intact. Unknown LUT input/scale encodings use specialized recovery. Runtime scale
values are exact powers of two (or the inherited zero for reserved encodings).

120 bytes fits the [Vulkan guaranteed 128-byte push-constant minimum](https://docs.vulkan.org/samples/latest/samples/performance/constant_data/README.html).
This adds no extension, permission, Android service or external driver dependency.
The existing source/build-fingerprinted module cache naturally separates this ABI.

New counters compare `previous_lighting_families` (the 0.0.12 key applied to these
same observations) with `canonical_families`, plus lighting/procedural structural
counts. They are capped at 2,048 per dimension and use the existing reporting
cadence. This is a key-space comparison, not hypothetical saved milliseconds.

Required host coverage includes family/source equality, non-mutation/idempotence,
packing/unsupported-input recovery, and exact color/depth comparisons against the
specialized generator. The extended corpus covers all lighting configurations,
LUT input types/scales/signs, remapped/duplicate lights, attenuation tables, bump,
shadow and one through eight lights. Vulkan frontend/SPIR-V validation with both
optimizer modes and existing Android/package/signing gates remain publication gates.
Local results: 7,400 family/source checks, 425,984 exact full-fragment pixel and
depth comparisons, 57,344 TEV color/texture-use comparisons, and 591 complete Vulkan
programs validated in both optimizer modes all pass. Vulkan integration syntax and
the existing host/Android-configuration probes also pass. Android APK compilation,
packaging/signature gates and Thor performance testing remain outstanding.
Host Mesa correctness does not prove Adreno timing or complete PICA hardware accuracy.

## Acceptance and next implementation order

1. Test 0.0.13 Native at 2x, one cleared-cache run and one warm run through the same
   section/battle. Compare visuals, speed windows, worst frame intervals, actual
   wait totals and the same-run old/new family counts. Pause if called away.
2. Preserve the near-full-speed warm battle behavior. The immediate cold target is
   fewer first-use families and fewer/shorter long frame intervals; a useful stretch
   target is at least 25% less generic wait than 8.121 s in comparable coverage.
   This is a target, not a predicted or measured result.
3. If dynamic LUT branching harms warm speed or makes individual builds excessively
   long, narrow the runtime dimensions before adding more. If key reduction is low,
   use structural counts and targeted next diagnostics to identify the actual split.
4. Next, consider merging additional proven lighting structure or building a bounded
   ready program/pipeline bank. Pipeline state still needs distinct native objects;
   this alpha does not achieve universal compilation-free rendering.
5. Revisit compute/GPU vertices when evidence justifies their cost. Screen-pair
   presentation/synchronization and model clarity remain later milestones.

Next default full review: after **0.0.16**, allowed after 0.0.15–0.0.17, before
starting 0.0.18. Review earlier for visual regression or a failed architectural premise.

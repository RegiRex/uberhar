# Uberhar 0.1.46 architecture and purpose review

<!-- CodexAstraLocal: Bound the audit to changed consumers and renderer ownership; this is not a claim of whole-codebase or device qualification. -->
The .46 review targets serial preparation in existing rendering paths and the
experimental failures observed in .45. The owner's Sonic observation overrides
any general recommendation to force CPU vertices. Existing parallel work remains
useful, but neither thread count nor total CPU occupancy is a performance gate.

<!-- CodexAstraLocal: Explain the dependency that justifies the main preparation change. -->
Native/Combo's original `FSConfig` is decoded once before generic-family
canonicalization. `DynamicTevPreparation` retains only the original configuration,
the entire device profile and 128 prepared bytes; exact equality proves a hit.
It owns no Vulkan handles, guest memory or readiness result. A draw-local pending
snapshot is cleared on every preparation/retry and consumed before any generic
census, partial-plan lookup or queued possible-fallback use. Ready full fragments
bypass transport expansion. Queued commands retain value-owned snapshots and
the existing upload invalidation. Title teardown clears pure reuse state.

<!-- CodexAstraLocal: Reuse established concurrency rather than duplicating ownership systems. -->
CPU Software's immutable per-triangle combiner plan lives through the existing
full worker join. Demanded inputs use the original scalar arithmetic; unused
inputs do not request texture/lighting work. No inter-triangle reordering,
cross-draw result reuse, extra pool or weakened feedback/alias admission is added.
The separate invalid semantic-coordinate producer remains unresolved.

<!-- CodexAstraLocal: Keep the Calculated hypothesis and terminal ownership obligations explicit. -->
Calculated's clipping-failure reconstruction executes once after nested clipping
returns. Required successful geometry and diagnostic words retain their contract.
The four compute roles share two existing owners; creation errors are normalized
before generated guest frames. Optional first-incident artifacts are bounded,
local and do not replace the original failure. This is a portability/correctness
candidate, not a demonstrated driver fix or successful gameplay claim.

<!-- CodexAstraLocal: Document source cleanliness and actual result consumers. -->
Each new logical section has a purpose comment. Temporary runtime evidence has a
`CodexAstraLocal TEST-ONLY LOG` marker and a removal condition. Review corrects
stale Native fragment-lane wording, duplicated fragment-register decoding and
work whose result the selected draw cannot consume. Dynamic transport and generic
family preparation are distinct caches with distinct inputs/consumers. Existing
texture workers are exercised but too small in the measured Dark Moon span to
justify further expansion. No speculative worker-count increase is included.

<!-- CodexAstraLocal: Final integration records completed checks separately from pending publication/hardware tests. -->
Native/Combo's focused route suites pass: 6,698 full-route and 5,175 static-route
checks, both with zero optional waits in the modeled execution. They include
current transport after full specialization, partial selection from stale bytes,
a GPU-to-CPU retry and a generic alternative selected later on the command worker.
Six affected Software cases pass. One fixture incorrectly expected a zero-address
constant sampler to fail; a real nonzero address with invalid dimensions corrected
the setup without changing either assertion or production behavior. Only that
case was repeated after its incremental fixture build.

Calculated's actual production owners pass five complete color/depth/vertex
comparisons against retained graphics outputs, a packed RGB565/D16 comparison,
all 57 clipping-failure words, and three injected module/pipeline creation errors.
An intentionally omitted failure epilogue is detected. A private fixture's const
qualification was corrected after its first compile; production remained frozen.
These checks use host lavapipe, not the Thor's Qualcomm driver. The inherited
midpoint-color disagreement remains explicitly unresolved and was not rerun.

The consistent native build and nine manifest checks pass. Existing mandatory
publication gates are retained and remain a separate pending result at submission.
This scoped source/purpose review does not reset the full historical audit cadence
or claim every inherited line was audited. No Thor operation occurs in this cycle.

# 0.1.58 scoped architecture and implementation review

<!-- CodexAstraLocal: Record observed source/host findings separately from hosted publication and hardware qualification. -->

The architect reviewed independently from the engineers responsible for conversion,
transport, shader generation and diagnostic delivery. Root integrated settings,
mono input, exact memo reuse and release controls, then compiled the affected native
units with the Android NDK and the actual Android Kotlin/resources target. This is
a review of the changed architecture and its consumers, not every inherited line.

**Reconciliation:** existing 64-entry vertex FIFO, prepared input recipes, worker
pools, whole-draw packets, deferred uploads and state-generation caches were
identified before implementation. The [decision ledger](UBERHAR_DECISIONS_0.1.58.md)
records which were extended, retained or deferred, rather than adding competing
mechanisms with overlapping ownership.

**Issues caught and corrected before submission:**

- A full specialized fragment PSO lifetime token was unnecessarily excluding valid
  ordinary CPU-layout vertices from indexed/worker transport. The same trivial
  vertex shader/88-byte layout remains eligible while the token's normal lifetime
  stamping remains intact.
- Expanded render-pass areas could hide real requested-area transitions or create
  false ones. Separate last-requested identity preserves the original submission
  opportunities independently from the expanded active area.
- An initial adaptive expiry would end a settled trial before it could collect its
  required samples. Expiry now permits that trial to complete before static fallback.
- If clipping removes every triangle, the framebuffer helper now cancels write
  invalidation before returning, preserving existing alias contents.
- New settings were added to the generated key registry, all native/Kotlin layers,
  launch retention, quick Testing and diagnostic context. Explicit title false/zero
  values continue to override global choices.
- The new telemetry check was initially duplicated in the probe script. It executes
  once, retaining JSON-output validation. Changed extraction fixtures were updated
  without dropping their order/late-reservation failure controls.

**Validation scope:**

| Contract | Local evidence |
|---|---|
| CPU conversion | Exact output/input/cache fixtures, exceptional bits, original interpreter/JIT/FIFO/assembly integration and deliberate invalid variants |
| Independent result memo | Ordered byte equivalence across chunk shapes, original FIFO behavior, actual invocation accounting |
| Packed output / upload | Exact indexed reconstruction, bounded lease ownership/credits, delayed publication, ring wrap and pass flush cases |
| Deferred scratch packing | Actual queue/JIT output versus original scalar packing, primitive/quaternion order and FP state |
| Conservative clipping | Exact strict clip-plane classifier, exceptional fallback and stable retained record bytes |
| Pass reuse | Actual production transitions with original hazard/submission checks and deliberate defects |
| Specialized SPIR-V | 384 synthetic emitted TEV comparisons, 24 paired modules validated for Vulkan1.1, frozen queued-policy/cache identity |
| Settings and mono input | Four-layer inheritance, explicit false/zero, launch ownership, live resolution, preserved saved slider/stereo |
| Telemetry and reports | Real one-second partition/tails, required log delivery, incremental retention, production Kotlin engine/session comparisons |
| Integration | Affected Android NDK native translation units; actual Android Kotlin/resources compilation |
| Retirement | Isolated feature patch applies to its recorded origin and reverse-applies to restore it |

These checks verify semantic and ownership contracts. They do not measure Thor
performance, GPU pixels under every driver or full-game compatibility. Existing
hosted shader/image, Android, signing and publication gates remain required.

**Cost and cleanliness review:** all new controls have actual consumers. New
logical sections retain purpose comments; temporary diagnostics identify removal
conditions. Bounded allocation/queue/capacity paths expose refusal and delivery
counts. The V38 override is removed from runtime and archived by feature. Conversion,
memo, deduplication, clipping and worker upload save different work and are not
reported as interchangeable. CPU-layout fragment specialization remains GPU work.

**Remaining architectural limits:** general offscreen right-eye elimination lacks
complete producer/consumer/materialization proof; Dark Moon's veto remains. Typed
append's prior section-dependent regressions are not explained by a source-proven
fix in this build. Adaptive coverage may still depend on useful recurring work.
Larger pipeline capacity can increase compilation/memory cost. New diagnostics
must be kept identical across comparisons. These are explicit hardware decisions,
not reasons to invent performance results or silently promote an optimum.

Submission is the stopping boundary. The owner evaluates the released alpha using
matched baselines and selected additions; no device tests were performed during
this implementation session.

## First-attempt build repair

<!-- CodexAstraLocal: Retain the actual hosted failure and strengthen fixture contracts instead of weakening production guards or gates. -->

Run 42575667130 did not publish an APK. GPU-pass attribution's extracted fields
referenced `PassUnionPolicy` without its real declaration in the test fixture.
The fixture now includes and fingerprints the production helper. Compiler errors
are also printed in the job log, while retaining the complete artifact.

The CPU-queue negative control removed the explicit arithmetic floor, but the new
standalone seed helper independently rejected the same light workload. A real
pending heavy seed now exercises the alternative grouped-admission path, so the
test still requires the explicit floor to reject 34 and accept 35. Both cost-seed
settings and the original standalone boundaries remain covered. The expected
negative-control result remains a runtime assertion failure, never compilation
failure or unconditional success.

Review also identified stale compute-census and parallel-cache fixtures before
they became later hosted failures. Their adapters now carry the real adjacent
default-OFF clip policy and the launch-owned vertex-memo setting. Original census,
ordering and mutation populations remain; memo launch immutability has explicit
coverage. These repairs change tests and handoff instructions only, with no change
to emulator rendering or production admission behavior.

The repaired actual ARM64 queue gate passed 13,963 checks, 1,050 comparisons,
16 floating-point modes and all 26 original negative controls. GPU attribution
passed its candidate and all three original negative controls; compute census
passed 97,147 checks and all 11 original negative controls; parallel cache passed
its baseline and all seven original negative controls. Independent review found
no further actionable extraction-adapter gaps in the bounded inspected scope.
The replacement hosted shader, Android, signing and publication gates are still
required; these local results do not establish APK availability.

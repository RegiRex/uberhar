# 0.1.51 scoped integration review

<!-- CodexAstraLocal: Record independent review scope and hardware limits without implying a fresh line-by-line audit of all inherited code. -->
This review follows the .39–.50 hardware assessment and the owner's retirement of
Calculated. Architecture review is separate from implementation and the removal/
consumer/comment audit. The release changes are described in
[0.1.51 notes](releases/0.1.51.md).

The architecture review checks the following contracts:

- Structural fragment state is keyed by the complete configuration and profile.
  Numeric refresh begins from a clean base, so stale certificates cannot survive.
- Static certificate pruning uses the existing evaluator's maximum knownness.
  Unsupported or input-dependent channels retain the original arithmetic.
- GPU attribution records the actual worker-selected pipeline after late fallback.
  Existing samples retain mixed/missing coverage and timestamp ownership. No
  additional query, draw split or wait is introduced for identification.
- Small standalone CPU batches cannot bypass the actual-miss admission floor.
  Already-pending groups retain publication checks and ordered output ownership.
- Calculated's exclusive code and dependencies are removed. Shared graphics,
  rectangle transfers, CPU census and direct shadow/cube bindings remain.
  Saved mode2 migrates to Native without renumbering the supported modes.
- Diagnostic family counters acknowledge only a successfully emitted snapshot.
  Concurrent producer drops survive acknowledgement. Optional progress never waits
  for queue capacity; final summaries and production errors remain reliable.
- Software stepping and invariant reuse preserve the original output contract,
  unsupported-operation paths and nonfinite-input rejection. Provenance does not
  pretend to identify an original memory writer from a matching value alone.

<!-- CodexAstraLocal: Retain observed gate repairs and meaningful negative controls rather than erasing failures or weakening their oracles. -->
Focused checks passed for conservative CPU admission and twelve injected defects,
Software work/provenance, supported-mode migration, Android setting coverage,
descriptor and stream ordering, retained draw use, actual shader selection and
runtime shader output. Logging checks exercise real queue contention, concurrent
family drops and production reporter/sink behavior. Removal exposed stale fixture
dependencies and a removed default-key block; these were repaired before publication.
The CPU miss-count negative control was moved to the new actual admission decision.

<!-- CodexAstraLocal TEST-ONLY LOG: Qualification counters and identity details may be removed after placement/regression decisions; keep lifecycle/error records. -->
Every optimization keeps bounded attributable evidence and a stated scope.
Shader identity describes generated source/profile, not a measured compiled-driver
cost. Scissor area is geometry, not fragments executed. GPU samples can contain
multiple shader states; omitted progress has explicit per-family accounting.
CPU submission, dependency waiting and GPU execution times are not interchangeable.

Integrated native compilation, Android qualification and published artifact
verification remain separate required gates. Host passes do not establish Thor
speed or gameplay correctness. Matched device comparisons, including the requested
existing-build lean Combo test, determine whether performance is recovered.
Software's Dark Moon numerical failure is still unresolved.

# Uberhar 0.1.50 integration review

<!-- CodexAstraLocal: Keep architecture, cleanliness, historical device evidence and release qualification separate. -->
The candidate starts from released source `2e657af0e53c6cc7cae8155d15898642cbf406b8`.
It responds to the owner's broad 0.1.39–0.1.49 assessment, including failed and
partial experimental-mode runs and distinct settings/power conditions. That
assessment found a real regression in Dark Moon with Combo CPU vertices enabled,
excess coordinator-only small work, adaptive histories without completed trials,
and unresolved GPU/observer costs. It did not prove that charging separation,
every busy GPU interval or every earlier optimization caused the regression.

## Architectural decisions

<!-- CodexAstraLocal: Cover all fifteen inherited feature items and the integrated models without claiming every item needed a rewrite. -->
| Scope | Decision and reviewed contract |
|---|---|
| CPU seeds, lazy startup and grouping (13/5) | Conservative standalone seeds; safe existing pending joins; no artificial fill wait. Preserve pinned ownership, dependency drains and ordered retirement. |
| Adaptive participation (8) | Sampling cadence survives eviction. Bounded recurrence/probation and protected learning windows; expire nonproductive histories and use sparse discovery. Decisions still require comparable complete-work samples and useful helper/join cost. |
| Rejection, mapping and positive effects (2/4/10) | Reuse exact immutable decisions. Accepted proof slot/generation replaces redundant lease copying; stale or mismatched proofs cannot authorize work. No cached mutable vertex bytes. |
| Statistics and census (3/9) | Owner-local counters; optional execution census off by default. Real shared capacity permits remain enforced. First acquisition fixes census policy for the app lifetime. |
| Liveness and alpha rejection (1/6) | Preserve required draw operations while removing unused fragment consumers. Attribution follows the final selected queued shader, not an earlier intended route. |
| Texture samples (7) | One lazy invocation-local ordinary memo; direct reads when no reuse is possible. Preserve first use, sampler identity, typed shadow and procedural handling. |
| Constant arithmetic (11) | Real generic/static endpoint substitution from exact live colors/buffer state, immutable transport and original arithmetic rules. Numeric values do not create unbounded module keys. |
| Directional arithmetic (12) | Exact positive-Z certification and omission of unused distance work. Arbitrary direction normalization and varying lighting remain unchanged. |
| Image and RAM transfers (14/15) | Lazy epoch writes, cheaper reports and full refusal/cost scope. Retain allocation/epoch/current-RAM/nonalias/writer/join guards; no forced GPU readback or speculative lower copy floor. |
| Lighting specialization | Reserve a bounded part of existing optional capacity; never expand the total bound or block ready/recovery lookups. Reservation uses a structural cost proxy, not measured GPU savings. |
| Barriers and readiness | Preserve old-pass attachment metadata and required visibility. Attribute known next consumers and distinguish submission readiness from actual submitted fence waits. |
| CPU Software | Exact preparation cache, checked spans, useful row tasks, bounded owned constant groups and actual-read input provenance. Drain groups before invalidation, normal return and failure. The demanded-input NaN remains unresolved. |
| Calculated | Invocation-local state, inside/clip/active-region changes and one structural shader retry. Exact feature/cache identity and bounded failure archives. No Native fallback; primary-color specialization remains unavailable. |

<!-- CodexAstraLocal: Controls make experiments interpretable but do not make all interacting policies statistically independent. -->
Thirteen Native/Combo controls are separately persisted and frozen for their
owners. GPU timestamps remain independent and off by default; Calculated's model
diagnostics cannot enable queries. Shader code-generation controls enter exact
profile identity. Liveness OFF can change lighting execution and optional
reservation eligibility; its comparison is not an isolated arithmetic-cost
measurement. Software/Calculated masks are global restart-only INI controls.
Qt per-game configuration must not reset or persist those global masks.

## Separate cleanliness and diagnostic review

<!-- CodexAstraLocal: Record actual consumer/overhead coverage rather than claiming an audit of every inherited line. -->
Review follows each changed value from preparation to its consumer: proof tokens,
mapping plans, adaptive observations, immutable shader transport, optional module
admission, resource epochs, current-memory copies and model work outputs. No
unused second ordinary-sample cache is retained. GPU query polling and host clocks
are omitted when their controls disable them. Reports use bounded periodic work
and final totals; optional failure evidence remains associated with its owner.

The review corrected admission timing that initially missed five early shape
refusals, decoder clocks that ignored diagnostic OFF, and a fence counter that
included a canceled pre-submission wait. It also corrected Qt's global Calculated
setting ownership. New logical sections have purpose comments. Temporary
diagnostics use `CodexAstraLocal TEST-ONLY LOG` with qualification/removal context;
mandatory failure handling is not a removable performance experiment.

This is a focused review of changed CPU/GPU/resource/model paths and a broad
assessment of retained multi-build results. It is not a fresh line-by-line audit
of the entire inherited emulator or proof that every historical hypothesis has
been resolved. The old full-review ledger remains preserved.

## Validation and remaining acceptance

<!-- CodexAstraLocal: Distinguish the exact checks already performed from CI and hardware outcomes. -->
The CPU queue campaign passed 4,270 assertions and all 13 deliberate defect
controls. Budget and mapping checks passed; texture/copy checks passed 119 cases.
Software's exact preparation/coverage/read-time helper passed 769,653 checks.
Previously frozen Software kernel/alias/output checks remain retained; no fresh
integrated execution is inferred from old linked archives. Twenty affected native
owners compiled against current headers, and the final query/count/control
fixture passed. Separate stream-order controls detect five intended defects.
The shader campaign passed 816 retained TEV cases, 144 runtime arithmetic cases,
128 validated Vulkan modules and five deliberately broken shader controls.
Route, cache and actual-worker checks passed without repeating that shader campaign.

Calculated retained the completed guest/assembly/FP/retry/cache/tile comparisons;
final affected syntax and four GPU-control combinations passed. The midpoint
discriminator found pre-TEV interpolation disagreement, so no bias, tolerance
relaxation or unqualified primary-color optimization is enabled. GPU byte/count
fields describe logical bounds, not actual memory traffic. Host shader output
checks and the required release gates qualify their stated scope; they cannot
establish Thor driver acceptance or gameplay correctness.

The owner performs the next matched hardware runs. Compare complete scene speed,
slowest windows and pacing, useful CPU execution, actual shader selections and
sampled costs under recorded resolution/power/thermal conditions. Higher CPU
occupancy alone is not success. The product target remains correct cold rendering
without draw skipping at 99%+ normal speed, first at 2x and then at 4x. Model boot
and useful complete output remain separate acceptance requirements.

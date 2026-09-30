# Architecture review ledger

<!-- AstraEH: Build-count workflow requested by the owner, not a timed reminder. -->

Review every **three to five alpha builds**, normally every four. Count published
alpha versions since the version covered by the last review; failed build retries
of the same version do not advance the count. The next review can happen after
the fifth build's evidence arrives, but must finish before beginning a sixth.
Review early for correctness regressions or a changed architectural assumption.

| Review date | Latest version examined | Default next review | Allowed window |
| --- | --- | --- | --- |
| 2026-09-24 | 0.0.6 | After 0.0.10 | After 0.0.9 through 0.0.11; before 0.0.12 work |
| 2026-09-24 | 0.0.9 | After 0.0.13 | After 0.0.12 through 0.0.14; before 0.0.15 work |
| 2026-09-25 | 0.0.12 | After 0.0.16 | After 0.0.15 through 0.0.17; before 0.0.18 work |
| 2026-09-27 | 0.0.15 | After 0.1.3 | After 0.1.2 through 0.1.4; before 0.1.5 work |
| 2026-09-28 | 0.1.3 | After 0.1.7 | After 0.1.6 through 0.1.8; before 0.1.9 work |
| 2026-09-29 | 0.1.6 | After four successor builds | After three through five; before beginning six |

The [0.0.6 review](UBERHAR_ARCHITECTURE_2026-09-24.md) established the earlier
roadmap. 0.0.7 implemented family consolidation; 0.0.8 added broader runtime state,
ready CPU vertex routing and host-pipeline reuse; 0.0.9 corrected bridge coverage.

The [0.0.9 review](UBERHAR_ARCHITECTURE_0.0.9.md) rechecks all four 0.0.9 sessions
and starts explicit primary-generic/native and compute-subset experiments in
0.0.10. The owner's request to compare virtual-PICA designs and essentially
unchanged cold waiting justify reviewing after three builds. This ledger does
not claim unattended development or timed reviews.

For each review, record exact revisions, device evidence, unanswered questions,
accepted/rejected alternatives, validation gates, implementation order and the
new review window. Keep previous entries for comparison.

<!-- AstraEH: The third post-review alpha supplied a successful device baseline. -->
The [0.0.12 review](UBERHAR_ARCHITECTURE_0.0.12.md) rechecks source and the Thor
cold/warm milestone, compares architectural alternatives, and selects runtime
lighting controls for 0.0.13. The subsequent [0.0.13 device analysis](UBERHAR_LOG_ANALYSIS_0.0.13.md)
confirms reduced family counts and retained warm speed; that results analysis is
not another full architectural review and does not reset this ledger.


<!-- AstraEH: Results analysis and beta planning do not restart the review interval. -->
The [0.0.14 analysis](UBERHAR_LOG_ANALYSIS_0.0.14.md) finds smaller family counts but
higher cold driver cost, motivating compact runtime lighting in 0.0.15. This is a
results analysis, not another full review. The default review remains after 0.0.16;
a proposed 0.1.0 promotion should include the final-alpha architectural assessment
within the existing three-to-five-alpha window.


<!-- AstraEH: Beta promotion follows the third post-review alpha and does not hide a build. -->
The [beta-promotion review](UBERHAR_ARCHITECTURE_0.1.0.md) audits 0.0.15 source and
all four Thor sessions before promoting Native to beta. It selects final-vertex
reuse, prepared register transport and sparse CPU-stage samples while preserving
the compact fragment path. Count 0.1.0 as successor build one, 0.1.1 as two,
0.1.2 as three, 0.1.3 as four and 0.1.4 as five. Thus the default next review is
after 0.1.3, allowed after 0.1.2–0.1.4, before starting 0.1.5. The earlier
0.0.16 default above is historical and superseded by this completed review.


<!-- AstraEH: Broader beta evidence and a diagnostics follow-up do not reset the cadence. -->
The [0.1.0 device analysis](UBERHAR_LOG_ANALYSIS_0.1.0.md) adds Ocarina and Sonic,
confirms reduced vertex work per input, and identifies draw-heavy steady-state
costs plus sample exhaustion. 0.1.1 is a focused diagnostics iteration. This is
not another full architecture review: the next default remains after 0.1.3,
allowed after 0.1.2–0.1.4, before 0.1.5. The owner's requested automatic
compatibility profiles are recorded as post-1.0 scope.

<!-- AstraEH: Complete the scheduled four-successor review; this alone resets the interval. -->
The [0.1.3 full review](UBERHAR_ARCHITECTURE_0.1.3.md) rechecks the five latest
runs, prior beta evidence, current source, alternatives and acceptance gates.
It retains Native and selects settings/cache context for 0.1.4, then measured
per-draw bookkeeping reduction. Count 0.1.4 as one, 0.1.5 as two, 0.1.6 as three,
0.1.7 as four and 0.1.8 as five. Next default after 0.1.7 evidence, allowed after
0.1.6–0.1.8 and required before 0.1.9. Earlier windows remain historical.

<!-- AstraEH: The requested early full review is within the three-to-five build window. -->
The [0.1.6 full review](UBERHAR_ARCHITECTURE_0.1.6.md) examines twelve deduplicated
Thor sessions, the exact released source, zero compute coverage and MH4U's
specialized recovery. It selects fused vertex input plus support/recovery
instrumentation for 0.1.7 and revises the owner roadmap without dropping its
scope ledger or prior history. This is a completed full review, not only a log
analysis. Count 0.1.7 as successor one; default next review after four successors,
allowed after three to five, before starting six. If the beta number stays 0.1,
that is after 0.1.10, allowed after 0.1.9-0.1.11, before 0.1.12. A minor promotion
retains the successor count rather than resetting it.

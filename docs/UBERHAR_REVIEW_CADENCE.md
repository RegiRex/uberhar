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

The [0.0.6 review](UBERHAR_ARCHITECTURE_2026-09-24.md) established the earlier
roadmap. 0.0.7 implemented family consolidation; 0.0.8 added broader runtime state,
ready CPU vertex routing and host-pipeline reuse; 0.0.9 corrected bridge coverage.

The [current review](UBERHAR_ARCHITECTURE_0.0.9.md) rechecks all four 0.0.9 sessions
and starts explicit primary-generic/native and compute-subset experiments in
0.0.10. The owner's request to compare virtual-PICA designs and essentially
unchanged cold waiting justify reviewing after three builds. This ledger does
not claim unattended development or timed reviews.

For each review, record exact revisions, device evidence, unanswered questions,
accepted/rejected alternatives, validation gates, implementation order and the
new review window. Keep previous entries for comparison.

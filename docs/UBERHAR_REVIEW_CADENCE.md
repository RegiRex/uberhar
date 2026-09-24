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

The [current review](UBERHAR_ARCHITECTURE_2026-09-24.md) completes the three steps:
source/evidence audit, design validation, and an ordered implementation roadmap.
0.0.7 implemented family consolidation; 0.0.8 implements broader runtime state,
ready CPU vertex routing and host-pipeline reuse. This ledger is a development instruction; it
does not claim an unattended review will run while development is idle.

For each review, record the exact source revision, available device captures,
unanswered questions, accepted/rejected alternatives, validation gates, and the
new covered version and next review window. Keep old entries for comparison.

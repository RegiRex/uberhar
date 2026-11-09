# Compact frame telemetry, schema 1

CodexAstraLocal: diagnostic evidence only. This feature makes no performance claim
and does not change preferences, frame limiting, guest progress or renderer policy.

`PerfStats` observes consecutive **system frame-end** endpoints. These are not
Android display presentations or distinct guest-rendered frames. Submission counts
mean top-screen guest framebuffer swaps. Guest progress is measured with canonical
268111856 Hz ticks; speed is guest elapsed time divided by actual host elapsed time.

Each existing five-second frame window has one compact `window` record containing
`one_second_sections`. Sections close at the first endpoint at/after one second;
actual `wall_ns`, `overshoot_ns`, `missed_deadlines`, `partial` and boundary flags
are explicit. The final tail of a five-second window may be shorter than a second.
No samples are fabricated across a stall or pause. The legacy five-second arithmetic
is unchanged. Use summed guest/summed host time for Overall, never an unweighted
mean of section percentages. Only comparable full sections support one-second
Averages Low/High; partial/mixed sections remain evidence, not hidden samples.

Every section and session summary retains its six shortest and six longest valid
intervals, including intervals below 50 ms. Equal durations retain earlier ordinals
first; repeated ties are separate observations. Fewer observations yield fewer
entries. Shortest/longest scalar fields are zero only when their lists are empty.
An interval tuple is:

`[ordinal, begin_ns, end_ns, begin_guest_ticks, end_guest_ticks, begin_submissions,
end_submissions, work_ns, context_generation, scene_id, flags]`

`ns` endpoints are monotonic relative to `run_ns`. Context records carry adjacent
`anchor_monotonic_ns` and `wall_clock_ns` readings for external localization;
wall-clock jumps must not change interval durations. A context tuple is:

`[generation, scene_id, mode, configured_resolution, effective_resolution,
six_candidate_feature_mask, effective_limit, temporary_limit]`

Scene zero means unknown. Explicit tuner/owner markers can identify a scene; I/O
phases and game menus are not automatically equivalent to visual content. Generation
changes follow actual ApplySettings notifications. Context text is a snapshot of
resolved values, not proof every launch-owned backend adopted an edit. Cross-context
intervals retain both contexts and are marked mixed. Flags: duration1, five-second
end2, pause4, final8, context boundary16, discontinuity32, scene boundary64.

Histogram edges are 16666667,33333334,50000000,100000000,250000000,500000000,
1000000000 ns, with equality in the higher bin. Maximum 16 sections per parent
window bounds memory. Pathological context churn reports section/interval/wall/tick
overflow; totals and session extremes still cover valid observations. The records
do not promise top-six lists for arbitrary retrospective subscenes.

Fastest system interval is not necessarily highest guest throughput. Paired ticks
permit that interval's measured guest-progress ratio; the nominal
`1671.368087504493 / interval_ms` value is cadence-equivalent only. Neither is
display FPS. One-second Averages High remains the clearest sustained peak measure.

The existing log worker writes required compact data to sibling files named
`<ordinary-log>.frames-<process-monotonic-ns>-<chunk>.jsonl`. Chunks rotate after
approximately16 MiB, on record boundaries, without overwriting prior chunks or a
duration cutoff. These files bypass ordinary filters, optional Diagnostic admission,
instant-debug synchronous IO and the ordinary100 MiB cap. **Capture/export must
include these files explicitly**; copying only the ordinary native log is insufficient.
Archive completed chunks incrementally and the final chunk on normal close.

The producer never waits for queue capacity or sink IO. Queue capacity16 is fixed;
sequence gaps, producer drops, section overflow and sink failures are explicit.
Indefinite sink failure cannot be lossless with bounded nonblocking memory. The
`delivery` record reports cumulative attempts/queued/written-and-flushed/failures
and final requested versus written sequence. An enqueued final record is not a
durability acknowledgment. The worker drains compact records independently of the
ordinary shutdown quota. Crash tails and missing final acknowledgments remain
incomplete. No new thread or per-frame text/heap/GPU query is introduced.

Advisory reports use `UberharTuningReport`: primary deltas against a matching
baseline, secondary against an explicitly selected measured profile, and no
reciprocal baseline-versus-optimal row. Percentage points and relative percent are
separate. A resolution bridge requires the exact baseline tuple except resolution.
The Android journal exports its actual A/B/A baseline references and accepted
recommendation; it never invents cross-resolution references from archived plans.
Its120-system-interval bins are labelled separately from these one-second records.

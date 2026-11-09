# Bounded tuning decisions

<!-- CodexAstraLocal: Integration contract for the Android-free .58 tuning evaluator. -->

`UberharTuningEngine` is in
`org.citra.citra_emu.features.settings.model`. It evaluates measurements; it does
not launch games, apply preferences or establish that two captures show the same
scene. The Android controller displays the requested measurement and recommendation;
the owner changes settings manually. This feature does not apply or undo settings.

Construct one engine with an immutable context identifier, a confirmed scene
identifier, the original comparison baseline feature set, and an ordered list of
at most 12 unique candidate tuples. Each tuple is the complete enabled-feature
set, including any preserved baseline features. Explicit pairs remain eligible
even if a standalone member is rejected. No automatic factorial search occurs.

The context must identify the title, build, driver, renderer, resolution, other
effective graphics settings, cache protocol, speed limit and relevant device
conditions. A changed context requires a new session. A frame count alone does
not establish scene equivalence: every sample requires explicit
`sceneConfirmed = true` from the controller's reviewed scene workflow.

For each `nextCandidate()`, collect a `Comparison(before, candidate, after)`:

- All three samples have the exact context/scene, fixed guest-frame count and
  bin size. The baseline samples use the original baseline features.
- Default collection is 1,200 guest frames in 120-frame bins. Low/mean/high are
  emulation-speed percentages; p95/p99 are frame-time milliseconds. These are
  not wall-clock five-second windows. Bin counts are not independent repeats.
- Samples supply monotonic start/end times, actual complete bin count, and
  validity, pause and pacing-limit flags. Non-finite or inconsistent statistics
  cannot produce an accepted result.
- The preceding valid comparison's exact trailing baseline sample may be reused
  as the next leading baseline, including for final confirmation. All fields
  must match. Other stale or overlapping evidence is rejected.

`submit(id, comparison)` returns `ACCEPT`, `REJECT` or `INCONCLUSIVE`, explicit
reason codes, relative gains, the raw evidence and attempt count. Default policy
requires at least 3% gain in low or mean with neither regressing more than 2%.
Baseline low/mean drift above 5% makes the comparison inconclusive. Smaller drift
is interpolated at the candidate's midpoint. Highs and p95/p99 remain evidence;
a peak alone cannot pass. Thresholds are directional decision rules, not claimed
statistical confidence. A candidate receives at most one inconclusive retry;
`declineRetry(id)` skips that retry without affecting later candidates.

`bestCandidate()` ranks accepted tuples by low gain, then mean gain, then fewer
enabled features. It is a candidate for validation, not a global optimum.
`recommendation(id)` reuses a winning comparison only when it is the most recent
measurement. An older winner returns `FRESH_CONFIRMATION_REQUIRED`; provide one
fresh A/B/A through `recommendation(id, fresh)` against the original baseline.
Failure finalizes a baseline recommendation. It cannot silently switch to a
second-best tuple or retry until a favourable result appears. Passing final
validation also finalizes the session. Repeated recommendation calls return the
same result. `recommendation(null)` finalizes with the baseline.

Persist the ordered candidate plan, policy, context, scene and completed
comparison/declined-retry events. Reconstruct by replaying those events on a new
engine in order. Persist the final confirmation separately if used. The storage
controller must independently reject stale launch/settings state and leave all
graphics preferences untouched, including inherited values and later manual
edits. `requiresBaselineRestore` is an advisory instruction to return to the
comparison baseline, never permission to write preferences. Replaying valid
historical measurements does not authorize applying a profile to a changed title
or context. Only plans, captured evidence and recommendations are persisted.

Host validation compiles this actual production Kotlin class and runs synthetic
decision/lifecycle cases without Android, JNI or a device:

```sh
python3 tools/uberhar/test_tuning_engine.py --kotlinc /path/to/kotlinc --java /path/to/java
```

The runner retains source hashes, commands and results under
`build/uberhar-probe/tuning-engine/`. Passing these cases establishes the decision
contract, not Android integration correctness or measured hardware improvement.

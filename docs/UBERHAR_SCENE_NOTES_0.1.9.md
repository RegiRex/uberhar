# 0.1.9 scene annotations and 0.1.10 publication follow-up

<!-- AstraPro: Owner-supplied scene context, recorded October 1, 2026. No new device run or runtime change is implied. -->

## LEGO City: The Chase Begins

The owner reports that the high-performance sections consistently approached the
coast with the camera pointing away from the city. Values above 300 in the
existing tests correspond to that coast-facing view; below 200 corresponds to
looking inland toward the densest city area; intermediate values generally
represent travel between them. The previous analysis's near-399% Combo 2x sample
is therefore a coastline headroom result, not dense-city qualification.

Retain coast-facing, dense-city-facing and transitional sections separately.
Make the dense-city-facing view the throughput stress case and retain the coast
as a lighter-scene control. These are retrospective owner annotations, not an
emulator-detected scene classifier. Do not label future scenes solely by their
speed: a successful optimization could move a dense-city result above 200 or 300.
The numerical log measure is emulation speed (%); do not relabel it display FPS.

## Luigi's Mansion: Dark Moon

The owner replayed the opening and changed from 2x to 4x at approximately the same
visual cue: a ghost being zapped by electricity. The pre-cue 2x portions can be
compared against one another, and the corresponding post-cue 4x portions can be
compared against one another. This establishes a repeatable scene boundary and
strengthens the evidence that 0.1.9 Combo is slower in this opening workload; the
same image sequence naturally takes more wall time in a slower run.

Do not compare pre-cue 2x directly against post-cue 4x as if resolution were the
only changed variable: the scene content changes too. Keep speed-limit bands,
cache conditions, actual captured scene endpoints and host conditions explicit.
The transition is owner-aligned, not verified frame-exact alignment. Drop mixed
transition windows from fixed-resolution comparisons, retain their raw evidence.

## Sonic Racing Transformed

The owner plans to switch fast-forward off and back on at each race countdown to
mark the boundary between menu/loading and racing. Apply this convention to
future annotated tests, not retrospectively to old unmarked toggles. Prefer
leaving fast-forward off during the final loading/pre-race transition, switching
it on when the countdown begins, and retaining the chosen limit until the race
ends. This creates an observable frame/window transition rather than an
instantaneous off/on pair that may not be sampled. Mixed windows stay excluded
from fixed-limit speed aggregates, not deleted from the raw record.

Keep the race result separate from menus. Normal-100% usability and fixed-400%
capacity tests remain different measurements; the marker does not automatically
implement a game-phase detector or reconstruct previous race start times.

## Publication and implementation scope

The saved 0.1.10 candidate is still based on public 0.1.9 commit
`1bce59f62b595da60f7f386e654bffbdeea5c876`. The original review package's 78 file
hashes and all 32 candidate before/after content hashes were verified during this
follow-up. New edits in this follow-up are documentation/test context only;
runtime source is unchanged from that candidate. Existing Android, shader/pixel,
package/signing and publication gates must pass before an APK is called ready.
Actual commit, workflow and publication results are reported at handoff, not
assumed by this note. Native stays the control; Combo specialized fragments
remain an experiment requiring correct on-device output and matched-scene gains.

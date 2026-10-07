# Thor device testing — October 7, 2026 UTC

<!-- CodexAstraLocal: Record the resumed baseline separately from the original October 6 report, retaining private evidence and the distinction between two visual symptoms. -->
## Baseline 1: released 0.1.23, full Combo, 2x

**Moon flashing reproduces; sustained normal speed does not qualify. The early
book-holder ghost defect remains unresolved because this pass missed its matched
temporal checkpoint.** A later ghost clip shows coherent visible surfaces in a
different story phase and cannot clear the original defect. This was an existing
0.1.23 baseline, not a 0.1.24 candidate test.

The root coordinator exclusively operated the connected Thor. This report is an
independent offline review of the resulting ordinary logs, screenshots, brief
videos and memory readings. All raw and derived game/device artifacts remain in
ignored `build/device-testing/20261007-recovery/`; none are embedded here. The
original October 6 device report and prior private evidence remain unchanged.
Times below are UTC on October 7 (the device's local date was October 6 EDT).

### Identity, preparation and conditions

<!-- CodexAstraLocal: Scope the cumulative log to its new run and distinguish empty application caches from unknown driver-private state. -->
The selected run is **session 3**, title `0004000000055F00`, source build
`99fe4a8851ff696219dddda58fb154fbf8ce1149`. The installed release was 0.1.23,
version code 33972593; the coordinator had already verified the installed APK
against the released artifact. No new installation occurred for this baseline.
The exported current log also contains older Native and mode-4 sessions; those
are excluded from every statistic below.

Effective start/final settings agree: **full Combo (logged as Automatic, mode 3),
Vulkan, fixed 2x, enabled 100% speed limit, CPU clock 100%, JIT and accurate
multiplication enabled**. The log records zero observed setting changes, no
temporary-limit frames, `ready_gpu_vertex_policy=independent_lists_v2` and
`ready_gpu_fragment_policy=specialized_ready_v1`. Generic optimization remains
disabled; optional ready shaders use `optimized_background_v1` with optimization
enabled. The virtual controls remained visible with the owner's permission.

The launch inventory at 03:02:23.163 reports **zero generic, driver and specialized
application Vulkan cache files/bytes**. Final generic reuse is 0 hits / 3 misses
and the driver file is missing at load. Driver-internal cache state remains
unknown. The owner-authorized File 1 reset used the game's confirmation UI;
`baseline023-file-empty-check/screen.png` visibly verifies Empty before creation
and opening playback. Other saves and the retained 0.1.22 emulator state were not
part of this reset.

The timed scene starts with the recorded A input at **03:15:44.883** and ends at
the first Back input at **03:21:01.845**. The preceding approximately 13-minute
launch/menu/setup interval is excluded. Exit navigation after the endpoint is
also excluded; the actual frontend pause begins at 03:21:28.079 and lasts
13.475 seconds. Normal native return is 03:21:41.752, and the 03:22:15.841
capture verifies the library/title panel. The two lifecycle clock anchors differ
by only 0.100 ms across this run.

Recorded conditions: AC power, battery 80%, Android power saver off; battery
temperature **27°C before launch, 28°C at opening/early laboratory, 29°C before
exit and after return**. Android thermal status is 0, but the thermal HAL is not
ready and thermal headroom is unavailable. Fan/performance policy and GPU clocks
are unobserved. These battery temperatures do not establish chip temperature or
absence of throttling.

### Consecutive-frame visual evidence

<!-- CodexAstraLocal: Score recurring moon changes from ordered frames and keep the unmatched later ghost scene separate from the original dark-patch checkpoint. -->
The moon clip spans host capture **03:16:15.019–03:16:27.199** and contains 246
decoded frames over 12.004 seconds. Reviewed consecutive sequences 60–75,
90–105 and 120–135 show repeated abrupt darkening/striping of individual facets
while the outline, glow and nearby terrain remain coherent. In particular,
frames **94–97**, at relative times **4.508, 4.558, 4.608 and 4.659 seconds**,
show the right/top facets developing dense dark stripes and horizontal bars,
then returning mostly purple. A fixed-position, fixed-scale crop verifies that
automatic crop changes do not explain this observation. Controls and the
performance overlay are outside that crop.

This confirms the owner's Combo moon-flashing symptom in the new baseline.
It does not identify its shader or draw cause. Native remains the owner's
nonflashing reference; no new Native run was performed. Lossy display capture
does not supply exact guest-frame alignment or a calibrated flashing frequency.

The early wide book-holder checkpoint fell between the 03:17:39.679 E. Gadd/ghost
close-up and the 03:18:43.921 broom/chalkboard close-up, and was not recorded as
consecutive footage. The later clip at **03:20:32.811–03:20:45.043** is a
different phase: the wide laboratory view includes purple shock effects above
the ghosts, followed by a close view of the red-book holder. Its 143 decoded
frames span 11.853 seconds. Reviewed sequences 0–15, 48–63 and 96–111 show
coherent visible ghost surfaces without the prior dense irregular near-black
crown/face patches. Broad background-aligned transparent bands and the scripted
shock/eye changes are not scored as that defect. **This later observation does
not clear the early wide book-holder corruption or prove a fragment-route fix.**

The next matched checkpoint is the earlier pullback with the red electrical
apparatus at lower left, E. Gadd/chair at far right and five ghosts across the
room; the implicated book-holder is beside the chalkboard at upper right.
Capture on that camera transition before the broom close-up. Prior elapsed
times are guides only because scene speed varies.

### Opening timing

<!-- CodexAstraLocal: Independently recompute complete normal-limit window statistics and separate video overlap without claiming a capture-overhead experiment. -->
Two offline computations agree. Included windows are complete, wholly inside the
scene boundary, mode 3, 2x and 100% limit, with no mixed or unknown rendering
context. They cover **03:15:46.969–03:20:59.439**. The speed metric is logged
guest-time/wall-time progress, not an estimate from the FPS overlay or video
frame cadence.

| Statistic | All complete scene windows | Excluding any video overlap | Video-overlapping windows |
| --- | ---: | ---: | ---: |
| Windows | 62 | 54 | 8 |
| Observed wall seconds | 312.469 | 272.222 | 40.247 |
| Wall-weighted speed | 23.803% | 23.448% | 26.204% |
| Game submissions / second | 14.222 | 14.011 | 15.653 |
| Wall-weighted p05 window speed | 16.385% | 16.245% | 16.813% |
| Window speed range | 15.970–65.036% | 15.970–65.036% | 16.813–32.783% |
| Wall seconds in windows averaging below 95% | 312.469 | 272.222 | 40.247 |
| Worst observed interval | 228.541 ms | 228.541 ms | 107.632 ms |

The worst interval is in the 03:15:52.010–03:15:57.033 window; the slowest window
is 03:19:13.730–03:19:18.753. Every included window averages below 95%. The p05
and below-target durations describe approximately five-second **window
averages**, not individual-frame speed distributions. Removing video overlaps
changes the scene mix and cannot quantify recording overhead; ordinary
screenshots/readings remain. Different scene coverage, setup duration and
conditions prevent a matched speedup claim against older runs. **2x must still
qualify before pursuing 4x.**

### Memory and route evidence

<!-- CodexAstraLocal: Separate scene maxima, later exit-navigation allocation and normal recovery; preserve overlapping-counter and asynchronous-sampling limits. -->
| Observation | UTC | KGSL MiB | Process RSS MiB | System available MiB |
| --- | --- | ---: | ---: | ---: |
| External pre-launch capture | 03:02:21.567 | 11.324 | 357.727 | 11,732.883 |
| `before_native_run` | 03:02:22.830 | 10.707 | 363.148 | 11,714.590 |
| Before opening, after setup | 03:15:42.418 | 2,033.844 | 978.320 | 9,069.020 |
| Highest sampled KGSL inside scene | 03:20:59.678 | 3,315.781 | 1,006.535 | 7,779.918 |
| Whole-run sampled peak, after first Back | 03:21:22.981 | 3,393.629 | 1,013.855 | 7,693.977 |
| `after_native_run` | 03:21:41.752 | 13.531 | 517.039 | 11,361.922 |
| Verified library/title-panel capture | 03:22:15.841 | 13.828 | 480.770 | 11,514.816 |

The live footprint is large and system memory recovers on normal exit. This run
does not demonstrate a post-exit GPU leak, sustained memory bounds or a memory
cure. Sequential external readings are not atomic. KGSL, RSS, raw streams and
allocator counters overlap and must not be added.

Teardown records zero live raw-stream bytes, VMA allocations, descriptor pools
and command-buffer capacity after dependents are destroyed. Raw streams peaked
at 604.125 MiB and sampled VMA blocks at 224 MiB. One 64-MiB allocator block
remained at that pre-allocator-destruction checkpoint.

Full-title route totals include setup and exit navigation, unlike the timing
table: 2,530,154 ready GPU batches, 16,577,944 CPU batches, 37 ready GPU pipelines,
26 optional specialized fragment modules and 1,961,072 optional specialized GPU
draws. Quaternion interpolation retained 4,987,486 batches on CPU; no lit
accelerated sparse snapshot was recorded. There were **zero executed compute
rectangles/pixels**, so that route cannot explain the captured moon flashing in
this run. These aggregate counts do not identify a ghost or moon draw.

There are no reported optional shader/pipeline failures, ready key mismatches,
or generic-module rejects/write failures. The five selected-session error lines
occur during initial setup, before the opening: three default FS delay-generator
messages and two missing other-profile save files. No error/critical line occurs
inside the timed scene. The 17-file export includes historical crash artifacts;
their presence does not indicate a new crash in this completed run.

Pipeline population again correlates with live KGSL growth: at 32 completed
ready pipelines (18 with color, 14 without), KGSL is 3,049.375 MiB; at 36
(21 with color, 15 without), it is 3,315.781 MiB. The previously documented
72/48-MiB aggregate coefficients leave residuals around 1,081–1,084 MiB at these
points. This is consistent with retained evidence, but remains a correlation of
non-atomic samples, not measured allocation ownership or proof of the cause.

### Private evidence and continuation

<!-- CodexAstraLocal: Supply resumable provenance without publishing game-derived payloads or mixing the next baseline/candidate into this completed result. -->
Current ordinary log: `baseline023-log/azahar_log.txt`, SHA-256
`33d18a5ae94f38084e67057d3e3a96405040eded383ae4730960c98716b54e76`.
The run is bounded by log lines 5003–12592 and its preceding lifecycle anchor at
4888. Selected complete frame windows begin at line 10380 and end at 12356.

Private derived results live under:

- `analysis/baseline023-run3/`: retained-analyzer reuse, independent raw-log
  recomputation, timing windows, pipeline/memory joins and export hashes.
- `analysis/baseline023-moon-031615/`: original selected decoded frames,
  sky-only consecutive sheets, constant-position evidence and `moon-review.json`.
  Early broad automatic crops sometimes included unrelated purple pixels;
  `sky-moon-*.png` and `fixed-roi-090-105.png` supersede them for inspection.
- `analysis/baseline023-later-book-032032/`: full-scene consecutive sheets,
  selected original decoded frames and clip provenance for the later ghost phase.

Continue with the matched early wide checkpoint, then compare the gated 0.1.24
candidate when its build/signing/publication requirements and installation checks
are satisfied. Keep output-W fallback coverage, moon flashing and ghost patches
as separate results. No finding here establishes a title-specific cause or
qualifies a 0.2.0 release.

## Baseline 2: released 0.1.23, temporal checkpoint missed

<!-- CodexAstraLocal: Retain the second cold baseline as a completed measurement and failed capture attempt, without manufacturing a ghost or moon conclusion from stills. -->
The second baseline again used **0.1.23 build `99fe4a885`, full Combo, Vulkan,
fixed 2x, 100% limit and CPU clock**, with JIT/accurate multiplication enabled.
Its selected ordinary-log scope is **session 4**; start/final settings agree with
no observed changes or temporary-limit frames. Application Vulkan cache inventory
is again empty at launch: zero generic, driver and specialized files/bytes,
0 generic hits / 3 misses, driver file missing. Driver-private cache remains
unknown. The reset's Empty File 1 screen and the normal-return library/title
panel were independently inspected.

The early wide book-holder scene was not captured in consecutive footage. The
retained 03:28:49.851 screenshot is around opening +121 seconds, while the later
`baseline2-ghost-approach` image is another camera angle. **This is a failed
temporal checkpoint, with no new title-graphics conclusion.** No video was
recorded during this pass. A subsequent separately reported pass is scheduled
to record across the anticipated transition instead of relying on isolated
manual screenshots.

<!-- CodexAstraLocal: Bound timing by recorded opening/first-Back actions and retain the meaning of lower-tail window statistics. -->
Opening A input: **03:26:48.597**. First Back/scene endpoint:
**03:31:24.121**. Complete normal-limit windows span
**03:26:52.121–03:31:23.977**; prior setup and later exit navigation are excluded.

| Statistic | Baseline 2 complete scene windows |
| --- | ---: |
| Windows / observed wall seconds | 54 / 271.856 |
| Wall-weighted speed | 24.426% |
| Game submissions / second | 14.596 |
| Wall-weighted p05 window speed | 16.278% |
| Wall seconds in windows averaging below 95% | 271.856 (all) |
| Worst observed interval | 392.782 ms |

The worst interval is in 03:28:58.075–03:29:03.076. Lower-tail and below-target
values describe whole window averages. Screenshots/readings remain overhead;
different coverage and conditions prevent a speedup inference against baseline
1. The recorded speed is far below sustained 100%.

<!-- CodexAstraLocal: Keep the later exit-navigation maximum separate from the timed scene and summarize recovery without asserting a leak or thermal cause. -->
AC power, battery 80%, power saver off and **29°C battery temperature** are
recorded throughout. Thermal status is 0 with the HAL unavailable/headroom
unknown; hardware mode and GPU clocks remain unobserved.

| Observation | UTC | KGSL MiB | Process RSS MiB | System available MiB |
| --- | --- | ---: | ---: | ---: |
| `before_native_run` | 03:24:50.960 | 10.730 | 422.430 | 11,587.863 |
| Highest sampled KGSL inside scene | 03:31:21.010 | 3,269.664 | 1,023.184 | 7,763.398 |
| Whole-run sampled peak, after first Back | 03:31:51.014 | 3,318.352 | 1,014.750 | 7,693.012 |
| `after_native_run` | 03:31:58.012 | 18.953 | 518.488 | 11,288.691 |
| Verified library/title-panel capture | 03:32:00.168 | 19.270 | 499.977 | 11,421.168 |

The frontend pause was 3.445 seconds during exit, fully outside the scene.
Lifecycle clock-anchor drift is -0.386 ms. Memory again recovers normally; the
sampled counters neither prove sustained bounds nor demonstrate a post-exit GPU
leak. They overlap and are not additive.

Whole-title routes include setup/exit: 710,413 ready GPU batches, 36 ready GPU
pipelines, 25 optional fragment modules and zero executed compute rectangles.
No optional shader/pipeline failure or ready key mismatch is reported. Five
startup error lines have the same delay-generator/missing other-profile pattern
as baseline 1 and precede the scene; none occurs inside it.

<!-- CodexAstraLocal: Keep resumable provenance for this pass separate from later captures and the candidate version. -->
Private results: `analysis/baseline2-run4/retained-analyzer.json` and `.txt`.
Source log: `baseline2-log/azahar_log.txt`, selected run lines 12725–15613 with
the preceding lifecycle anchor at 12610; SHA-256
`b31836b32da67ca29d6d12aa97fd2285e2f64cc5db9a8cd9b2d69bdce636a1ed`.
This pass used the existing release and is not a new candidate build/test.

## Baseline 3: released 0.1.23, early ghost corruption captured over time

<!-- CodexAstraLocal: Establish the completed cold run's scope before comparing actor-specific consecutive evidence. -->
The third baseline used **0.1.23 build `99fe4a885`, full Combo, Vulkan, fixed
2x, 100% limit and CPU clock**, with JIT and accurate multiplication enabled.
Ordinary-log **session 5** has matching start/final settings, zero observed
setting changes and no temporary-limit frames. Application cache inventory is
empty again: zero generic, driver and specialized files/bytes at launch,
0 generic hits / 3 misses and a missing driver cache file. Driver-private cache
state remains unknown. The reset's Empty File 1 screen and normal-return
library/title panel were independently inspected. The permitted controller
overlay remained enabled; its regions are excluded from visual judgments.

### Consecutive ghost evidence

<!-- CodexAstraLocal: Separate three visible actors/phases and retain clear-patch-clear transitions rather than judging one still. -->
The primary clip runs **03:37:26.625–03:38:11.881 UTC**, beginning about
opening +110.037 seconds; it contains 489 decoded frames over 44.891 seconds.
It covers the E. Gadd pullback and the requested early wide laboratory view,
including the electrical apparatus, chalkboard and right book-holder.

| Observed actor/phase | Consecutive decoded evidence |
| --- | --- |
| Near ghost beside E. Gadd, before the full pullback | Frame 118 is clear; frames **119–122**, PTS **10.702933–10.963278 s**, acquire dense dark jagged patches across the head, face and torso; frame 123 is clear again. |
| Moving foreground ghost above the electrical apparatus | Frames **396–411**, PTS **35.280278–36.827322 s**, retain dark patches while the ghost moves; this actor is separate from the right book-holder. |
| Right book-holder in the early wide view | Frame 448 is clear; frames **449–454**, PTS **40.748833–41.253456 s**, show dense dark jagged head/body patches; frame 455 is clear again. The later inspected frames 460–488 are also clear. |

The last sequence matches the historical book-holder/camera checkpoint and
establishes temporal corruption in the current full-Combo baseline. Its brief
patch episode occurs approximately opening **+150.785 to +151.290 seconds**.
Those offsets use host capture start plus encoded PTS and are approximate, not
exact GPU submission timestamps. The clear frames before and after explain why
isolated later poses could look correct on this same unchanged build.

A separate later clip runs **03:38:43.283–03:38:55.625 UTC**, with 121 decoded
frames over 11.679 seconds. In this different camera angle, the closer foreground
ghost holding a cylindrical container has dark/brown jagged and stippled patches
in inspected sequences 0–15 and 40–57. The patches disappear between consecutive
frames **57 and 58** (PTS **5.546144 and 5.645344 s**) while that ghost continues
turning; its broad transparent background bands remain. This corroborates a
temporal ghost defect separately from the early book-holder.

These conclusions come from consecutive full-scene frames and fixed-position
crops. The dense irregular patches are distinct from the broad background bands
visible through the translucent bodies. They do not establish the responsible
shader stage. No matched Native or mode-4 temporal recording of these exact
early poses is retained. There is no new moon clip in this pass: baseline 1's
moon result remains separate, and the owner's Native moon correction is unchanged.

### Scene timing and capture windows

<!-- CodexAstraLocal: End the measured scene at the first Back action and exclude the unusually long subsequent navigation tail. -->
Opening A input is **03:35:36.589**; first Back is **03:39:24.675**. Complete
normal-limit windows span **03:35:37.727–03:39:24.360**. Setup, the long drawer
tail and subsequent close confirmation are excluded, even where the emulator
continued running. The 27.120-second frontend pause occurs later during exit,
at 03:41:40.713–03:42:07.833, entirely outside the measured scene.

| Statistic | All complete scene windows | Excluding either video overlap | Video-overlap windows only |
| --- | ---: | ---: | ---: |
| Windows / observed wall seconds | 45 / 226.633 | 31 / 156.068 | 14 / 70.565 |
| Wall-weighted speed | 21.940% | 23.775% | 17.883% |
| Game submissions / second | 13.105 | 14.193 | 10.699 |
| Wall-weighted p05 window speed | 16.264% | 17.211% | 16.242% |
| Wall seconds in windows averaging below 95% | 226.633 (all) | 156.068 (all) | 70.565 (all) |
| Worst observed interval | 240.476 ms | 240.476 ms | 108.177 ms |

The worst scene interval falls in 03:35:42.732–03:35:47.739. The 362.009-ms
whole-title maximum belongs to setup and is excluded. Video-overlap means any
overlap with a capture's host start/end, so its windows include time outside
the actual encoded footage. These are window-average lower-tail/below-target
statistics, not per-frame speed quantiles. Capture exclusions change scene mix
and do not estimate recording overhead or a build-to-build speedup. Performance
remains far below sustained 100%.

### Conditions, memory and routes

<!-- CodexAstraLocal: Keep timed-scene memory distinct from the later navigation peak and avoid interpreting recoverable driver allocations as a leak. -->
Recorded conditions remain AC powered, battery 80%, power saver off and
**29°C battery temperature**. Thermal status is 0 with HAL unavailable and
headroom unknown. Hardware mode, fan policy and GPU clocks remain unobserved;
no low-memory flag is recorded. Lifecycle clock-anchor drift is -0.967 ms.

| Observation | UTC | KGSL MiB | Process RSS MiB | System available MiB |
| --- | --- | ---: | ---: | ---: |
| `before_native_run` | 03:33:01.476 | 10.730 | 421.730 | 11,574.969 |
| Before opening input | 03:35:34.433 | 2,033.676 | 991.488 | 8,992.309 |
| Highest sampled KGSL inside scene | 03:39:01.525 | 3,121.223 | 1,021.680 | 7,880.531 |
| Whole-run sampled peak, after first Back | 03:41:31.542 | 3,388.277 | 1,035.078 | 7,616.898 |
| Verified library/title-panel capture | 03:42:07.952 | 13.859 | 528.082 | 11,390.203 |
| `after_native_run` | 03:42:08.049 | 13.348 | 531.352 | 11,277.582 |

The host capture starts just before the final lifecycle sample; its individual
readings are sequential, not an atomic snapshot. Recovery is again visible.
After-dependent teardown reports zero VMA allocations, raw stream bytes,
descriptor pools and command-buffer capacity; one 64-MiB VMA block remains
at that point before allocator destruction. This short run does not establish
full-game memory bounds. KGSL, RSS and allocator measurements overlap and must
not be added.

At the scene's highest sampled KGSL, 33 ready pipeline detail records comprise
19 color-attachment and 14 no-color-attachment variants. At the later maximum,
37 comprise 22 and 15 respectively. The previously retained 72/48-MiB population
hypothesis leaves approximately 1,081/1,084 MiB at these two samples. This is
another non-atomic population correlation, not measured allocation ownership;
the larger later maximum includes extra content after the timing endpoint.

Whole-title totals, including setup and exit, report 917,566 ready GPU vertex
selections, 37 ready GPU pipelines, 26 optional fragment modules and zero
executed compute rectangles. Optional shader/pipeline failures and ready key
mismatches are zero. Five startup error lines repeat the prior delay-generator
and missing-other-profile pattern; none occurs in the timed scene. The 17-file
ordinary export retains historical crash evidence and does not establish a new
crash in this normally exited run.

### Provenance and next comparison

<!-- CodexAstraLocal: Preserve private reproducibility and use matched temporal scene content, rather than wall time alone, for the candidate comparison. -->
Private derivations are `analysis/baseline3-run5/retained-analyzer.json` / `.txt`
and `independent-summary.json`; independently recomputed scene aggregates agree.
Selected ordinary-log run lines are 15746–19244, with the preceding lifecycle
anchor at 15631. Source `baseline3-log/azahar_log.txt` SHA-256:
`efb76318539ed97227f11d3e7405090938c1396c2ebb268890244a1670c9604c`.

The private visual directories `analysis/baseline3-early-ghost/` and
`analysis/baseline3-ghost-corruption/` retain clip hashes, original PTS, selected
full-resolution frames, consecutive sheets, fixed-crop bounds and
`temporal-review.json`. Primary clip SHA-256:
`2264e84098dbff450ed10a1deb190b0eb5a8bdf81529a33b6fc73e90b9748c7e`;
later clip SHA-256:
`0b89da33067d4ed39bd32f09407b4f8851a85ddbf42b486f6103960196ad685b`.
Raw game-derived images and footage remain private and ignored.

This pass provides the intended early-wide temporal comparator for the gated
0.1.24 candidate. Match the camera/actor sequence, allowing for changed scene
timing; a fixed wall-time offset alone may miss it if speed changes. Record
output-W fallback coverage, moon flashing and ghost patches separately. A clean
still or one captured vertex packet cannot clear a fault that changes between
consecutive frames. No new candidate build was used in this baseline.

## Candidate 0.1.24: both symptoms persist, no output-W rejections

<!-- CodexAstraLocal: Record the gated candidate's actual device outcome before distinguishing visual evidence from guard coverage and throughput. -->
**Moon flashing and the matched early-wide book-holder corruption both persist
in 0.1.24.** The new output-W guard ran 773,137 checks but rejected no draw.
This is a completed device test of the candidate, not evidence that its narrow
synthetic output-default fix addresses either Dark Moon symptom.

The coordinator installed the compatible published update after the shader,
Android, package, signing and publication gates passed in
[run 37566500834](https://github.com/RegiRex/uberhar/actions/runs/37566500834).
The [0.1.24 prerelease](https://github.com/RegiRex/uberhar/releases/tag/0.1.24)
targets `970805ddcf448bfc6b7d437dfc6a4c9632ec4c2d`, version code 33973716.
Published APK SHA-256 is
`8c9ada7a510804cffa7a23bef341d761ea80ceb5f0a009d6b873b8e97b0748f6`.
Private verification/provenance is retained in `candidate-024/`; the update
preserved application data and used compatible signing.

The selected ordinary-log scope is the new process's **session 1**, not session
5 from the prior APK. Its exact build, full Combo, Vulkan, fixed 2x, 100% limit
and CPU clock, JIT and accurate multiplication agree at start and finish, with
zero observed setting changes or temporary-limit frames. Empty File 1 and the
normal-return library/title panel were independently inspected. The application
Vulkan cache again starts with zero generic, driver and specialized files/bytes,
0 generic hits / 3 misses and a missing driver cache file. Driver-private cache
state remains unknown.

### Matched temporal graphics

<!-- CodexAstraLocal: Compare corresponding scene/actor phases across versions and retain the clear-patch-clear transitions that isolated screenshots miss. -->
The moon clip runs **03:47:55.939–03:48:08.122 UTC**, with 250 decoded frames
over 12.016 seconds. Consecutive frames **122–124**, PTS **5.781956, 5.832533,
5.882511 s**, change from a mostly purple right facet to conspicuous black
horizontal bars and then lose those bars. Abrupt dark top/right facets recur
in inspected sequences 60–75 and 90–105. The surrounding scene and moon glow
remain coherent. This is the same scenic phase and temporal symptom as the
0.1.23 baseline; the controller/performance overlay is outside the reviewed ROI.

The early ghost clip runs **03:49:17.939–03:50:03.236 UTC**, beginning about
opening +110.038 seconds, with 489 decoded frames over 44.814 seconds. It covers
the same pullback and wide laboratory phase as baseline 3.

| Matched actor/phase | Candidate consecutive evidence |
| --- | --- |
| Near ghost beside E. Gadd | Frame 112 is clear; **113–116**, PTS **10.141500–10.401900 s**, have dense dark head/face/body patches; frame 117 clears. |
| Moving foreground ghost above the electrical apparatus | **392–407**, PTS **34.928589–36.468811 s**, retain extensive dark/brown patches while the ghost moves. |
| Right book-holder in the early wide view | Frame 442 is clear; **443–448**, PTS **40.163711–40.669522 s**, have dense dark jagged head/body patches; frame 449 clears. |

The book-holder transition matches the baseline 3 defect and occurs approximately
opening **+150.202 to +150.708 seconds**. Different frame indices and small
timing shifts do not change the matched actor/camera interpretation. Broad
transparent background bands remain visible in both patched and clear poses.
Later clear stills, including the closer broom shot, cannot clear these observed
faults. No matched later cylindrical-container clip was taken for this candidate;
the early book-holder comparison is sufficient to show persistence of that
diagnostic target. Neither visual result identifies a shader-stage cause.

### Output guard and route coverage

<!-- CodexAstraLocal: Distinguish an evaluated guard from actual CPU fallback coverage, and preserve the conservative write-union proof's limits. -->
The ordinary output-guard total is at log line 2773. It reports:

| Counter | Value |
| --- | ---: |
| Optional batches reaching the guard (`checked`) | 773,137 |
| Checks with consumed W (`consumed_w`) | 773,137 |
| Never-written consumed-W CPU fallbacks (`never_written_w`) | **0** |
| Memo entries / scans / hits | 1 / 1 / 773,136 |

No bounded fallback detail record appears, consistently with zero rejections.
These are renderer-lifetime admission attempts after existing layout/quaternion
exclusions and before index reads, uploads and optional-fragment readiness.
They do not identify the draw responsible for a visible pixel. The guard was
exercised, but its proven rejection case had **no title fallback coverage** in
this run. Zero rejections do not establish general output parity: possible
conditional/unreachable writes or unknown words can prevent the narrow absence
proof. One program/swizzle memo entry can serve different uniforms, mappings,
draw layouts and scenes; its hash alone cannot isolate an affected ghost.

Whole-title routes include setup and exit: **714,660 GPU batches**, 4,406,255
CPU batches, 34 ready GPU pipelines, 23 optional fragment modules and zero
executed compute rectangles. Quaternion-interpolation CPU exclusions total
1,666,721. Optional shader/pipeline failures and ready key mismatches are zero.
The ordinary total at line 2786 records **`skipped=0`**; this is logged route
coverage rather than proof of full-game rendering correctness. Five startup
error lines repeat the prior delay-generator/missing-other-profile pattern;
none occurs inside the measured scene.

### Scene timing and memory

<!-- CodexAstraLocal: Use recorded opening/first-Back boundaries, keep video windows separate and avoid interpreting unlike scene coverage as a candidate speedup. -->
Opening A is **03:47:27.901** and first Back is **03:51:01.394**. Complete
normal-limit windows span **03:47:32.511–03:50:59.429**. Setup, partial boundary
windows and subsequent navigation are excluded. The 0.997-second frontend pause
at 03:51:26.089–03:51:27.086 occurs entirely outside the scene.

| Statistic | All complete scene windows | Excluding either video overlap | Video-overlap windows only |
| --- | ---: | ---: | ---: |
| Windows / observed wall seconds | 41 / 206.918 | 27 / 136.390 | 14 / 70.528 |
| Wall-weighted speed | 21.567% | 21.065% | 22.536% |
| Game submissions / second | 12.879 | 12.567 | 13.484 |
| Wall-weighted p05 window speed | 16.514% | 16.536% | 16.294% |
| Wall seconds in windows averaging below 95% | 206.918 (all) | 136.390 (all) | 70.528 (all) |
| Worst observed interval | 402.226 ms | 402.226 ms | 107.016 ms |

The worst interval falls in 03:48:43.117–03:48:48.127, overlapping the ordinary
opening screenshot/readings operation. Its source cannot be assigned from that
coincidence. Lower-tail and below-target values describe whole logged window
averages. This pass's endpoint and video phases differ from baseline 3's; the
summary does not measure a build speedup, regression or recording cost. Sustained
100% remains unqualified by a large margin.

<!-- CodexAstraLocal: Preserve the new process's power conditions and normal-return memory recovery without comparing unlike process histories as an optimization result. -->
Conditions remain AC powered, battery 80%, power saver off and **29°C battery
temperature**. Thermal status is 0, HAL unavailable and headroom unknown;
hardware mode, fan policy and GPU clocks remain unobserved. No low-memory flag
is recorded. Lifecycle clock-anchor drift is +0.740 ms.

| Observation | UTC | KGSL MiB | Process RSS MiB | System available MiB |
| --- | --- | ---: | ---: | ---: |
| `before_native_run` | 03:44:56.105 | 9.668 | 220.461 | 11,869.555 |
| Before opening input | 03:47:12.269 | 2,035.441 | 933.535 | 9,148.141 |
| Highest sampled KGSL inside scene | 03:50:56.154 | 3,122.926 | 975.684 | 8,034.754 |
| Whole-run sampled peak, after first Back | 03:51:26.158 | 3,199.957 | 957.887 | 7,957.730 |
| Verified library/title-panel capture | 03:51:27.201 | 13.340 | 320.426 | 11,730.035 |
| `after_native_run` | 03:51:27.273 | 13.465 | 461.879 | 11,473.480 |

Host capture timestamps mark the start of sequential readings and are not atomic
snapshots of all counters. KGSL recovers normally. The candidate uses a new
process after installation, whereas the preceding baselines reused a process;
lower RSS or a different after-exit sample cannot be attributed to this change.
The scene's sampled GPU peak has 33 pipeline records, 19 with color and 14
without, matching baseline 3's population at its scene peak. It leaves about
1,083 MiB under the prior 72/48-MiB population hypothesis, which remains a
correlation rather than allocation ownership. Short-run recovery does not
qualify sustained full-game bounds; overlapping memory counters are not additive.

### Provenance and next evidence

<!-- CodexAstraLocal: Make the zero-coverage result resumable and direct the next change toward captured draw evidence instead of speculative title attribution. -->
Private `analysis/candidate024-run1/` contains the reused offline analyzer,
independent raw-log aggregates, export hashes and `output-guard-coverage.json`.
Independent scene calculations agree. Source `candidate024-log/azahar_log.txt`
uses run lines 145–2830 and lifecycle anchor 28; SHA-256:
`f0ae3744e64851dc38ad60a7696e1226d352a9df25298f776e39b8db23754ef9`.
The 17-file export includes retained historical crash artifacts; this run ends
normally and supplies no new crash conclusion.

Private `analysis/candidate024-moon/` and `analysis/candidate024-early-ghost/`
contain clip metadata/hashes, exact PTS, original selected frames, consecutive
sheets, fixed-crop bounds and `temporal-review.json`. Moon clip SHA-256:
`c431b85f62c863242b26dc05c0adfbbb34063deccf2b38f6dc13ae6780cebcab`;
early ghost clip SHA-256:
`4937a505d778cff9dfe66d1460d18de9883630ef5c7375daa491c5b0c10f71ee`.
Raw game-derived content remains private and ignored.

The next authorized implementation is the handoff's bounded, default-off capture
of immutable data from successfully submitted implicated draws. Retain actual
input/index order, shader/uniform/output mapping identity, lifetime and byte
bounds, then replay production CPU/GPU contracts before assigning a cause.
Capture must identify the scene and draw within it; a program hash or a single
clean frame is insufficient. Treat packet recording, queue acceptance and GPU
completion separately, and keep moon versus ghost conclusions independent.

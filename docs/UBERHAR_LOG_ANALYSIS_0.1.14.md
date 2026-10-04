# 0.1.14 device evidence and logging correction

<!-- AstraEH: October 4, 2026. Separate owner observations, recorded data and hypotheses. -->

Reviewed build: `5664fac1f1dd06a773ad9c973ed55e26ab208dc5`. Its Android, shader,
package, signing and publication gates passed. Device validation exposed a logging
path defect that those gates did not cover. This review selects 0.1.15 reliability
work; it is not a reset of the full 0.1.13 architecture-review anchor.

## Inputs and interpretation

Two owner exports: `azahar_log.older(1).txt` (4,684 lines / 1,476,082 bytes) and
`uberhar_log_10_4_1410_SASRT_FEA_LCUTCB_MH4U(1).txt` (9,122 lines / 2,773,863 bytes).
The first starts October 3 at 19:11:02 -04:00; the second October 4 at 14:10:10
-04:00. Eleven title runs total: ten orderly endings and one incomplete Dark Moon
run. All recorded runs use Vulkan, 2x resolution and guest CPU clock 100%.

The owner used speed toggles to mark Sonic laps and gameplay in the second LEGO
run, and briefly checked FEA acceleration. Actual recorded limits take precedence
over assumptions: Sonic, both LEGO runs and the long MH4U run spend most of their
recorded time at the temporary 400% limit. Keep these as separate fast bands, not
normal-speed samples. The configured turbo preference is 200%, while the temporary
override actually observed is 400%; use the observed limit for classification.
Exclude transition intervals. Markers identify approximate scene boundaries within
the existing five-second windows, not exact lap completion or race timing.

## Recorded bands

Normal and fast columns are time-weighted speed bands emitted by the application.
They are not frame-time percentiles or averages of the overlay. A3 has no final
band; its estimate uses only complete captured normal windows.

| Run | Game / mode | Normal band: speed, seconds | Fast band: speed, seconds | Interpretation |
| --- | --- | --- | --- | --- |
| A1 | Dark Moon / Combo | 14.702%, 264.322 s | — | Orderly ending; owner still reports graphical faults. |
| A2 | Pac-Man & Galaga / Combo | 94.055%, 3.785 s | — | Too brief for a gameplay qualification. |
| A3 | Dark Moon / Combo | About 15.461% over complete windows only | — | Incomplete ending; final complete windows 10.026%, 9.388%, 9.569%. |
| B1 | Sonic / Combo | 94.467%, 4.954 s | 124.871%, 151.301 s | Very little normal-speed evidence; see marker analysis below. |
| B2 | FEA / Combo | 98.652%, 58.857 s | — | Strong sampled normal behavior. |
| B3 | FEA / Native | 98.612%, 54.644 s | 252.561%, 5.446 s | Brief acceleration check separated from ordinary play. |
| B4 | LEGO / Native | 99.294%, 11.547 s | 284.421%, 130.338 s | Most of the run is accelerated. |
| B5 | LEGO / Combo | 98.636%, 3.253 s | 391.805%, 137.969 s | Strong 2x headroom; gameplay marker helps locate sustained section. |
| B6 | MH4U / Combo | 98.308%, 10.507 s | 208.064%, 26.453 s | Short startup sample, not equivalent to the later long run. |
| B7 | FEA / Combo | 95.713%, 2.777 s | — | Brief restart; not a meaningful battle benchmark. |
| B8 | MH4U / Combo | 99.767%, 5.461 s | 81.196%, 441.926 s | Sustained heavy section remains well below full speed despite acceleration. |

Application cache inventory is empty for A1–A3 and B1–B7; B8 has two generic files
present. This says nothing definitive about driver-internal cache warmth. The gap
between A2 and A3 is outside active title-run performance and includes device sleep;
do not average it into game speed.

### Sonic and LEGO

Sonic's three owner-described race/lap marker windows end about 25.2, 70.3 and
115.4 seconds after run start. Excluding the mixed marker windows, the following
three approximate segments each contain eight complete windows: means 92.516%,
91.188% and 119.936%, with minimum window means 86.072%, 84.450% and 91.188%.
The final segment may include post-race activity; there is no explicit finish
marker. Thus 124.871% for the entire fast band is not a full-speed racing floor.
The owner's positive impression is consistent with useful Combo behavior, but
sustained 100% racing is not established. Optional GPU work selects 4,060,883
draws, with only 0.427 s aggregate optional driver creation in this recording.

LEGO Combo's 26 complete unmixed fast windows average 393.800%, ranging from
351.240% to 399.907%. Native's 25 corresponding fast windows average 282.235%,
ranging from 185.027% to 399.934%. These are different runs, not matched camera
positions; do not turn the ratio into a controlled speedup claim. Combo reaches
the existing 256-pipeline/128-fragment limits while remaining strong in this
sample. No cap increase is justified solely by reaching those limits.

### MH4U

B8 records zero GPU promotion attempts and zero promoted draws. Its exclusion
counts are 4,735,670 topology batches and 2,465,432 guest-geometry batches.
CPU vertex processing takes 302.729 s out of 447.405 s observed, about 67.7%.
The 87 complete unmixed 400% windows average 78.456% (35.301–223.483%). Earlier
0.1.9 Combo/2x/400% windows averaged 80.467% over a different, shorter section.
The present evidence does not establish a new regression, nor does it show a
fix: this is still the separate CPU/coverage bottleneck. Thermal status is zero
throughout the sampled run, battery temperature reaches 27 C and available system
memory stays around 9,500 MiB. Host performance mode and GPU clocks are unobserved.

Two `Unimplemented gas mode` critical records occur during B8. The title continues
to an orderly ending. They identify an existing unsupported rendering feature,
not a fatal backtrace or a measured cause of its slowdown. Keep it on the fidelity
ledger; do not bypass geometry/topology correctness guards to chase a speed number.

## Dark Moon crash evidence and the next diagnostic target

The older export preserves a second Dark Moon run through process time 6466.713 s.
There is no normal run-end record, fatal stack/signal, Java exception or OS exit
report in that text. This is consistent with the owner's crash report, but cannot
distinguish a native fault, OS kill or another abrupt exit. Do not label the last
ordinary log call as the crashing function.

A1 builds 81 optional pipelines, with 113.969 s aggregate background driver time.
After its final optional completion at 319.358 s, six complete windows spanning
30.551 s still average 12.254%. A3 builds 99 optional pipelines, accumulating
201.272 s driver time by its final progress record. Its final two complete windows
after the last optional completion average 9.478%. Background build totals are not
foreground stalls. Readiness may still relate to the fading graphical anomalies,
but cannot by itself explain the persistently slow late execution.

**New memory clue:** available system memory in A1 falls from 10,186 MiB before
the run to 1,912 MiB at its last health sample. Reported native heap remains around
0.3 GiB in later A1 samples and falls to about 38 MiB in the following title, while
available system memory remains low. A3 starts at 3,454 MiB and reaches 1,280 MiB
at its last health sample; its native-heap field is unknown. Thermal status remains
zero and Android's low-memory flag is false. System-wide free-memory change is not
proof of per-process leakage, driver allocation or OOM termination. It does make
process/GPU memory attribution and next-launch exit evidence high-priority focused
diagnostics alongside route/state isolation. No memory eviction experiment is
selected without ownership and measurement evidence.

Do not ask for another long failing Dark Moon run. When focused renderer testing
resumes, use a short matched scene and stop at the first visible anomaly. Separate
GPU preparation from actual promotion, then isolate vertex and fragment routes.
Broader optimization experiments remain deferred until graphical correctness.

## Logging root cause and 0.1.15 scope

The 0.1.14 frontend correctly created an app-private physical `log.txt`, but handed
its unmarked absolute pathname to `FileUtil::IOFile`. On Android raw-file builds,
`TranslateFilePath` interprets such paths as relative to the emulation data folder.
The writer therefore opens the wrong location and fails, leaving the frontend's
real file empty. That explains the startup health warning and the empty bundles,
while the ordinary current/old/older logs continue recording games correctly.

The prior host file-backend test used a plain stdio adapter and missed Android
translation. The new regression executes the actual production JNI initialization
and `IOFile::Open`, reproduces the old relocation, then verifies the new descriptor
handoff in raw and provider modes, append preservation, descriptor lifetime/closure
and missing-file failure. The fix opens the private path directly and passes an
owned duplicate through existing `fd://` support. It changes no renderer policy.

The normal log picker returns only current, previous and older text logs. An
explicit **Crash reports** action separately shows meaningful crashes, recorded
errors and unresolved interruptions; ordinary launch bundles are removed from the
UI. Available Android exit-only evidence can still be exported when 0.1.14 left an
empty log. Unknown game metadata is explained rather than presented as a healthy
empty session. Crash reports use ZIP only to preserve binary OS traces and metadata.

Empty idle launch records without failure/trace/incident evidence are reclaimed.
Only OS-confirmed clean idle backups are limited to two; crashes, errors, active
or unresolved records are not aged out. Current evidence cannot be deleted. This
keeps crash retention while fixing the rejected UI rather than hiding all failures.
Android UI behavior and descriptor-backed journal health still need device testing.

Per-game settings retain the by-2.0 requirement and both approved entry points.
0.1.15 is successor two after the accepted 0.1.13 review only once published;
default next full review remains after 0.1.17 evidence.

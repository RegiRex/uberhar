# Uberhar 0.1.12 evidence and 0.1.13 decision

<!-- AstraPro: Keep observed results, owner annotations, source defects and hypotheses separate. -->

## Scope and provenance

Reviewed source parent: `366ca36e88ec3dba7258fa270b343a5e93f9f6be` (0.1.12).
New evidence: `uberhar_log_10_1_2012_SASRT_LSMDM_FEA_LCUTCB.txt`.
Seven completed sessions, four games, including a brief LEGO restart; not seven
independent performance benchmarks. The prior 0.1.10 Dark Moon/Sonic file is a
separate comparison source. Its earlier Dark Moon-only prefix is not extra data.
Raw owner logs and game binaries are not added to the public repository.

Owner annotations: Sonic turbo off/on marks the race start and lap three. Dark
Moon retains ghost corruption and one observed lower-half black frame. FEA was
accelerated. LEGO seemed excellent in Native and Combo, with Native subjectively
better. The recorded effective modes do NOT establish a Native LEGO comparison:
all three LEGO sessions are Automatic/Combo. Do not override those records with
intended settings or guess why the discrepancy occurred.

## Method

Use observed time excluding the logger's recognized pauses. Separate normal and
400%-requested bands; mixed transition windows do not belong in either fixed-cap
window statistic. Use frame-end mode/resolution ranges rather than the final
setting alone. Unknown frames, mixed settings and mixed speed limits are excluded
from fixed-window groups, with raw records retained. Weighted window means are
weighted by observed duration; medians/minima describe approximately five-second
window means, NOT frame-time percentiles, display FPS or physical input latency.
Phase labels remain unknown unless owner annotations identify the interval.

Sonic's race marker lies within log time 76.936429–81.947429 s and its lap-three
marker within 167.115776–172.119410 s. Source lines 443–445 and 1019–1021 identify
the two mixed windows. Startup contains a separate mixed window at lines 210–212.
Five recorded speed transitions agree with initial turbo plus the two off/on
pairs. The exact instants are not retained; do not interpolate a precise lap
boundary. The end-of-run windows are not an explicit finish-line marker.

## Results

| Sample | Effective mode/resolution | Recorded accelerated-band mean | Duration |
|---|---|---:|---:|
| Sonic | Combo 2x | 123.092% | 152.090 s |
| FEA, first run | Combo 2x | 343.189% | 37.198 s |
| FEA, second run | Native 2x | 323.573% | 35.450 s |
| LEGO, first long run | Combo 2x | 392.559% | 141.975 s |
| LEGO, final run | Combo, changes 2x to 4x | 335.578% | 171.291 s |

Dark Moon Combo 2x at normal 100% averages 13.753% over 306.971 s. The brief LEGO
restart provides only 4.510 s of normal observation, not a sustained test.
Band references in the new log: 1343–1344, 3393, 3900–3901, 4391–4392,
5548–5549, 5761 and 7082–7083. Mode references: 4534, 5544, 5688, 5757, 5896,
5954 and 7078. All long LEGO runs actually promote GPU work.

### Sonic: preserve the gain, do not invent another measured gain

The prior 0.1.10 whole accelerated-band mean was 125.054%, versus 123.092% now.
Those are unequal scene exposures and turbo-marker patterns, not a controlled
regression or improvement. Current all-400 window median is 95.126%, compared
with the earlier 97.480%; the owner feeling better pacing is retained as a
qualitative observation. The strong previously observed advantage over Native
remains a reason to protect Combo, not proof 0.1.12 increased throughput again.

Owner-marked complete windows: pre-race 337.486% over 15.006 s; race-start marker
to lap-three marker 91.547% over 85.168 s; after lap-three marker 105.344% over
40.077 s. Combined complete post-race-start windows average 95.962%, range
78.564–122.092%. Fourteen of those 25 window means are below 95%. Thus neither
the 123% aggregate nor a fast menu establishes a full-speed floor in gameplay.
The last segment includes recovery after entering lap three, not just its first
slow moment. A few seconds of marker uncertainty remain excluded, not reassigned.

Sonic promotes 4,095,482 batches and builds 52 optional pipelines with 483.719 ms
aggregate driver time (77.154 ms maximum). Its optional fragment modules total
65.413 ms. CPU recovery still executes 156,202,112 vertex invocations across
8,390,909 batches. Pipeline creation is distinct from GPU execution; no direct
GPU timing is inferred from host clocks.

### Dark Moon: correction encountered; visual qualification still fails

The 0.1.12 pending-uniform counter is 7,719 in Dark Moon and zero in the other
six sessions. It proves the pending state was revisited, not that 7,719 frames
were wrong. The retained dirty-flag fix does not explain or eliminate all of the
owner's remaining ghost corruption or the reported black half-frame.

Dark Moon promotes 495,846 GPU batches, with 856,109,331 input slots. Its remaining
CPU vertex stage accounts for 39.436 s of host wall time. It builds 87 optional
GPU pipelines with 117.200 s aggregate driver creation (3.188 s maximum), while
75 optional fragment modules take only 455.604 ms total. Generic waiting is
494.997 ms. These are different quantities; worker time cannot be subtracted
from wall time as if it were a serialized foreground stall. This log contains no
GPU result comparison that attributes the ghost shapes to a shader stage.

The prior matched 0.1.10 pair was Combo 13.287%, Native 33.123%. The current run
is slightly longer in emulated frames (2,526 versus 2,457), so 13.753% is not a
cleanly isolated speedup. No new Native Dark Moon run is present. Both corruption
and sustained performance remain unresolved, and no new crash trace is supplied.
References: new log 3357–3396; prior matched file 1992–2043 and 2985–3036.

### FEA: meaningful capacity, not sustained 400%

Both modes remain at 2x. Their six complete fixed-400 windows average 350.664%
Combo and 328.125% Native, with minima 249.184% and 243.610%. These short runs have
different read/frame counts and no battle-only annotation, so the mode difference
is descriptive, not an isolated causal percentage. Both are below the eventual
400% floor. Native is an active performance target rather than only a control.

### LEGO: distinguish resolution, effective mode and camera workload

The 26 qualifying Combo 2x windows average 395.131% over 130.078 s, minimum
353.723%. This is a much longer high-headroom sample than the earlier 30-second
coast-facing result. It does not independently identify the camera direction of
each window. Never use a speed threshold to label new footage as coastal: faster
rendering could lift an unchanged inland scene through that threshold.

The 32 pure-4x windows average 339.423% over 160.187 s, minimum 168.804%.
The resolution-transition window is explicitly excluded. This supports strong
sampled 4x headroom but NOT a stable 200% floor. It is not Native versus Combo:
all three LEGO sessions identify Automatic, including the brief restart.

Both long runs reach the 256 optional-pipeline cap, but record only 780 and
1,024 capped requests respectively. Do not equate reaching the allocation cap
with all draws being blocked, or enlarge it without a measured benefit and
lifetime/memory validation. Specialized fragments remain below their own cap.

## Source review and selected implementation

### Native semantic-plan reuse

`PicaCore::LoadVertices` reconstructs `NativeVertexPlan` for every no-GS CPU
batch. Its constructor consumes only attribute-register mapping, output mask,
active output-map words and their counts. Add one value-only cache entry whose
complete key covers those constructor inputs. It caches no guest pointers,
vertex payloads, defaults, program constants or shader execution results. Live
input/output conversion and every submitted vertex remain unchanged. Restored
registers are compared, not assumed clean from a dirty-bit flag. Invalid mappings
retain the existing recovery behavior. The same optimization benefits Native
and CPU recovery under Combo; it does not force more work onto the GPU.

This reduces repeated per-batch setup, NOT the cost of every PICA instruction.
How many batches reuse a plan and whether total speed improves are device checks.
Do not represent a host equivalence test as a percentage performance gain.

### Fixed-attribute reservation correction

The production `SetupFixedAttribs` writer reserves 16 vec4 slots but writes a
leading default vector plus as many as sixteen distinct fixed registers. The
maximum is 17 * 16 = 272 bytes, versus its old 256-byte reservation. Reserve all
17 vectors. A harness extracting the production writer with real PICA/layout
types reproduces one failing maximum case in 131,072 cases before correction
and no failures afterward. The allocator is modeled, so this is a proven writer
capacity contract defect, not proof the Thor crashed from this exact layout.

New counters record maximum committed fixed bytes and occurrences exceeding
the former reservation. No current log establishes that Dark Moon used it.
Sonic GPU eligibility, geometry correction, shader arithmetic, fragment policy,
cache limits, CPU clocks and all inherited driver workarounds remain unchanged.

### Targeted diagnostics rather than a global debug flood

For each admitted optional GPU pipeline (bounded to 256 per cache generation),
capture immutable VS/FS/GS identities, stage-presence mask, topology, layout counts,
attachment formats and final driver-build duration/failure. The worker never
reads changing PICA registers. Grouping these records can show whether costly
builds share a vertex/geometry program or vary with fragment/state combinations;
it does not measure execution cost or isolate one stage's compilation duration.

Every 4,096 prepared draws checks a five-second rate limit for a synchronized
host-state snapshot: title, route, optional pipeline identity, guest topology,
vertex count, lighting/shadow state, clip/flip, viewport/scissor, framebuffer
addresses, output mask and input count. These are prepared states, not captured
GPU output, not a confirmed bad frame, and not every draw. The clock is not
called per vertex. No readback, extra GPU wait or render-pass split is introduced.
One final fixed-capacity summary and the existing cadence's Native plan hit/build
counts complete the instrumentation. A one-frame visual event may still fall
between samples; further bounded GPU output comparison/replay may be needed.

## Parallel development and rejected shortcuts

Accept the owner's request to optimize Native as a continuing first-class path.
A useful alternative must preserve output and be evaluated on CPU-heavy and
GPU-heavy scenes separately. Native still uses host GPU rasterization/fragment
processing; comparable CPU performance does not guarantee the same FPS on a
weaker GPU. The Duo and other drivers remain untested, not presumed qualified.

Keep the successful Sonic route. Do not globally revert Combo, disable quaternion
fix-up, lift unsupported strip/guest-GS restrictions, drop failed draws, overclock
the guest, or enlarge caches merely to make the performance numbers look better.
Diagnostic next questions: is the old fixed-layout bound ever reached; do slow
pipeline groups share VS/GS/state; do black-frame-adjacent host states disagree;
does Native reuse materially reduce measured batch setup. If corruption remains,
use stage output/descriptor-lifetime investigation rather than another unproven
throughput redesign. This change does not claim to fix the reported crash.

## Validation and release boundary

Full host probe passes, including new fixed writer and Native cache tests. New
ASan/UBSan checks pass. Six integration C++ syntax checks pass, with inherited
warnings retained. No new local GPU image result is claimed because this patch
changes host transport/preparation/diagnostics, not shader arithmetic. Existing
full Android, shader/pixel, package/signing and publication gates remain required.
Thor visual parity and performance are untested for this candidate. State commit,
workflow and APK status only from verified publication results.

This is a focused early source/correctness review, not a reset of the full 0.1.9
architecture-review anchor. After 0.1.13 evidence the scheduled full review remains
due (allowed 0.1.12–0.1.14, before 0.1.15). All current performance milestones,
latency work and 0.8 resolution-aware effects remain open within their own scope.

Source SHA-256: `a92f09bdfa2296f0ca2e2530e5b4257beea08496f590dad3da1970dbe62df401`. Machine-readable window groups and marker partitions accompany the conversation review package.

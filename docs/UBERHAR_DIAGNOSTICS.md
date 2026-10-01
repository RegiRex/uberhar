<!-- AstraEH: Bounded troubleshooting and removal map for the hybrid renderer. -->
# Renderer diagnostics, schema 17

Every Uberhar renderer log call has an adjacent **`AstraEH Log Line`** comment.
Find it with `rg -n 'AstraEH Log Line' src`. These markers identify diagnostic
output; deleting them must not remove completion signaling, failure recovery,
admission limits or state validation. They do not attribute inherited Azahar
logging to this fork. Android session/title/export records remain functional
parts of log export, rather than temporary shader debugging.

The startup record identifies effective switches, compiler worker count,
`diagnostics=17`, `dynamic_fragment=true`, `bridge_policy=ready_only`,
`fallback_abi=7`, `push_bytes=128`, `runtime_lighting_luts=true`,
`runtime_lighting_enables=true`, `runtime_light_loop=true`, `host_pipeline_identity=true` and
`bridge_assembly=isolated_lists_strips_fans`.
`cpu_bridge` is false when hybrid is off or forced fallback is on, even if the
saved bridge preference is on.

| Record | What it measures | Limit / interpretation |
| --- | --- | --- |
| `Uberhar capabilities` | Advertised and queried GPL/shader-object support, GPL fast-linking property, advertised/enabled dynamic state and border support, push-constant capacity | Once per device creation. GPL and shader objects remain **disabled**; support is evidence for future work. |
| `Uberhar progress` / `totals` | Draws, fallback use, scheduler waits, build/deferred/failure totals | Progress at most once per five seconds, clock checked once per 4,096 draws; final totals after workers drain. |
| `Uberhar CPU bridge` | Eligible pending observations, bridge selections, warming, ineligible draws, validated binds, mismatches, CPU batches/vertices and preparation time | Same aggregate cadence. `ineligible_draws` includes warm draws; it is not a miss count. |
| `Uberhar bridge coverage` | Admission reasons and seen/selected topologies | Same aggregate cadence. Counts only enabled-bridge, supported-fragment observations; includes warm draws. Input and expanded output each capped at 4,096 vertices. |
| `Uberhar wait histogram` | All scheduler wait events grouped by duration | Eight disjoint bins: below 1 ms; 1–16.666667; 16.666667–50; 50–100; 100–250; 250–500; 500–1000; at least 1000 ms. Upper bounds exclusive; counts are not cumulative. |
| `Uberhar worst wait` | Eight longest waits of at least 50 ms over the entire session | At most eight records, emitted on normal shutdown. Time is relative to renderer construction; draw is an ordinal, not a frame ID. The short retention lock is taken only after slow waits. |
| `Uberhar execution cache` | Guest pipeline records versus host pipelines; vertex configurations versus modules; live VS code generation | Same aggregate cadence, current title's existing maps. No additional per-draw logging or unbounded census. |
| `Uberhar build` | Worker queue, stage dependency and driver-call totals per route | Parallel wall times overlap. Never sum them as gameplay stalls. |
| `Uberhar variant census` | Raw/canonical fragment and candidate pipeline states | At most 2,048 keys per dimension. Includes proposed CPU bridge states in 0.0.8, so its pipeline census is not directly comparable to 0.0.7. |
| `Uberhar fallback utility` | Completed successful fallback pipelines used or unused, unused driver-call time | Same aggregate cadence; a currently unused pipeline may serve later. |
| `Uberhar pipeline build` | One slow specialization/fallback's queue, shader dependencies and driver call | First 20 slow builds per route. |
| `Uberhar fallback build` | Fragment frontend size/time, pipeline wall time, `cpu_vertex` route | First 20 successful fallback builds. Zero shader bytes means module reuse. |
| `Uberhar lighting family` | Canonical global lighting flags, texture kind, procedural flags and module sizes; count/slot operations are runtime in 0.0.15 | First 32 successfully built families per title, including disk hits. Pipeline variants do not consume this cap. |
| `Uberhar slow pipeline wait` | Actual command-worker wait and selected compatible pipeline | First 20 waits of at least 50 ms. Phase 0 queued, 1 dependencies, 2 driver, 3 complete, 4 failed. |
| `Uberhar fallback failure` | Family/key, CPU-vertex route and exception detail | First eight experimental failures; totals continue counting. Failed objects cannot be selected. |
| `Uberhar CPU bridge mismatch` | Expected versus prepared execution key | First four mismatches. Always use the existing accurate software path instead of binding incompatible state. |

Renderer/wait/build counters accumulate for the renderer lifetime. Family and
fallback maps/candidate census reset on title changes; execution-cache records
describe the current title. Prefer one game per test. Progress snapshots can
straddle concurrent completions; final shutdown totals are more consistent.
Pipeline keys contain process-local module identity and must not be compared
across launches or treated as transferable cache IDs.

CPU time is measured from the ready bridge decision to entry into CPU-triangle
submission. It includes the existing CPU shader JIT on a first encounter, vertex
loading, execution and assembly; it excludes the subsequent Vulkan draw setup.
`selected` can differ from `draws`/`batches` for empty output or a rejected key.
Bridge triangle assembly is isolated only for a prepared replacement of an
already-acceleratable draw; ordinary software assembly retains its state.
The bridge emits no per-vertex or per-draw text. VS codegen timing covers only
live GLSL translation misses, not driver compilation or startup reconstruction.

## How to attribute the next result

- Check coverage first: seen/selected strip/fan counts and separate rejection
  reasons should explain whether the bridge is reaching the pending work.
- Lower scheduler waits with useful bridge draws and modest CPU time supports
  the bridge strategy. High CPU maxima or worse gameplay with bridge on calls
  for the bridge-off comparison and adjustment of its admission limits.
- Fewer families but unchanged host pipelines points to remaining native state
  or vertex/interface variation. Lower host-pipeline counts than guest records
  demonstrates runtime sharing; it does not alone prove faster play.
- High unused fallback driver cost means speculative work still competes with
  demanded work. Only one unfinished generic build is admitted in normal mode.
- Any mismatch or fallback failure needs investigation before expanding coverage.
  Zero renderer errors does not establish visual correctness; screenshots and
  observed geometry/lighting changes are still valuable.

Existing upstream compiler diagnostics can print source after a compilation
failure. The caps above apply to Uberhar's own records, not every upstream log
category. Keep ordinary logging filters; verbose shader tracing is unnecessary.

## Virtual PICA profiles (0.0.10 and later)

<!-- AstraEH: New counters separate coverage, CPU interpretation and moved compilation waits. -->

`Uberhar_TestMode` is 0 custom, 1 native, 2 compute, 3 automatic. All profiles use
`vertex_engine=cpu`; the vertex-stage record names the actual engine. 0.0.10
used the reference interpreter, while 0.0.11 requests the cached CPU JIT. Neither
is GPU vertex interpretation. Existing custom-mode counters retain their meaning.

- `Uberhar virtual native`: primary generic/recovery draw counts, foreground generic
  wait count/total/maximum, and `complete_ready_bank=false`. Uses the existing
  bounded progress/final reporting cadence. These waits are distinct from the old
  scheduler wait counter; do not conclude that zero scheduler waits means zero
  shader stalls. Different thread intervals may overlap.
- `Uberhar virtual vertices totals`: a shutdown summary of batches, submitted
  input vertices, full vertex-stage wall time and worst batch. This includes
  setup/memory synchronization, not just shader arithmetic. Immediate-mode vertex
  processing is outside this batch counter. 0.0.11 also adds bounded progress windows.
- `Uberhar compute prepared`: one startup record with actual coverage, pipeline
  count, timing availability and mode. Initialization failures/capability rejection
  have one explicit diagnostic; native recovery remains active.
- `Uberhar virtual routes totals`: one shutdown record of state/geometry/format
  rejection, admitted rectangles, actual native/compute draw counts, compute pixel
  count and sampled GPU times. Zero compute draws is an honest coverage result,
  not proof that the compute kernel was fast or slow.

Automatic selection has eight area buckets and at most 32 in-flight query pairs.
It initially samples to learn both routes, then throttles ordinary observations
and resamples exploration draws. Results are read only after GPU completion,
without `WAIT_BIT`. CPU compilation is prepared before recording the native
measurement. Timings are draw intervals and may split render passes; they are not
whole-frame benchmarks or a guarantee that the chosen route is always faster.

The compute kernel has one pre-game pipeline creation and no per-draw compilation.
The wider native route remains on demand. A later version must implement/validate
broader coverage before claiming a complete compilation-free first playthrough.

## CPU cost and module reuse (0.0.11)

<!-- AstraEH: Schema-8 fields, limits and attribution boundaries. -->

- `Uberhar virtual vertices progress/totals` includes cumulative `shader_invocations`
  and indexed `cache_hits`; inputs can exceed invocations. Geometry index-input
  paths bypass the VS count. Immediate-mode vertices remain outside these batch
  totals. `engine=cpu_jit` identifies the actual CPU JIT; unsupported host architectures
  can report `cpu_interpreter`. Window fields describe elapsed wall time, CPU-stage
  wall time, submitted inputs and actual shader runs since the preceding snapshot.
  Progress is checked once per 4,096 batches, uses the existing batch-end clock read,
  and is emitted at most once per five seconds plus final shutdown.
- `Uberhar CPU JIT build` records only the first eight real program/swizzle cache
  misses; `totals` reports all program builds, compilation wall time and maximum.
  This is CPU machine-code generation, not GPU driver compilation. Its time is
  already inside the encompassing CPU stage: do not add it to that stage total.
- `Uberhar generic modules` reports persistent generic SPIR-V hits, compile misses,
  rejected entries and write failures, at the existing renderer progress cadence.
  A hit still creates a Vulkan module and may require a driver pipeline. Misses
  include disk-cache-disabled builds. The first four rejected entries and first
  four failed writes get detail; totals continue. Files use the title prefix in
  `vulkan/pipeline`, so the existing Android clear operation removes them too.
  Each file is limited to a 40-byte header plus 1 MiB of SPIR-V. The existing
  128-family admission cap bounds new entries per title/renderer; old compiler
  fingerprints remain until cache clearing. Length/framing/checksum/fingerprint
  validation detects stale or damaged cache data; it is not SPIR-V semantic validation.
- `Uberhar compute blockers` gives non-exclusive state rejection counts once at
  shutdown: shadow, color writes, depth test/write, stencil, alpha, clip, scissor,
  culling, fog and blending. One draw may increment several, so never sum these
  as a draw count. Geometry and format rejection still happen after state admission.
  These counters explain eligibility; they do not establish which expansion is cheap.
- `Uberhar execution origins` reports startup host objects/guest records, live new
  host objects, live new objects for an already-known guest key, and fragment-module
  count. Startup objects can still be building. Known-record misses flag possible
  identity changes; new guest keys do not by themselves establish different visible
  gameplay. Counts describe the current title and use the existing bounded cadence.

Generic foreground waits and scheduler waits can overlap on different threads.
Do not sum them as elapsed hitch time. Human absence is not identified by these
records: elapsed windows may include loading, menus, pauses or idle gameplay.

## Run pacing and Android context (0.0.12)

<!-- AstraEH: Overlay-independent frame and health evidence with explicit blind spots. -->

`Uberhar run` gives a process-local session number, title and settings snapshot.
Session numbers restart with the process; combine them with existing version/date/title
records. `bridge_requested` is a setting; renderer records establish effective use.
`Uberhar run end` records lifetime, observed frames and pause/exclusion totals even
for a run that exits before its first sampled interval. Lifetime includes loading
and pauses and must not be used as active gameplay time.

`Uberhar frames window` uses independent fixed counters: overlay polling cannot
reset or disable it. There is one additional monotonic clock read per system frame,
no per-frame text, GPU readback or unbounded history. Emit after five seconds of
accumulated valid intervals, with up to 32 early pause-boundary summaries, a final
partial window and session totals on normal shutdown. Final window and totals
overlap; never sum them with prior windows as independent samples.

- `frames`: observed system-frame intervals; the initial anchor is excluded.
- `game_submissions` / `game_fps`: guest GSP submissions across those intervals,
  not Android presentations or necessarily distinct visible images.
- `observed_wall_ms`: sum of valid frame-end intervals, including frame limiting;
  explicit pause intervals and their crossing frames are excluded.
- `speed_percent`: guest-time advance / observed wall time; 100 means real time.
- `work_ms` / `max_work_ms`: existing begin/end system-frame wall timing, excluding
  the subsequent explicit frame limiter. Can contain GPU or compilation waiting;
  this is not CPU utilization, arithmetic-only execution or a GPU timestamp.
- `interval_bins`: eight disjoint bins bounded by 16.666667, 33.333334, 50, 100,
  250, 500 and 1000 ms. Upper bounds are exclusive. No exact percentile claim.
- `frame_limit` / `temporary_limit`, mode and resolution describe settings at
  report time. Resolution 0 means automatic. `Uberhar frame limits` additionally
  records sampled minimum/maximum cap and temporary-cap interval count, identifying
  mixed fast-forward windows. In-game fast forward is not directly detected.
- `pauses` / `paused_ms`, `excluded` and `clock_discontinuities` are cumulative run
  counters. An ongoing pause is not yet included in `paused_ms` at its begin record.

`Uberhar pause` logs the first 32 begin/end pairs of waits actually reached by the
Android core loop, including frontend pause and modal core errors. Menu savestate
save/load paths mark waits too; returning on an error still closes the marker.
Counters retain all such waits. Sampling re-anchors afterward to avoid counting a
pause as a hitch or a forward state load as excessive emulation speed. Guest-time
reversal or a non-increasing supplied clock also re-anchors automatically. Arbitrary
external state injection or debugger suspensions are not identified as pauses.

`Uberhar worst frame` retains eight largest intervals of at least 50 ms in fixed
storage, reported at shutdown. `frame` is the observed interval ordinal, `end_ms`
is relative to the `PerfStats` run origin, and `work_ms` is that frame's work interval.
Correlate these with renderer log timestamps, but do not add them to shader waits.
`display_timing=false` excludes panel timing and screen-pair synchronization claims.
Abnormal termination can lose final totals and retained worst-event records.

`Uberhar device health` samples off the UI/render threads, at most every 30 seconds
across activity recreation, while the game view is resumed. It records model/API,
Android monotonic uptime, thermal status/headroom where available, battery temperature
and level, plugged-in code, power saver, available system memory/low-memory flag and
native allocated heap bytes. Unknown sensors remain `unknown`; status 0 can also
occur with incomplete thermal reporting. Battery temperature is not CPU/GPU
temperature; allocated native heap is not RSS or graphics memory. No added
permissions, network uploads or root/debug access are needed.

No log can anticipate every future optimization question. Screenshots and user
observations remain necessary for visual correctness. Pausing when stepping away
gives a real marker; continued rendering alone cannot establish human presence.

## Runtime lighting family reduction (0.0.13)

<!-- AstraEH: Same-workload evidence separates key reduction from unequal replays. -->

`Uberhar variant census` adds `previous_lighting_families`, `lighting_shapes`,
`proctex_shapes` and `lighting_abi=3`. The previous count restores original lighting
into the otherwise canonical family, reproducing 0.0.12's family key for these
same observations; no old shader is compiled. Compare it with `canonical_families`.
Lighting/procedural counts hash the remaining structural configurations. Counts are
independent dimensions, not a Cartesian product. They do not measure saved time.

All 14 census sets remain renderer-thread-owned, title-scoped and capped at 2,048
entries each. `capped=true` makes counts lower bounds. There are no new per-draw
log records or timers. Actual compilation, foreground waits, frame pacing and warm
speed remain the performance evidence. The startup record identifies schema 10 and
the 120-byte ABI so old and new shader layouts cannot be mistaken for one another.

<!-- AstraEH: Device evidence exposed an unchanged core header, not a stale APK. -->
In 0.0.13, the separate `Uberhar run` record in `PerfStats` still says
`diagnostics=9`; the renderer startup correctly says `diagnostics=10` and ABI 3.
Use the build revision plus renderer ABI to identify that build. 0.0.14 fixes
the ambiguity with `frame_diagnostics=1`; the stale 0.0.13 label does not alter
recorded timings. See the
[0.0.13 device analysis](UBERHAR_LOG_ANALYSIS_0.0.13.md).


## Runtime lighting enables/configurations (0.0.14)

<!-- AstraEH: Separate same-state key evidence, bounded structural detail and timing. -->

`alpha13_families` reproduces 0.0.13 family keys for the current observations,
without compiling the old variant. Compare with `canonical_families` to measure
this alpha's reduction. `previous_lighting_families` retains its **0.0.12** meaning.
There are now 15 title-scoped census sets, each capped at 2,048; saturation sets
`capped=true` and makes affected counts lower bounds. The new key adds one small
copy/hash and set lookup per generic request, with no new per-draw timer or text.
These are key counts, not compiled-module counts or saved milliseconds.

The startup renderer schema is 11. The generator owns `DynamicTevAbiVersion=4`
and the 120-byte layout; startup/census values read that constant. LUT byte bit 7
now includes register enable and configuration support. Private input 6 means
constant-zero index for CP outside Config7. Source/build fingerprints prevent
reuse of incompatible generic modules; game shader/transferable formats do not change.

`Uberhar lighting family` records the first 32 successfully built families per
title, on the serial generic worker after timing the build. `ordinal` is a detail
counter, not a draw/frame number. `flags` is canonical `LightConfig.raw`;
`light_flags_lo/hi` pack the eight canonical 16-bit `Light.raw` slot words in
ascending order (four per 64-bit word, least-significant slot first).
`light_count`, `texture0`, `proctex`, GLSL/SPIR-V byte sizes and the family hash
help identify remaining variants. A family may be unlit. Runtime LUT/source
values are intentionally absent; these records contain no uniforms or shader code.
Texture/procedural fields summarize only part of a family key, not its full identity.
The counter resets after the worker drains on title changes. Beyond 32, aggregate
census/build/cache counters continue, but structural detail is incomplete.

`Uberhar run` now uses `frame_diagnostics=1`, the independent frame-accounting
schema. Its timing semantics and five-second/normal-shutdown reporting are unchanged.
Use the build revision for the app version, renderer `diagnostics` for renderer
schema, and `frame_diagnostics` for frame schema. This avoids implying that one
subsystem's diagnostic version identifies all others.


## Compact lighting and fast-forward evidence (0.0.15)

<!-- AstraEH: Bounded runtime-loop coverage and fixed-memory throughput attribution. -->

Renderer schema 12 identifies ABI 5/128 bytes and `runtime_light_loop=true`. Two
additional uint words at offsets 120/124 hold four 7-bit per-slot operation controls
each. Bits 0–6 are directional, two-sided diffuse, distance attenuation, spotlight,
geometry0, geometry1 and shadow enable. The low word's high nibble holds the active
count 0–8; out-of-range counts use specialized recovery. The light-source/two-sided
LUT word is retained separately to preserve the inherited physical/slot indexing
convention. Both generic lighting and TEV loops carry `DontUnroll` in tested SPIR-V;
this is a compiler-input hint, not a guarantee about every driver's final machine code.

The census adds `alpha14_families`, applying 0.0.14 rules to the same observations;
compare it with `canonical_families`. `alpha13_families` and
`previous_lighting_families` retain 0.0.13 and 0.0.12 meanings. Sixteen sets are each
capped at 2048 and reset with title maps. `seen_light_counts` is a hexadecimal
bitmask: bitN means a lighting-enabled candidate with N active slots was seen,
including zero. It does not count draws or measure GPU cost. It adds one OR at a
generic request, without a timer or log line per draw.

Family details retain their 32-per-title cap. They now report `lighting_enabled`,
`light_count_mode=runtime`, `key_flags`, texture/procedural summaries and module
sizes. The old 0.0.14 `light_count`/`light_flags_lo/hi` fields are no longer appropriate
because they are not static shader choices. Runtime control values and guest code
remain absent from these diagnostics.

Frame schema 2 adds `Uberhar speed band` at normal shutdown, at most three lines:
`normal` (positive cap at or below 100%), `fast` (above 100%) and `uncapped` (zero).
Each records its own active interval duration, frames/submissions, achieved guest
speed, FPS, work, worst interval, cap min/max and temporary-limit frame count.
400% is a requested ceiling; `speed_percent` is the measured guest/host-time ratio.
Different fast caps can share a band, so compare only matching `limit_min/max`;
existing five-second windows still identify scene changes and mixed limits.

The new counters occupy fixed memory and reuse the frame timestamps already
collected. Explicit pauses/clock/state discontinuities retain their exclusion
rules. When sampled cap or temporary-limit status changes, the crossing interval
remains in overall totals but is omitted from speed bands; `transition_intervals`
counts those intervals once (the same total appears in each band line). Thus band
frames plus transition intervals equal overall observed frames. No band log is
written per frame or per limit change. Band totals do not reset with five-second
windows. Neither work nor frame intervals are pure GPU execution/display timing.


## Native vertex transport (0.1.0)

<!-- AstraEH: Fixed-size route counts and sparse samples accompany final-vertex reuse. -->

`Uberhar native vertices progress/totals: schema=1` accompanies each existing
vertex-stage report (at most once per five seconds plus normal shutdown). Counts
are cumulative per PicaCore/title lifetime and are host-only, outside save states:

- `batches/inputs`: draws/inputs admitted to prepared no-GS transport.
- `conversions/conversion_reuses`: actual output conversions and final-vertex FIFO
  hits. Their sum equals admitted inputs. Reuse does not omit assembly or triangles.
- `mapping_fallbacks`: insufficient packed VS outputs for the semantic map;
  `geometry_fallbacks`: GS/other non-No mode; `debug_fallbacks`: debugger attached.
  These count batches routed to the established path, not compilation failures.
  A draw with both debugger and GS counts under debugger, not twice. Custom is
  excluded. Invalid index addresses return before these counters, as before.
- `sample_misses/sample_hits`: selected vertices measured on misses/hits. One
  rotating index is chosen per 128 eligible batches, at most 8192 samples per title.
  Empty batches do not produce samples. The deterministic rotation is not a random,
  vertex-weighted survey and may miss rare states; there is no implicit extrapolation.
- `sample_input_ms`: loader plus prepared input-register transport, on sampled misses.
  `sample_shader_ms`: CPU shader engine Run only. `sample_output_ms`: prepared
  semantic conversion/color clamping. All three have `sample_misses` as denominator.
- `sample_submit_ms`: primitive assembly/triangle sink on every sampled hit or miss;
  the denominator is both sample counts combined. Includes CPU work reached through
  AddTriangle, not GPU execution. Timing boundaries add small clock costs; substage
  nanoseconds must not be presented as exact isolated machine instruction costs.

Ordinary batches compile without per-vertex clocks. A sampled miss uses six clock
reads (four partition boundaries plus two submission boundaries); a sampled hit
uses two. Cache lookup/insertion, batch setup, plan construction and loop overhead
are outside these sparse samples. Existing `stage_wall_ms` measures the broader
CPU vertex stage and remains the main performance comparison. Samples stop at the
cap; later broad frame/stage windows still continue. No uniforms, guest shader
programs, per-vertex records or new Android health polling are logged.

Renderer schema 12, frame schema 2 and fragment ABI 5 stay unchanged because the
fragment/transport-to-GPU path is retained. Native vertex schema 1 identifies this
new CPU diagnostic record. The exact build revision continues to identify releases.


## Sustained sampling and machine context (0.1.1)

<!-- AstraEH: Fix sample exhaustion demonstrated by Sonic's 13-million-batch run. -->

Native vertex **schema 2** replaces the schema-1 batch stride/lifetime cap with
`sample_period_ms=50`. Admission reuses the existing draw-start timestamp; a
nonempty, eligible no-GS draw can take one rotating-input sample per 50 ms, with
no catch-up after a pause. Fixed counters continue for the title's lifetime.
Ordinary batches retain the clock-free per-vertex path. This provides late-lap
coverage instead of exhausting a budget during early high-draw-rate scenes.

Existing `sample_misses/hits/input_ms/shader_ms/output_ms/submit_ms` keep their raw
sample meanings. New fields describe those same selected **whole batches**:

| Field | Meaning |
| --- | --- |
| `sample_batches` | Completed admitted batches; equals sample_misses + sample_hits. |
| `sample_batch_inputs` | All inputs in those batches, not just the selected input. |
| `sample_batch_invocations` | Actual CPU VS calls in those batches, excluding FIFO hits. |
| `sample_setup_ms` | Draw-start through loader/JIT/geometry/map setup before the loop. |
| `sample_vertex_ms` | Entire CPU vertex stage for sampled batches; includes setup. |
| `sample_draw_ms` | Host DrawTriangles call after vertex work, including renderer preparation and any waits it reaches. |
| `sample_draw_max_ms` | Largest such sampled DrawTriangles duration. |

These are **overlapping, non-extrapolated CPU wall times**, not GPU timestamps.
Do not add setup to vertex time or multiply samples into claimed whole-run savings.
Draw time can overlap existing compilation/wait measurements. Samples are selected
by time and then rotated within a draw, not a uniform survey of all input vertices.
Three additional clocks are read for a sampled batch (setup boundary plus draw
start/end), beyond existing sparse vertex clocks and existing broad stage clocks.
The progress record moves after DrawTriangles so denominators and durations match.
It keeps the existing 4096-batch/five-second gate plus shutdown; one clock is read
at that reporting gate, not on every unsampled draw. No per-draw log is added.

`Uberhar machine: schema=1` records one startup context in Core::System::Load:
actual title ID, `model_new`, configured core count, independent CPU clock percent,
system/app/New3DS memory modes, title-requested 804/L2 bits and `kernel_804_flag`.
It appears **before** `Uberhar run`; associate it with the following matching title
launch. The kernel flag is guest-visible metadata, not evidence of a measured
804 MHz rate. The L2 request is not a simulation of physical L2 behavior. No model,
clock, memory allocation or guest service return value is changed by the record.

The existing FRD GetFriendKeyList warning now logs the first four requests and
subsequent powers of two, preserving all replies. Each warning includes cumulative
requests and suppressed count; `Uberhar friend queries totals` records final counts
at module destruction. These host-only counters are not serialized into guest save
states and contain no friend identifiers. In the supplied 13,141-request workload,
this emits 15 warnings plus one total instead of 13,141 warnings. Those calls were
outside the sustained race interval, so suppression is not claimed as its fix.

Renderer schema 12, frame schema 2 and fragment ABI 5 are retained. This iteration
adds visibility without changing shader math, model policy or graphics profiles.


## Loading evidence and prepared TEV stages (0.1.2)

<!-- AstraEH: Phase labels must never transform uncertain gameplay stalls into loading successes. -->

Frame schema **3**, renderer schema **13**, fragment ABI **6**. The 128-byte
transport is retained. Existing caches are fingerprinted by shader source and
build revision, so old module instructions cannot interpret the new plan bits.
The actual preset, model, API and CPU clocks retain their existing semantics.

- **Confirmed frontend startup:** `Uberhar loading` has begin/end records around
  Android's non-interactive cache progress screen, with session and elapsed wall
  time. It does not claim that later game loading has ended. This measures existing
  startup work; the release does not introduce speculative precompilation.
- **Explicit phase:** the Android in-game **Test phase** chooser sets Automatic
  (unknown), Loading or Gameplay, session-only. User Loading/Gameplay is a supplied
  annotation, not emulator-verified ground truth. Switch back when the screen
  changes. New runs reset the marker. It changes no rendering, scheduling, cache,
  pause, settings-file or guest behavior. Frontend startup temporarily overrides it.
- **Automatic evidence:** aggregate guest file-read request count and requested
  bytes (including cache hits and failed requests) plus game submissions. No
  filenames, contents or screenshots are captured. At least 1 MiB requested and
  at most 10 submissions/s gives `loading_candidate`; other reads give
  `guest_read_activity`; otherwise `presenting_unknown` or `no_submissions`.
  These are clues only. Games can stream while playing, load in memory without
  file reads or animate loading at full speed. `automatic_confirmed=false` is
  always explicit. Low FPS, lack of input, shader stalls and static pictures
  alone never confirm loading. No stall is removed from overall totals.
- **Frame bands:** five fixed lifetime buckets: `unknown`, `startup_loading`,
  `user_loading`, `user_gameplay`, `mixed`. They sum to all valid observed frame
  intervals. Generation checks mark an interval crossing a phase change as mixed,
  even if phases change away and back between observations. Explicit pauses still
  break/re-anchor timing. No phase duration is an Android display measurement.
- **Frame records:** `Uberhar activity` accompanies existing window/pause/final
  records, with counts in that fixed phase order, reads/bytes and evidence.
  `Uberhar worst frame` retains the same eight events plus phase, read and
  submission context. An additional `Uberhar phase worst` bank retains four
  events per phase, so long loading stalls cannot evict all gameplay evidence.
  These overlap global worst events; they are not additional intervals.
  Read evidence is for the complete frame-end interval, not
  a screenshot or physical storage-time measurement. Final partial windows overlap
  totals as before. Up to 64 sampled `Uberhar phase` transition details are logged;
  all intervals still reach lifetime bands after that cap.
- **Generic waits:** `Uberhar generic wait` records the first 64 foreground waits
  at least 16.666667 ms, with phase, session and guest-read/submission changes
  during the wait. Five `Uberhar compile phase` buckets at the normal progress
  cadence retain all generic waits (count/total/max), successful generic builds
  and build wall time. Compiler counters are atomic and progress snapshots can
  straddle completion; shutdown after drain is authoritative. Build and wait
  time overlap and must not be added. Renderer buckets are renderer-lifetime,
  like existing virtual-native totals. Unknown and mixed waits remain explicit.
- **Build details:** existing first-20 generic build records and first-eight CPU
  JIT build records gain actual-build phase and session context. Phase at worker
  execution is used, rather than at queue admission. This is not a new complete
  trace of every specialized pipeline or CPU shader. Original specialized wait
  totals remain and can be correlated by timestamp with activity records.
- **TEV plan:** `Uberhar TEV plan` has seven bins (0..6) each for loop length and
  active stage count on selected primary-generic draws. Histograms sum to generic
  draw count. They measure draw coverage, not pixel-weighted speedup. Buffer-mask
  bits 0..7 retain delayed writes; bits 8..10 give loop end; bits 16..21 mark
  non-passthrough stages. Intermediate pass stages still advance the combiner
  buffer; only trailing pass stages whose buffer has no consumer disappear.

Counters are fixed-size. Each guest read adds two relaxed atomic increments;
GSP submissions add one. Frame reporting uses existing frame timestamps. No
per-fragment, per-vertex or per-read text, GPU readback, screenshot capture or
network upload is introduced. A pause/read/phase boundary may produce mixed or
unknown evidence; never promote it to confirmed gameplay or loading. Logs do not
identify a particular move, stage transition or exact button-to-panel latency.

The next prewarming step requires actual reusable pipeline descriptions and a
safe time budget. First-run unknown game shaders cannot be reconstructed merely
from recognizing a loading screen. Phase estimates in this release must not
trigger speculative compilation or silently permit dropped draws.

## Prepared TEV operands (0.1.3)

<!-- AstraEH: Identify changed instruction semantics without changing timing attribution. -->

Renderer schema **14** reports `prepared_tev_operands=true` and fragment ABI **7**,
still 128 bytes. Each prepared stage carries the existing source field positions,
with stage-zero Previous resolved once from the original third operand. Color
selectors encode `(component << 1) | invert`: RGBA components 0..3, RGB 4 and
constant zero 5. Alpha uses components 0..3; scales carry literal 1/2/4 values.
Original guest-register storage and specialized shader generation are unchanged.

The existing TEV plan histograms retain their exact meanings and bounds.
Frame schema 3, native vertex schema 2, phase fields and speed bands are unchanged.
No additional periodic record or counter is added. All unmarked in-game phases
remain Unknown. A sleep/resume surface-recreation hitch can remain in active
frame timing after the explicit pause boundary; do not attribute it to shader
compilation solely because it appears among worst frames. Temporary 400%-cap
segments must be separated using speed bands when assessing normal performance.

Prepared operands add per-draw CPU work while reducing per-fragment decoding.
Draw histograms are not pixel-weighted savings, host call time is not GPU time,
and successful host validation is not a device performance claim. ABI/source
fingerprinting prevents old module instructions from interpreting new selectors.

## Automatic run settings and cache context (0.1.4)

<!-- AstraEH: Separate sampled settings, disk presence and observed compatible reuse. -->

Renderer schema **15**, settings/cache schema **1**; frame schema 3 and fragment
ABI 7 are unchanged. Existing resolution and speed-band fields keep their meanings.

- `Uberhar settings` identifies session, title, exact build, named mode/API,
  resolution setting (zero explicitly means Auto), CPU clock/JIT, shader/cache
  controls, base/turbo limits, presentation settings, texture controls and render
  timing settings. It logs launch, the first 32 observed changes and final shutdown.
  Comparison occurs at existing five-second frame windows and on resume, never per
  draw. `sample_scope=instant` means the settings at that observation: changes
  between samples can be missed, and a setting may require a renderer restart.
  It does not report the physical scanout rate or pretend Auto is a fixed scale.
- `Uberhar cache start` inventories the current title's application Vulkan files
  before loading or writing, separately counting generic SPIR-V, device-specific
  driver data and specialized records. `inventory_ms` measures added metadata work.
  Directory scans stop at 8192 entries; no source or shader contents are logged.
  Android paths use the same native translation as FileUtil. Unsupported virtual
  filesystem mappings remain Unknown rather than falsely empty.
- `state=empty_files` means no nonzero bytes were found in the active application
  namespaces under a complete scan. `present_files` means bytes exist, with
  compatibility unverified. `disabled`, `unknown` and `not_observed` are distinct.
  A header-only driver file can still be present. Bypassed specialized records are
  shown but do not affect the Native active-cache label. This never inventories
  or certifies a GPU driver's private internal cache.
- `Uberhar cache use` follows the existing renderer progress cadence and shutdown.
  Per-title-load baselines separate actual generic module hits/misses from the
  existing renderer-lifetime totals. `cold_encountered` means misses and no hits;
  `warm_encountered` means hits and no misses; `mixed` means both; `not_observed`
  means no lookup yet. Disabled disk caching is explicit. These labels apply only
  to encountered generic modules, not all possible shaders or pipelines.
- `driver_load` distinguishes missing/open/read/directory errors, invalid data,
  data provided to the driver, failed creation and empty-cache fallback. Existing
  recovery/deletion behavior is unchanged. `provided_to_driver` is not a verified
  driver hit. Startup and current disk-enable settings are both preserved.

A build revision participates in generic-module fingerprints. Consequently an
upgrade can report present files and cold encountered modules even when fragment
math is unchanged. Do not equate module misses with a confirmed manual cache clear.
Do not add file-inventory time, compile duration and foreground waits together;
some intervals overlap. No cache is cleared or warmed by these diagnostics.

Normal-performance analysis excludes temporary fast-forward and mixed speed-limit
transition intervals unless the owner explicitly requested a speed test. Preserve
raw totals/bands; do not relabel fast-forward as loading automatically. Game-scene
notes remain useful because neither settings nor cache records identify a partner
attack, tutorial, respawn or physical input latency.


## Exact fallback preparation reuse (0.1.5)

<!-- AstraEH: Expose the optimization's work and cost without per-draw logging. -->

`Uberhar preparation progress/totals` uses schema 1, scoped to the current title.
`requests = hits + misses`; hits reuse a complete family/census result after
byte-exact raw FS and full static pipeline equality. A 256-entry direct-mapped
cache bounds memory; `evictions` count occupied-slot replacements, never skipped
rendering. `invalidations` count actual profile/extended-dynamic configuration
changes after initial configuration. Title resets clear entries and these counters
with the census sets. `capacity` and `storage_bytes` report actual compiled storage
(213392 bytes on the tested 64-bit host).

On a hit, skip three family canonicalizations, historical/dimension hashing and
sixteen census-set updates. On a miss, observe the same candidate dimensions
before admission limits, exactly as before. Evictions can cause repeat census
work; they cannot discard a required pipeline or prevent a new observation.
The existing 2048-key census cap and lower-bound flag keep their meaning.

Timing samples one request in 1024, starting with the first. Two host-clock reads
cover cache lookup/miss preparation plus census, excluding real pipeline-map
lookup, shader compilation, queued commands and GPU execution. `hit_samples`,
`hit_sample_ns`, `miss_samples`, `miss_sample_ns` retain separate populations.
Zero samples are unknown, not zero cost. Deterministic sampling can align with
repeating draw patterns; do not extrapolate it as an unbiased total or treat it
as shader compilation time. Text uses the existing five-second/final cadence.

Only pure preparation is cached. Per-draw push constants, dynamic state, shader
module identities, completion/failure, cache admission, rendering keys and draw
order retain their existing paths. Profile configuration is captured at renderer
construction and refreshed by `SetAccurateMul`, the only later profile mutator.
A future profile mutator must call `Configure` too. No new persistent cache format,
loading classifier, prewarming policy or owner setting is introduced.


## Exact fragment push-constant reuse (0.1.6)

<!-- AstraEH: Measure commands actually recorded by the selected worker path. -->

`Uberhar push constants progress/totals` uses schema 1 and `scope=current_title`.
It counts selected generic draws on the command worker, after pending/failed
pipeline selection is resolved. `requests` equals `uploads + reuses` during
normal completed execution. `uploads` counts full 128-byte fragment pushes;
`reuses` counts calls omitted after complete byte equality with valid prior state.
`invalidations` counts dirty-state notifications, including initially undefined
state; it need not equal uploads. `saved_bytes = reuses * bytes_per_upload` is
logical API payload avoided, not measured GPU/bus traffic or time saved.

Progress reports are queued at the existing renderer five-second cadence. They
read worker-owned counters in command order, without per-draw atomics, mutexes,
clock reads or waits for reporting. They may lag neighboring renderer-thread
records. Final reporting reads directly after the existing worker drain. Title
reset also drains before clearing values/counters. `timing=not_measured` is
intentional: command reduction does not by itself quantify performance gain.

The shadow value belongs to the command worker and one immutable rasterizer
layout/range. Each command retains its own 128-byte value copy. Compare only
after the actual generic/specialized winner is selected. New command buffers
invalidate through existing AllDirty handling. Graphics blits/filters and both
presentation paths explicitly dirty fragment constants; compute-only writes use
separate shader stages. Existing compute-rectangle Pipeline invalidation also
forces a conservative refresh. A dirty command invalidates the shadow even if
specialization wins, so a later generic draw cannot use utility-written bytes.

The production `ExactPushConstants` helper commits its shadow only after issuing
the upload. No partial update, family-based approximation, descriptor suppression,
draw omission or shader arithmetic change is introduced. A future foreign fragment
push writer must mark `FragmentConstants` dirty when enqueuing its work.

Vulkan begins each command buffer with undefined push values, and requires the
last pushed ranges to be compatible with the consuming pipeline. Pipeline and
descriptor binds alone do not overwrite those values. See the official
[push-constant command reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPushConstants.html)
and [lifetime examples](https://docs.vulkan.org/guide/latest/push_constants.html).

## 0.1.7 / diagnostics 18 (AstraEH)

`Uberhar vertex input {progress|totals}` reports one Prepare result per entered
validated no-GS Native batch: ready, missing_attribute, unconfigured, address_wrap,
short_mapping. `mapped_attributes` counts attempted bounded span maps, including
failed preparations. `fused_vertices` / `legacy_vertices` count actual shader
invocations in that branch, not FIFO hits and not geometry/debug fallback work.
All maps are retained only within a batch; no cross-draw memory cache is implied.

`Uberhar recovery reasons {progress|totals}` assigns each specialized experimental
route exactly one first reason: spirv_incompatible, shadow2d, gas_fog, custom_user,
light_count, lighting_lut, add_signed, generic_failed, generic_unavailable,
disabled_or_other. Reasons follow the existing conservative support order.
Their sum must equal `virtual native ... recovery_draws` at a final snapshot.
These are draw-weighted counts, not pixel coverage or GPU-time shares.
`transport_prepared` / `transport_skipped` count support checks that did/did not
construct generic runtime state. They are not byte-upload counts and need not
equal command-worker requests when state is reused between draws.

Both logs reuse the existing five-second/final report cadence; no per-vertex
clock, atomic update, shader dump or unbounded per-draw logging is added. Existing
frame schema3, ABI7, per-speed bands, app-cache inventory and unknown driver-cache
state are unchanged. GPU sample count zero means unmeasured, not zero GPU cost.


## AstraPro: diagnostics 19 / 0.1.8

<!-- AstraPro: These counters describe actual work and bounded observations, not GPU time or model settings. -->

- `Uberhar index bounds`: retries, scanned indices, rescued batches/invocations,
  and live-index escapes. One scan only after ShortMapping/AddressWrap, maximum
  262144 indices. Existing five-second/final PICA reporting gate; no per-index
  clocks or atomics. `vertex input` fused/legacy counters reflect the actual
  transport, including live-index escape fallback.
- `Uberhar PICA routes`: CPU batches, accepted GPU batches/input indices,
  eligible attempts and Automatic topology census [list,strip,fan,shader,other].
  Same cadence. GPU input indices are NOT GPU shader invocation count. The
  existing `virtual vertices` stage measures only CPU-handled work; do not use
  its decreasing input denominator to claim the game itself became simpler.
- `Uberhar ready GPU vertices`: renderer-lifetime attempts, accepted pipeline
  selections, dependency misses, deferred/capped/failed attempts, key mismatches,
  resident pipelines and actual driver build totals. Pipeline residency belongs
  to the current cache; counters can span a cache switch. Five-second/final
  existing renderer cadence. 256 pipeline slots, one pending driver build.
- `Uberhar GPU shader admission`: optional VS deferral/cap, GS cap, failed
  shader completions. At most 128 configs per optional VS and fixed GS cache;
  one optional VS compile pending. First eight shader failure details per title.
  Cached failed translation suppresses per-draw reattempt/logging. These are
  admission limits, not proof a game fits within the budget.
- Settings add `ready_gpu_vertex_policy` independently of `hw_vertex`. The latter
  remains the inherited user hardware-shader setting; Automatic can promote
  eligible draws with it false. Actual PICA/bind counters establish use.
- Speculative `UseFragmentShader` may prepare state before returning to CPU. Thus
  transport-prepared/skipped counts can include abandoned preparation. Actual
  generic/recovery draw counts are updated at binding and must not be inferred
  from preparation count alone. Specialized recovery reasons remain exclusive
  first-failing checks, not a complete inventory of every feature on that draw.
- Device health schema 2 retains the 30-second IO-worker cap and reads at most
  32 fixed sysfs nodes, each up to 24 characters. `elapsed_ms` includes deep sleep;
  `uptime_ms` now uses true uptime (the previous label held elapsed realtime).
  CPU frequencies are instant current/max kHz readings, or unknown. Vendor mode
  is explicitly unobserved, GPU clock unknown; zero/unavailable heap is unknown.
  Battery temperature is not SoC temperature. Thermal NONE with unknown headroom
  cannot rule out throttling. No root, new permissions, fan/power/clock writes.

No per-frame string/clock sampling is added to the renderer. The optional GPU
shader compilation worker shares the existing driver cache: absence of an
explicit wait is not absence of driver/compiler contention. First-use CPU
pipeline waits and existing route limitations remain visible and unchanged.


## 0.1.9 — AstraPro: independent-list admission and reporting

Renderer diagnostic revision 20; settings policy `independent_lists_v2` only in Automatic.
`Uberhar GPU admission` records exclusive first-failing draw-level decisions:
`eligible_list`, `eligible_shader_list`, `disabled`, `debugger`, `geometry`,
`assembly`, `winding`, `topology`, `small`, `large`, `incomplete`.
Only Automatic attempts are classified, so disabled is normally zero.
`topology` also includes a mismatch between registers and persistent assembler.
`winding` applies only to Shader: List does not consume that flag.
`selected_topologies=[List,Strip,Fan,Shader,Unknown]` counts actually submitted
GPU draws, not requests. Eligibility does not establish dependency/driver readiness.
These counts exclude immediate-mode draws, are host-only, and reset with PICA.

The existing 4096-batch/five-second progress gate now counts completed CPU and
GPU batches. CPU-stage totals remain CPU-only and cannot be compared across
modes without GPU coverage. No GPU invocation or GPU-time estimate is invented.
Old log parsers can still use existing PICA route counters; new fields are additive.
A nonzero `eligible_shader_list` with zero selected Shader draws requires checking
upload/dependency/pipeline state; it is not itself proof of improved performance.


## 0.1.10 candidate diagnostics — AstraPro

`ready_gpu_fragment_policy=specialized_ready_v1` identifies the Combo policy, not a measured fast path. `force_tev=false` is intentional in the effective Combo preset; Native and Compute retain true. The CPU first-use route remains generic.

`Uberhar ready GPU fragments`: shader-cache-object/title-scoped lookup counters, optional modules/builds/failures, frontend/module creation duration, demand replacements and fixed budgets. Lookup counts include preflight and final preparation, so they are not exclusive draw counts. Compilation wall time can overlap other work; do not add it to foreground stalls or GPU time. `storage=memory_only`, `generator=glsl_specialized`. Exact-profile collisions may defer rather than reuse an incompatible entry.

`fragment_preflight_deferred` in ready GPU totals: optional attempts stopped before vertex analysis/uploads because the fragment module was not usable. It does not measure uploaded bytes or claim a speed gain. Final preparation revalidates state after synchronization.

`optimized_gpu_draws` in virtual-native totals: covered draws that used ready specialized GPU fragments, distinct from generic draws and mandatory specialized recovery. `transport_bypassed_gpu`: optional GPU-only attempts that did not decode unused generic transport; it can exceed selected draws because pipeline readiness may still fail. A complete CPU retry prepares transport again. Existing preparation/recovery counts remain separate.

`Uberhar GPU host attempts`: one sample per 1,024 actual optional GPU attempts. Success/fallback sample counts and summed sample spans, plus sampled maximum. Includes host acceleration preparation/submission and possible waits/backpressure, excludes the later CPU retry, and is NOT GPU execution or utilization. Fixed-period sampling may alias repeated workloads; do not extrapolate the sum to a whole-frame GPU budget.

Frame records append `mode_min`, `mode_max`, `resolution_min`, `resolution_max`, `render_context_changes`, `render_unknown_frames`, `render_context_source=frame_end_settings`. Ranges include both sampled endpoints of each valid interval. Pauses re-anchor; window reset preserves the preceding endpoint; automatic scale zero is not unknown. A pure fixed-setting comparison requires equal appropriate ranges, zero changes and zero unknown frames in addition to a constant speed limit. These are setting observations, not physical output dimensions; a change-and-revert between samples can still be invisible. Raw overall and speed-band timing are retained. Older records without these fields still need conservative first-observed-change exclusion.

All new log calls remain on existing bounded five-second/final reporting paths or the eight-detail optional-fragment error cap. No per-draw text is introduced. New host/context sampling has a cost that requires device validation, not a claimed zero-overhead guarantee.


<!-- AstraPro: Crash preservation is distinct from rendering or a fatal backtrace. -->
## 0.1.11 current / old / older

`log/azahar_log.older.txt` preserves the previous `azahar_log.old.txt` when a
nonempty current log is rotated at logger initialization. Current and old stay
the only in-app export choices. A crash log is normally old after one restart,
older after two, and evicted by the third rotation. Copy it out promptly.

No useful current data means no rotation. Rotation failure keeps the source and
opens it in append mode; the current file can then contain multiple session
headers. Retained bytes count toward the existing 100 MiB write cap. A startup
failure note goes to stderr while normal logging is suppressed during creation.

The first consumed record and periodic consumed timestamps trigger stdio flushes;
this is not fsync, queue draining on a signal or guaranteed final crash context.
The older-file addition cannot reconstruct already-lost 0.1.10 playtime.

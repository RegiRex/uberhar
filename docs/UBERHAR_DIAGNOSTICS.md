<!-- AstraEH: Bounded troubleshooting and removal map for the hybrid renderer. -->
# Renderer diagnostics, schema 8

Every Uberhar renderer log call has an adjacent **`AstraEH Log Line`** comment.
Find it with `rg -n 'AstraEH Log Line' src`. These markers identify diagnostic
output; deleting them must not remove completion signaling, failure recovery,
admission limits or state validation. They do not attribute inherited Azahar
logging to this fork. Android session/title/export records remain functional
parts of log export, rather than temporary shader debugging.

The startup record identifies effective switches, compiler worker count,
`diagnostics=8`, `dynamic_fragment=true`, `bridge_policy=ready_only`,
`fallback_abi=2`, `push_bytes=108`, `host_pipeline_identity=true` and
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

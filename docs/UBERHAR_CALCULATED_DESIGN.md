# Calculated rendering: coverage and architecture decisions

<!-- CodexAstraLocal: The owner requires direct isolation before further performance conclusions; keep the installed baseline and next implementation separate. -->
## Next experiment: isolated Calculated execution

The owner has authorized .33/.34 and requires Calculated to stop using
Native/graphics fallback in .33. Unsupported guest draws will be explicitly
counted as unrendered instead of delegated. The exact compute admission checks
remain necessary; disabling recovery does not implement missing rasterization.
Current CPU vertex preparation and presentation remain shared infrastructure,
so this isolates guest raster work, not a new independent vertex processor.

The direct experiment uses scratch targets and supported original inputs
to compare actual compute commands with a separate graphics reference, checking
pixels before comparing GPU times. Setup, barriers/dispatch and sampling scope
must remain explicit. Synthetic reference draws must never inflate guest compute
coverage or act as fallback in a strict title test. A blank or incomplete title
cannot establish performance versus complete Native output.

The strict route and backend refusal/terminal reload handling pass focused
control-flow and failure tests. The merged native build, CTest and full host probe
pass. The scratch benchmark's focused owner/command checks, original-input pixel
comparisons and required Vulkan module validation pass, alongside the merged
compute gate. Android delivery and device measurements remain pending.
The .32 results below remain the baseline; the prior review-and-stop instruction
is superseded only for this two-build batch and its final audits.

<!-- CodexAstraLocal: Document explicit admission and evidence limits so a synthetic graphics reference cannot be mistaken for guest fallback or complete-title performance. -->
## Optional supported-work benchmark

Android with raw-filesystem access can consume one explicit request in the
application config directory, named `uberhar_compute_benchmark.json`. It is
disabled by default and admitted only in isolated Calculated mode. The JSON
object has exactly these five fields; choose a fresh identifier for an authorized
experiment, using at most 48 ASCII letters, digits, underscores or hyphens:

```json
{"schema":1,"enabled":true,"trigger":"renderer_startup","title_id":"0004000000055F00","benchmark_id":"example_once"}
```

Before compiling or allocating scratch resources, the owner exclusively creates
`compute-benchmark-<id>.claimed.json` in the application dump directory. An
existing claim prevents replay, including after an interrupted run. A separately
created `.result.json` reports completion or a bounded failure. Remove the request
after collecting the experiment. Other modes and unsupported frontends return
before request IO or resource construction.

Four cases cover 256×256 and 800×480 targets with full RGBA and partial R/B writes.
Each uses eight alternating graphics/compute pairs, with 32 operations per route:
two overlapping rectangles repeated sixteen times. The graphics reference uses
the original 88-byte CPU hardware vertices and the production shader generator;
compute uses the production rectangle command recorder. Both must match an
independent whole-image integer oracle, including untouched and masked bytes.
These graphics commands are private reference work and never service guest draws.

GPU intervals include route barriers and either one graphics pass or 32 compute
dispatches. Reset, upload and readback lie outside those intervals. Host enqueue,
validation and observed readback latency are separate fields. Ratios require all
64 routes to complete with correct, useful output. Repeated idempotent writes
cannot prove that every individual operation executed; command tests check their
ordering and counts separately.

Only one pair may be in flight. The 120-second soft deadline stops further
admission, without cancelling submitted GPU work or bounding driver compilation.
Pending resources remain owned until the existing renderer drain. Allocation
reports include VMA sizes; the roughly 7.34 MiB raw allocation total excludes
driver overhead. Synthetic timings cannot establish general 3D coverage or
complete Dark Moon throughput.

## Delivered .32 baseline and longer-term options

<!-- CodexAstraLocal: Keep the requested independent-rendering goal distinct from the bounded .32 candidate and unimplemented architecture alternatives. -->

The goal is correct first-run rendering with no saved application shader cache,
no skipped draws and sustained 99% or better normal speed. Qualify 2x before 4x.
Calculated mode currently retains CPU vertex execution and a complete graphics
fallback. Its 0.1.31 and 0.1.32 Dark Moon controls computed zero rectangles
or pixels. A mode name does not establish independent rendering.

The integrated 0.1.32 candidate expands exact solid replacement to nonzero partial
RGBA masks and source-alpha endpoint blends. It passed all release gates and
was installed through a verified data-preserving update. Original-vertex host
pixel comparisons and native builds pass. The single Calculated control prepared
its compute pipeline but admitted zero states; all 6,646,279 lifecycle attempts
fell back before geometry or format checks. This has not added Dark Moon coverage. The prior .31
ColorWrite/Blend population is concentrated in menus and the opening boundary. The census does
not prove that those draws have supported geometry, TEV or blend factors.
The .32 logs retain at least 1,126,850 six-vertex raw-1026 draws across delivered
lifecycle detail rows, yet no effective state admission. Before further pixel
widening, collect a bounded exact failed-state cohort and replay the unchanged
classifier. Geometry relaxation cannot fix a rejection occurring before geometry.

<!-- CodexAstraLocal: Tie the next architecture decision to the direct measured bracket without misidentifying wall time as CPU or GPU utilization. -->

The .31 Calculated opening has 166.568 seconds inside `LoadVertices` over 198.917
seconds of separate complete vertex-report windows: 83.737% inclusive wall time.
This bracket includes preparation, loading, shader execution, FIFO, output and
assembly, plus scheduling delays. It ends before `DrawTriangles`. It is neither
exclusive shader CPU time nor a GPU timing measurement. A pixel-only extension
cannot claim to remove this work. The .32 control similarly averaged 20.957%
normal speed, with 83.665% of separate vertex-report wall time in this bracket.
These are observations with different report and scene populations, not a causal
version comparison.

| Architecture | Work moved | Required proof | Decision condition |
| --- | --- | --- | --- |
| Exact rectangle extension | A proved pixel operation after CPU vertices | Original vertices, exact replacement, coverage, preserved byte lanes, image ownership and ordering | Actual useful computed draws and pixels; report zero coverage honestly |
| Compute vertices feeding hardware fragments | Potentially vertex input, shader execution, output and triangle preparation | Shader carry/FIFO semantics, exact output, quaternion adjustment, device-resident transport and visibility | Supported title coverage and lower complete-path cost without output differences |
| Ordered tiled compute renderer | Pixel coverage, interpolation, texture sampling, depth/stencil and blending, plus vertices if implemented | Complete ordered operations, bounded tile lists, derivatives, resource aliases and recovery before visible writes | Exact pixels/state and measured end-to-end benefit on the target |

<!-- CodexAstraLocal: Define independent vertex execution before attempting parallelism that could break persistent guest shader state. -->

The next substantial prototype should start with the real per-draw shader unit
and indexed FIFO contract. Execution occurs on ordered cache misses; temporary,
address and output state can carry between invocations. A restricted private
independence certificate rejected all 48 retained packets at unsupported control
flow, so it supplies no permission to parallelize these draws. This is a proof
limitation, not evidence that each title shader necessarily depends on carry.

A sequential compute implementation can provide a correctness baseline. Useful
parallel execution then requires a sound independence proof or a separately
validated carry algorithm. Keep output on the GPU, initially using the existing
88-byte hardware-vertex interface and original triangle order. Per-triangle
quaternion sign adjustment cannot be replaced by sharing one adjusted vertex
across every triangle. Compute vertices with hardware fragments would remove
some CPU fallback while still using graphics rasterization; describe that
hybrid architecture explicitly.

<!-- CodexAstraLocal: Document the extra pixel-renderer contracts rather than presenting a stage-name change as general compute support. -->

A full compute rasterizer needs stable primitive order and exclusive tile/pixel
ownership for depth, stencil, alpha and destination-dependent blending. It must
construct interpolation gradients and texture LOD behavior explicitly. The
existing fragment generator's derivatives and fragment built-ins cannot simply
be used in a compute stage. Bounded-list overflow must recover before visible
writes or resume exactly; replay after partial blending would apply work twice.

The retained Adreno 740 query does not advertise compute derivatives, fragment
interlock or tile-shading extensions. Its 32 KiB compute shared-memory limit
must include attachment state, lists and scratch together. Advertised queue
count does not prove concurrent execution; the emulator currently creates one
graphics/compute queue. Existing shader float-control limits also prevent
assuming ordinary GLSL matches arbitrary guest arithmetic.

Ordered software rasterization is established engineering practice, but its
benchmarks cannot be transferred to this workload. See the
[Laine/Karras rasterization paper](https://research.nvidia.com/sites/default/files/pubs/2011-08_High-Performance-Software-Rasterization/laine2011hpg_paper.pdf)
and [paraLLEl-RDP source and design](https://github.com/Themaister/parallel-rdp).
The [Khronos compute derivative specification](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_compute_shader_derivatives.html)
defines a separate optional capability; generic compute support is insufficient.

<!-- CodexAstraLocal: Preserve known driver and visibility safeguards until a complete grouped path supplies an exercised replacement. -->

The current rectangle dispatcher ends graphics rendering and applies broad
incoming/outgoing image barriers around each dispatch. On this Qualcomm path,
an accumulated pass can also trigger the existing submission workaround. New
compute coverage may add transition and memory traffic costs. Zero-dispatch
controls have not measured those costs. Do not remove the workaround or narrow
barriers without actual dependency and driver validation.

A later grouped path could prepare immutable inputs for several compatible
draws and consume them in original order. Attachment/texture aliasing, guest
memory visibility, surface rebinding and incomplete snapshots end the group.
Measure input capture, upload, dispatch, rendering, synchronization and final
visibility together. A fast isolated kernel with expensive transport is not a
gameplay improvement. The [Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
and [tile-rendering guidance](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html)
support this dependency analysis, not a measured Thor speedup.

<!-- CodexAstraLocal: Make completion observable and retain the owner's .32 review-and-stop boundary. -->

Each prototype needs original-input output/state comparisons and deliberate
failing controls, followed by actual title coverage. Device evidence uses one
cold opening per method per build, with normal-100 lower-tail timing,
consecutive-frame moon and separate ghost review, and relevant memory readings.
Longer gameplay, 4x and the retained Fire Emblem Awakening beta baseline remain
unqualified. The [0.1.32 architecture review](UBERHAR_ARCHITECTURE_0.1.32.md)
and [independent cleanliness audit](UBERHAR_CLEANLINESS_0.1.32.md) are complete.
Development is paused for owner inspection; these architecture proposals do not
authorize automatic continuation or qualify 0.2.0.

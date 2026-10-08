# Calculated rendering: coverage and architecture decisions

<!-- CodexAstraLocal: Keep the requested independent-rendering goal distinct from the bounded .32 candidate and unimplemented architecture alternatives. -->

The goal is correct first-run rendering with no saved application shader cache,
no skipped draws and sustained 99% or better normal speed. Qualify 2x before 4x.
Calculated mode currently retains CPU vertex execution and a complete graphics
fallback. Its 0.1.31 Dark Moon control computed zero rectangles or pixels. A mode
name does not establish independent rendering.

The integrated 0.1.32 candidate expands exact solid replacement to nonzero partial
RGBA masks and source-alpha endpoint blends. It is not yet a delivered change.
Original-vertex host pixel comparisons and native builds pass; release gates
and device qualification remain required. The observed ColorWrite/Blend
population is concentrated in menus and the opening boundary. The census does
not prove that those draws have supported geometry, TEV or blend factors.

<!-- CodexAstraLocal: Tie the next architecture decision to the direct measured bracket without misidentifying wall time as CPU or GPU utilization. -->

The .31 Calculated opening has 166.568 seconds inside `LoadVertices` over 198.917
seconds of separate complete vertex-report windows: 83.737% inclusive wall time.
This bracket includes preparation, loading, shader execution, FIFO, output and
assembly, plus scheduling delays. It ends before `DrawTriangles`. It is neither
exclusive shader CPU time nor a GPU timing measurement. A pixel-only extension
cannot claim to remove this work.

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
unqualified. After 0.1.32 is built and tested, complete the systematic architecture
review and independent cleanliness/purpose-comment audit, then stop for owner
inspection. This design document neither closes that audit nor qualifies 0.2.0.

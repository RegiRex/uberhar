# Uberhar 0.1.45 architecture and purpose review

<!-- CodexAstraLocal: Separate the architect's source assessment from developer execution and later owner hardware qualification. -->
The focused source review accepts the bounded .45 architecture described below. The consistent native build and focused checks below passed, with a retained Calculated color-precision limitation stated explicitly. Release gates and owner hardware qualification remain separate. This is not a speedup, CPU-occupancy or experimental-renderer usability claim. The architect reviewed the changes independently of their implementers and did not repeat their execution checks.

## CPU placement and texture work

<!-- CodexAstraLocal: Explain the actual borrowed-memory and publication contract, including what the local worker budget does not guarantee. -->
[ParallelTextureDecoder](../src/video_core/rasterizer_cache/texture_decode_work.h) distributes existing CPU texture conversion across the existing sleeping-worker dispatcher. A cache owns one lazy pool. Its initial allowance is the permitted logical-CPU count minus two, with a minimum of one participant and the calling thread included. Other vertex/compiler pools can overlap: this neither reserves physical cores nor sets a global thread limit. Only actual ready tiles are dispatched; idle workers sleep.

The first path admits complete, compact, eight-pixel-aligned color/texture upload rectangles of at least 32,768 pixels, in 32-tile grains. It reuses the exact existing 8×8 Morton codec, including nibble and ETC formats and the bottom-up output convention. Mode, setting, shape, complete lengths and nonaliasing are checked before work starts. The guest-writer check runs only after cheap admission. Concurrent guest writers, small or unsupported layouts and depth/encoding paths keep the original serial codec.

The [cache owner](../src/video_core/rasterizer_cache/rasterizer_cache.h) retains the existing flush and maps source/staging storage before publication. Workers only read pinned input and write disjoint output pixels. They never map resources, submit GPU commands or validate cache entries. All borrowers join before hashing, upload, stream commit or validity publication. Failure drains the job before propagation; partial output is not replayed or published. No extra GPU conversion or readback is introduced.

Three independent saved controls appear immediately below CPU Vertices and require a game restart. Native full fragments OFF disables optional full specialization in Native while preserving static TEV and mandatory recovery. It leaves Combo's fragment policy independent. CPU prefix overlap OFF retains certified worker shading and the full join, without speculative readiness/append work. Parallel texture decode OFF selects the serial codec. Existing full-fragment and prefix defaults remain enabled; the new decoder defaults enabled only in its admitted Native/Combo Vulkan paths.

## Software and Calculated correctness

<!-- CodexAstraLocal: Keep proven numerical corrections distinct from unexplained retained title failures, and preserve full-batch state/error boundaries. -->
[Software rasterization](../src/video_core/renderer_software/sw_rasterizer.cpp) now uses top-left biases only for coverage. Covered attributes, reciprocal-W, Z/W depth and helper coordinates use unbiased signed-area weights. This corrects the demonstrated area-one zero-sum interpolation fault without dropping its covered pixel. Exact zero-area primitives return before interpolation; the same computed area serves winding and culling. Feedback ordering, row workers and invalid-coordinate recovery remain intact. The separate FEA NaN fault is not attributed to this geometry merely because both failed sampling.

[Calculated clipping](../src/video_core/shader/generator/glsl_compute_vertex_shader_gen.cpp) handles the narrow original homogeneous-zero case per triangle: the remaining two position vectors span at most a projective line, so no filled triangle is emitted. Other triangles and persistent semantic tails retain the existing whole-batch success contract. A W-zero vertex with nonzero XYZ is not discarded. Earlier-plane arithmetic, custom-distance interpolation and terminal refusal of unexplained nonprojectable geometry remain; no blanket epsilon or Native fallback is added.

Clipping failure details reuse the existing zero-filled completion header and readback. Numbered reasons distinguish plane distance, intersection, varying, eye/reciprocal and capacity failures. Original diagnostic positions are reconstructed from immutable semantic scratch only on failure, avoiding success-path diagnostic copies. Successful tail and 88-byte vertex transport remain unchanged.

## Purpose and consumer review

<!-- CodexAstraLocal: Document meaningful consumers and bounded diagnostic costs without turning object counts into performance attribution. -->
The comparison settings have native/Android/desktop persistence and immutable runtime consumers. The decoder's spans and pool lifetime have explicit owners; it adds no input snapshot, per-tile heap task or duplicate codec pass. Review removed an unconditional guest-writer query from disabled and serial upload paths. Existing dispatcher results supply actual owner/worker tile counts. Texture helpers receive their distinct thread name once through the existing startup hook; there is no per-task naming. `parallel_owner_tiles` explicitly counts calling-thread tiles only in jobs that also delivered worker work.

Texture cost logs are bounded and marked `CodexAstraLocal TEST-ONLY LOG` with removal conditions. They sample small calls and all large calls. Wall, owner and join durations are not worker CPU time, and serial/parallel populations are not automatically matched. Software's new operand context is exceptional-only, bounded, logged by the owner after worker drainage and has its own removal condition. Permanent Calculated errors explain why a batch was refused; they do not authorize retries or imply repaired title coverage.

The independent allocation follow-up reuses existing VMA, stream, pipeline and resource-pool counters. It does not add duplicate instrumentation or equate KGSL allocations with physical residency. Existing .44 reports show comparable application-managed storage across placement modes despite larger ordinary Combo KGSL; they narrow the next investigation to unaccounted driver/resource classes without proving a leak or a particular responsible object.

## Validation and remaining qualification

<!-- CodexAstraLocal: State only completed evidence reported by the responsible executor; pending execution remains explicit and is not replaced by source reasoning. -->
| Evidence available at this review | Scope |
| --- | --- |
| Texture conversion: 68 exact-output cases passed | Local codec/dispatch correctness. Complete-stage cost qualification and target pacing are separate. |
| Prefix: 32 cases on both transports, with overlap OFF control passed | Existing output/order/rollback contracts plus the independent comparison path. This is not new evidence of time saved. |
| Software actual-triangle checks passed: two cases, 38 assertions, no skips | Retained area-one shape, projective/depth/helper behavior, shared edges, exact degeneracy, a separate FEA-shaped finite/NaN case and the existing worker/alias contract. |
| Calculated focused geometry checks passed with byte-centered colors | Zero-origin/mixed batches, persistent state, visible W-zero geometry and specific terminal detail. The original valid midpoint-color case remains a strict-color failure, as detailed below. |
| Consistent native tests build and profile matrix passed | Reported by the integration owner. Release status is separate; no gate is waived by this document. |

The first Calculated comparison retained a valid constant color of `(0.25, 0.5, 0.75, 1)`. Its visible nonzero-XYZ/W-zero case differed from the independent graphics reference in 17 green bytes: computed output was 128, while the reference was 127. Coverage and complete D24/S8 output matched; the other four cases matched completely. That failed comparison is preserved. The subsequent geometry control uses byte-centered colors with the same exact-output comparison and no runtime or tolerance change. Its pass isolates the clipping/visibility contract; it does not resolve the original midpoint difference or establish universal color parity.

Cost measurements must compare equal decoded output and complete-stage work, including dispatch and join, with cold worker creation kept separate from reuse. Host or emulated-architecture timing is not a device result. Owner hardware acceptance must establish meaningful Software/Calculated scene progress and normal return; graceful error delivery alone is containment. Native/Combo comparisons must judge complete frames, output and pacing under comparable settings and conditions, rather than a prescribed busy-core percentage.

<!-- CodexAstraLocal: Preserve the latest owner-controlled testing boundary. -->
After the gated .45 build is submitted, the team checkpoints and stops. The owner performs hardware playthroughs and requests collection when wanted. No stock comparison, installation or autonomous device testing is part of this cycle.

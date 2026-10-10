# Uberhar 0.1.43 review

<!-- CodexAstraLocal: Record separate architecture and cleanliness review, bounded execution evidence and unresolved hardware acceptance. -->
## Calculated architecture and qualification

Calculated now has an integrated original-input Vulkan compute path. PICA array and immediate commands enter it before CPU guest shading. The GPU executes the guest vertex program, preserves draw-local FIFO/carry behavior, assembles persistent primitives, clips them, interpolates fragments, evaluates the shared production texture/TEV/lighting formulas and applies the ordered output merger. It does not recover through Native, CPU Software or a guest graphics draw. Existing texture-cache decoding, resource uploads and display presentation remain shared infrastructure. This is GPU compute rendering; the separately selectable CPU Software renderer has a different execution model.

The existing serialized PICA assembler remains the only persistent primitive-state owner. Capture copies live original semantic tails; GPU completion returns only touched slots. State is committed after complete producer success and renderer admission. List, Strip, Fan and Shader topology, array/immediate mixing and zero-emission batches retain their original ordering. Shader topology does not imply support for guest geometry programs. A failed batch terminates visibly and cannot be retried through another renderer.

| Stage | Integrated behavior and boundary |
| --- | --- |
| Inputs and guest vertices | Original compact/default/index inputs, uniforms, program and swizzles are captured with cache visibility. Compute reuses the production guest decompiler. Draws are bounded to 4,096 inputs and 16 MiB of captured raw bytes. |
| Assembly and clipping | Raw semantic tails survive between calls. Standard vertex sanitization, six homogeneous planes and the dynamic custom clip plane precede the GPU-owned 88-byte triangle stream. Opposite shared-edge traversal uses a common intersection orientation. |
| Fragment evaluation | Production TEV, lighting, fog, alpha/scissor, ordinary/projected/cube textures, typed shadow sampling and procedural LUT evaluation are callable from compute. Quad helper values supply explicit sampling derivatives. |
| Targets and ordering | Each invocation owns one pixel and processes primitives in order. Native RGBA8/RGB8 fallback, RGB565/RGB5A1/RGBA4 and D16/D24/D32 depth representations use their actual backing format; stencil lanes and packed transfer tails preserve adjacent pixels. Shadow updates preserve the production packed depth/shade arithmetic without a competing pixel writer. |
| Resource ownership | Cache images, typed views, feedback snapshots, descriptor publication, queue barriers and final-use ticks remain explicit. Packed images expand and repack on GPU. Current render targets require supported level-zero images and matching color/depth extents. |

The consistent main native build passed before the consolidated owner check. That executable linked the rebuilt main libraries without replacement or mixed-generation archives and exercised the actual Vulkan instance, scheduler, descriptors, texture runtime, surfaces, vertex producer and raster consumer. Its local execution used lavapipe; no Vulkan validation layer was installed.

- A retained 165-input cohort produced all 14,520 hardware-vertex bytes exactly against the independent A64 guest-engine reference. Both complete 65,536-byte color and canonical depth/stencil outputs matched the independent production-graphics reference. Omitting raster work or depth writes caused the intended mismatches; the collinear-plane control preserved both attachments.
- Persistent continuity passed 40 draws, 28 emitted triangles, 60 complete state comparisons and seven clipping checks, including sanitizer thresholds, reversed shared edges, a custom plane completing a retained tail and failed-producer state preservation.
- Twelve actual-image cases compared 75,928 bytes exactly, including untouched borders, odd packed tails, write masks, D16/D24/D32(S8), stencil, both order-sensitive shadow sequences, RGBA and integer-shadow feedback snapshots and procedural LUT descriptors. Two D32 fallback mappings were explicitly selected by the fixture; this does not establish automatic selection on a physical device.
- The updated source-derived census/strict-owner gate passed 97,309 checks and detected all 15 intended defects. The cached-resource gate retained its 68 cases, passed 334 checks and detected all three stamping defects.

The private owner executable does not invoke PicaCore's live guest-memory capture or automatic cache-alias discovery; those entry and ownership paths received source review. Independent architecture review found no new concrete blocker in the final producer, persistent commit, target/feedback views, shared-queue barriers, LUT stamps or bounded resource lifetimes. This source acceptance and the finite executable comparisons do not establish complete title output, Thor compatibility or performance.

Explicit remaining refusals include guest geometry execution, debugger/concurrent guest-memory writers, untranslated or invalid guest programs, invalid/nonfinite vertex domains, gas/custom fragment states and unsupported target representations. Numerical equivalence remains bounded by the exercised inputs and target compiler behavior. Feedback uses a draw-start snapshot. Synchronous producer status/tail readback, cold mandatory compilation, packed transfers and a per-pixel loop over the draw's triangles are real costs. No speed improvement or full-game readiness is claimed; device and title acceptance remain pending.

## Calculated cleanliness delta

The review covered the 23 Calculated implementation/build files, their direct consumers and the obsolete strict rectangle contract. Original-input interception, persistent assembly import, resource preparation and the callable evaluator have adjacent `CodexAstraLocal` purpose comments. Historical authorship markers remain. The old successful-omission branch and its unused reason counters were removed; the optional rectangle renderer remains in use by other presets.

The final cleanup renamed the required inactive storage-image descriptor, corrected stale CPU-bridge/omission comments, removed an unused include and restored the original unconditional trivial-shader calls after the deferred admission guard. That guard already rejects Calculated, so the removed second condition could never affect an admitted packet. The change has no interface-layout, descriptor, shader-string or accepted-route effect. Exact preimages and reversal evidence were retained; unaffected GPU checks were not repeated for these edits.

The three changed gate files now test terminal failure and schema-2 original-input accounting instead of obsolete successful omissions. Ordinary rectangle census, deferred ownership, complete-output distinctions and negative controls remain checked. The lifetime summary is permanent bounded diagnostics; no temporary production testing log or per-vertex logging was added. This is a focused delta review, not a claim that all inherited code or all rendering domains are fully audited.

## Software correction and cleanliness

<!-- CodexAstraLocal: Connect the actual startup failure to a bounded correction without claiming an unrecorded register tuple was reproduced. -->
The .42 Thor attempt stopped before the title menu with an invalid LCD span,
eight system frames and zero game submissions. Its original error did not retain
the failing register tuple. The correction matches existing GL/Vulkan display
behavior: scan out only the complete pixels inside the configured stride, up to
the width register. Final-row bounds, rotated pixel order, fill precedence and
terminal failure for nonempty unmapped/truncated storage remain intact. An empty
visible extent exposes the presenter's configured background.

Independent architecture review accepted this boundary. The developer verified
actual PICA startup registers/VRAM and format changes preceding stride changes:
two native LCD cases passed 135 assertions, and the existing standalone helper
passed 102 assertions. The final incremental native build reconfigured CMake and
passed. Existing triangle/worker tests were not repeated for this scanout-only
change. These checks do not establish that Dark Moon starts successfully on Thor.

Errors now include dimensions, stride, format, mapped size, screen and address
on the terminal path. There is no new per-frame logger or unused runtime result.
The Android dialog describes rendering errors accurately. Its unresponsive OK
button in the .42 failure remains unresolved; no speculative EGL lifecycle change
was made. Host source acceptance and device acceptance are separate milestones.

## Native/Combo queue and next measurement

<!-- CodexAstraLocal: Retain the shipped queue's observed work and resist treating higher worker counts as the throughput target. -->
The .42 queue change and its ordering review remain unchanged in .43. The
[device review](UBERHAR_TEST_REVIEW_0.1.42.md) now demonstrates auxiliary packets
and up to six participants, while the observed pool CPU share is small. Native
and Combo still average below 99%, and their slow windows coincide with high
aggregate GPU activity. The next optimization should target complete-frame cost,
using the owner's .42/.43 comparisons to identify regressions before extending
parallel work. More CPU load alone is not success. DDR bandwidth remains unavailable.

The source candidate is ready for the existing release gates. Keep .42 installed
for the owner; neither a pending .43 build nor these host checks qualify a beta.

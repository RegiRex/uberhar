# 0.1.52 scoped integration review

<!-- CodexAstraLocal: Record the independent architecture findings and changed-code review without claiming all inherited code or Thor throughput has been requalified. -->
The review covers shader admission after scene changes, static fragment emission,
render-pass reuse, exact copies, retired Android profiles and the separate .48
CPU-only comparison. Architecture review is separate from implementation and
focused test execution. Full release gates and hardware qualification follow.

The lighting reserve previously refused unlit demand before observing it when six
owners were present. Replacement-aware admission now keeps observations bounded
while allowing class-compatible stale owners to retire. Tests cover menu-to-race
demand, class reserves, wrong-victim controls, budgets and immutable pipeline
lifetimes. Compilation remains optional; a pending specialization retains a
complete rendering path.

Static locals are emitted only where fixed state permits first-use evaluation.
The unpruned emitter keeps independent sample and arithmetic controls active when
fragment liveness is off. This fixes control semantics without assuming which
setting will be fastest on the target GPU.

Contained render-area reuse requires the same graphics framebuffer/pass, LOAD
behavior, no shadow path and a requested area inside the active area. The caller
still sets each draw's actual scissor. Required feedback, ownership and submission
boundaries remain intact. Counts identify actual reuse, not saved milliseconds.

The exact RGBA8 copy path rejects aliases, conversion, custom textures, scaling,
flips and invalid extents. Review caught broader mip barriers in the reused copy
routine; eligibility is restricted to single-mip images so the optimization does
not widen that synchronization scope. A real Vulkan transfer fixture compares
translated copy/blit pixels with a CPU oracle.

Saved profile migration is shared across C++, Kotlin and Android configuration.
Software API0 migrates to Vulkan on Android; the reference engine and its internal
tests remain available without exposing a repackaged slow mode. The requested CPU
pixel switch is deferred because presentation changes do not address the measured
rasterization cost. High CPU demand is acceptable if it improves sustained speed.

The comparison branch starts at exact .48. Its production scope is mapping/proof
reuse, lazy worker startup and owner-side accounting, preserving shader generators,
shader admission, GPU rendering, scheduling policy, draw order and resolution.
Source-boundary checks and host/ARM64 fixtures passed. It supplies an independent
CPU experiment, not evidence of a measured gain before owner testing.

<!-- CodexAstraLocal: Preserve diagnostic removal policy and avoid testing unrelated historical failures. -->
New logical sections retain purpose comments. Temporary diagnostics are marked
`CodexAstraLocal TEST-ONLY LOG` with removal conditions; correctness checks remain
when diagnostic collection is retired. Review found no remaining source blocker
in these areas. Hardware testing targets meaningful Dark Moon control comparisons,
five-second lows, shader selection and CPU/GPU cost against the earlier .48 target.

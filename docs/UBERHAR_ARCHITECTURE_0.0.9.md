# Architectural review after 0.0.9

<!-- AstraEH: Early review requested by the owner; distinguishes evidence, implemented prototypes and targets. -->

Reviewed base: `71657e2b58292690fd24185703197365bdab57bc`.
Evidence: the four sessions analyzed in [0.0.9 results](UBERHAR_LOG_ANALYSIS_0.0.9.md),
the current renderer, PICA interpreter, software rasterizer and Android settings.
This follows the 0.0.6 review after three alpha builds. Next default review is
after 0.0.13, allowed after 0.0.12–0.0.14, before beginning 0.0.15.

## Goal and constraints

After generic preparation, a supported draw should execute without waiting for
new game-specific compilation. The owner accepts lower resolution and an initial
execution cost to prove that architecture. Correct draw order and output remain
required; uncovered draws must be reported and rendered correctly. A smaller
wait count alone does not establish success.

The Thor driver advertises neither shader objects nor graphics pipeline
libraries. Extended dynamic state is advertised but remains disabled by the
existing compatibility workaround. Do not assume a new native graphics pipeline
can be linked instantly, or remove the workaround without device validation.

## Implemented experiment in 0.0.10

All three profiles use the existing reference CPU PICA shader interpreter,
including the ordinary geometry/primitive path, followed by the stable software
vertex interface. The CPU JIT and programmable GPU vertex translation are off.
This is a first reference implementation, not a GPU vertex interpreter.

Native mode promotes dynamic TEV fragment processing to the primary route.
Covered draws do not enqueue specialized fragment or pipeline rivals. The full
original FS config is retained for failure recovery. Unsupported draws use the
accurate specialized route. Specialized disk records are neither warmed nor
written in these profiles; the existing driver pipeline cache remains usable.
Generic families and native graphics-state combinations are still created on
demand. Foreground waits are explicitly reported rather than hidden by moving
them out of the old scheduler counters. A complete prepared bank is not present.

Compute mode compiles one reusable compute pipeline before gameplay. Its first
coverage set is solid, integral-edge, six-vertex rectangles with Replace-only
constant/primary/previous TEV sources and unrestricted color replacement. It
requires an RGBA8 surface with supported storage access. Clipping, variable
vertex colors, texture-dependent combiners, depth/stencil/alpha tests, fog,
scissor tests, other blending, write masks and culled states are rejected before
submission. Native rendering handles every rejected draw. The kernel writes
pixels directly; it is not a renamed native rendering preset or presentation copy.

Framebuffer cache invalidation remains owned by the existing framebuffer helper.
Fenced descriptors and surface lifetimes, render-pass termination, and explicit
image barriers order the compute writes against other graphics/transfer work.
Resource destruction waits for GPU completion once at shutdown.

Automatic mode explores both routes within rectangle-size buckets, then chooses
the lower sampled GPU cost and periodically resamples. Timestamp reads only occur
after fence completion and never request a blocking query wait. There are 32
pending samples at most. Timing support is optional: without it automatic mode
uses native rendering and does not claim a measured winner. Samples cover draw
execution intervals, not whole-frame CPU/GPU cost. Measurement can split render
passes; these initial timing results are heuristic, not proof of optimal routing.

The first compute subset establishes integration and correctness contracts. It
does not establish the performance of textured triangles, a general tile renderer
or all PICA states. A title may have zero eligible rectangles; the coverage report
must make that result visible. Large images/3D models still use the native path.

## Settings contract

One persisted integer (`uberhar_test_mode`: 0 custom, 1 native, 2 compute, 3 auto)
backs three mutually exclusive switches at the top of Android Graphics. Switching
the active option off returns to custom mode. Modes require a game restart.

Native runtime overrides are applied after loading saved values. Kotlin's saved
custom objects are not overwritten. Disabled rows display effective profile
values. Internal resolution and integer scaling remain editable. Profile switches
remain available to select another experiment, and other Graphics controls are
locked until all profiles are off. No new runtime permissions or application
dependencies are required.

## Validation and next order

Required gates include native profile invariants; all switch transitions; exact
rectangle recognition/rejection; routing that learns either winner; native-vs-
compute pixel comparisons including clipping, partial workgroups and overlapping
writes; Vulkan shader validation; existing TEV/fragment/primitive checks; and the
full Android/signing/manifest gates. Host tests are not device performance results.

Local validation passed 57,344 exact TEV color comparisons plus 57,344 texture-use
checks, 196,608 full-fragment color and 196,608 depth/discard comparisons, and 256
native/compute image comparisons on Mesa llvmpipe. The production rectangle
admission and sparse adaptive-routing tests, native profile tests, two Kotlin
switch-model tests, existing key/policy/assembly/wait tests, 163 Android setting
keys and eight manifest-policy tests passed. Host C++ syntax checks cover the
changed Vulkan implementation, PICA core and common settings. Vulkan module
validation and the complete Android integration/package build remain CI gates;
the local compute pixel test uses OpenGL transport and is not Vulkan device proof.

1. Capture the three modes at 1x/2x and identify vertex-stage cost, generic waits,
   unsupported coverage, and compute utilization. Preserve an all-off custom
   comparison. Do not infer generic performance from a specialization-warmed run.
2. Implement a precompiled GPU vertex interpreter, beginning with validated normal
   vertex instructions and control flow, with explicit correct recovery.
3. Broaden runtime fragment state and prepare a genuinely device-wide generic
   bank. Check the full native pipeline state space instead of claiming that a
   generic fragment shader alone eliminates compilation.
4. Extend compute coverage only with pixel-level comparisons: triangle coverage
   and interpolation, texture sampling/derivatives, depth/stencil and ordered
   blending, then lighting and procedural behavior. Rectangle timings cannot
   predict these costs. Consider tile binning once the complete reference path
   establishes a meaningful bottleneck.
5. Improve automatic choice using complete route costs and stability/hysteresis
   after both implementations cover comparable work. Reintroduce optional
   specialization only after generic execution can stand on its own.

The hardware-state-as-data design remains the target. A complete ready renderer
is a larger task than the settings controls or this first compute subset. Screen
synchronization and model presentation remain subsequent roadmap items.

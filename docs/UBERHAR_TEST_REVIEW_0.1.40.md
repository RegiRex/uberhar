<!-- CodexAstraLocal: Report the actual four-case timing experiment, including its interrupted fourth observation, without promoting it to matched-scene or complete-title qualification. -->
# Dark Moon test review: 0.1.40

The existing **Settings → Debug → Simulate 3DS GPU Timings** switch is usable
with Native and Combo in the released .40 Android build. Effective launch and
final settings confirm the requested flag, with render-thread delay still zero.
Source inspection confirms that Calculated's preset also leaves the flag alone;
Calculated was not tested on the device because its independent rendering is
still incomplete. A .41 build is not needed merely to expose this switch.

Released source `7bc71c5c9c7e10a39558084392330faa884889ba` passed all 28 required
steps in [workflow 37993185551](https://github.com/RegiRex/uberhar/actions/runs/37993185551).
The installed APK matches the qualified release hash and compatible signing
identity. Installation itself was not observed by this operator. The protected
.22 emulator state and original owner report remain intact.

<!-- CodexAstraLocal: State actual conditions and separate saved shader caches from unobservable driver caches. -->
Each test reset Dark Moon File 1 to Empty and deleted the title's Vulkan shader
cache through the app UI. Launch inventories were empty. All used Vulkan 2x,
normal 100% cap and CPU clock setting 100%. Driver-internal caches remain unknown.
The same app process hosted separate title sessions; optional worker populations,
scene duration and observation overhead differ. Battery/thermal and resource
conditions are retained with the private measurements. CPU-clock setting is not
host CPU frequency, and battery temperature is not SoC temperature.

Native off, Combo off and Native on each have observed Start-to-first-Back
boundaries, normal return and complete final logs. A credit interruption occurred
during Combo on. Its passive collector stopped at its declared five-minute limit;
the later session teardown is recorded, but the operator did not observe its
exit input. The library was already visible when work resumed. The fourth case
therefore uses only the independently bounded observation and cannot establish
a fully observed normal opening or a matched four-way benchmark. No run was
repeated to replace missing evidence.

<!-- CodexAstraLocal: Report reviewed values, keeping the interrupted finite observation visibly distinct from the three observed Start-to-Back cases. -->
| Case | Mean normal speed | Slowest complete window | Best complete window | Mean app logical-core equivalents |
|---|---:|---:|---:|---:|
| Native, timings off | 38.771% | 26.345% | 90.012% | 1.919 |
| Combo, timings off | 33.564% | 18.879% | 62.270% | 1.679 |
| Native, timings on | 68.598% | 57.046% | 90.456% | 2.031 |
| Combo, timings on — limited observation | 66.361% | 38.256% | 90.350% | 2.050 |

The accepted wall durations are 246.279, 261.479 and 175.523 seconds respectively.
These are wall-weighted complete reporting windows, approximately five seconds
each, excluding setup and post-Back navigation. They are not rolling minima or
frame percentiles. Native on advanced through the laboratory and moon sequence
earlier; distinct scene weighting prevents interpreting the mean ratio as an
exact causal speedup. Its 6,640 game submissions average 37.830/s, while its
7,204 system frames average 41.043/s. Neither is interchangeable with speed.

The interrupted Combo-on row uses 56 complete windows over 280.774 seconds,
bounded before result selection by the last valid finite monitor endpoint at
284.097 seconds after Start. First Back and observed normal-exit input remain
unknown. The later roughly 89-minute session lifetime is excluded. This row has
36.731 game submissions/s and 39.705 system frames/s; it is not a matched complete
opening. All four observed populations remain below the 99% target.

Sampled battery temperatures were 29°C, 30°C, 30°C and 30–31°C in table order,
all at 80% battery with AC power. Thermal HAL was unavailable. The eight online
logical CPUs have heterogeneous capacity; Native off reported six available
participants/five workers, while Native on reported seven/six. This is another
comparison limit, not proof of why the allowance changed.

<!-- CodexAstraLocal: Pair selected speed extrema with independently scoped resource context; absent counters remain absent. -->
## Resources near the best and slowest windows

| Case and window | Speed | App CPU cores, overlapping intervals | GPU busy point | RSS MiB point | Own KGSL MiB point | DDR bandwidth |
|---|---:|---:|---:|---:|---:|---|
| Native off, slowest | 26.3% | 1.97–1.97 | 75.2% | 879.8 | 1100.1 | Unavailable |
| Native off, best | 90.0% | 1.58–2.08 | 48.4% | 880.2 | 1101.7 | Unavailable |
| Combo off, slowest | 18.9% | 1.11–1.54 | Unavailable | Unavailable | Unavailable | Unavailable |
| Combo off, best | 62.3% | 1.74–1.89 | Unavailable | Unavailable | Unavailable | Unavailable |
| Native on, slowest | 57.0% | 1.77–1.78 | 99.7% | 983.8 | 1096.8 | Unavailable |
| Native on, best | 90.5% | 1.72–2.12 | 44.4% | 993.5 | 1097.6 | Unavailable |
| Combo on, limited, slowest | 38.3% | 2.43–2.96 | 24.2% | 1148.0 | 5273.5 | Unavailable |
| Combo on, limited, best | 90.3% | 1.96–2.04 | 70.1% | 1051.4 | 3415.3 | Unavailable |

No CPU interval fits wholly inside these exact speed windows; the CPU ranges
are overlapping context, not five-second averages. GPU/RSS/KGSL points listed
here fit inside the window's clock bounds, but the GPU driver's own integration
window is opaque. Combo-off has no fully enclosed sensor point at either
extremum; none is interpolated. The global GPU reading and process-owned memory
have different scopes, and RSS plus KGSL is not a valid total.

Native-on's slowest point is near 99.7% GPU busy at 680MHz, while the limited
Combo-on slowest point is 24.2% at 401MHz with roughly 2.43–2.96 app core equivalents
in overlapping CPU intervals. Thus these samples do not support a universal
GPU-saturation explanation. The selected Combo-on minimum overlaps heavy
observation; excluding recorded heavy-observer enclosures gives 51.876% as that
subset's minimum. This neither measures observer cost nor establishes the same
scene. The limited Combo-on own-KGSL point range is about 2042–6814 MiB; allocation
accounting does not establish physical residency, unbounded growth or a leak.

<!-- CodexAstraLocal: Keep temporal evidence separate from speed and preserve missing scene coverage. -->
Native off has three isolated events in the first moon clip and one in its later
moon clip, all one decoded frame. Its early laboratory clip has seven isolated
ghost substitutions/cuts. Combo off has zero counted moon and ghost events in
its two actual clips, with later angry-ghost coverage missing. Native on's first
moon clip has 56 events across 78 decoded frames, maximum three consecutive
frames; 19 events last at least two frames. It crosses all three owner-defined
investigation thresholds. The owner subsequently stopped automated visual
analysis and will assess noticeable artifacts. Native-on ghost and Combo-on
temporal reviews remain unfinished; retained clips are not treated as reviewed
merely because they were recorded. No new visual qualification is claimed.

Recorded frames establish visible capture events, not their rendering or
capture cause, panel persistence or correctness of unrecorded scenes. Background
objects visible through translucent ghosts are not automatically counted as
corruption. Abrupt rectangular cuts and rear-ghost face substitutions are tracked
separately from those persistent shapes.

<!-- CodexAstraLocal: Carry the architectural implications without assigning an unmeasured GPU pass or manufacturing CPU utilization. -->
The timing switch delays emulated GSP completion interrupts; it does not directly
add CPU workers or transfer CPU work to the GPU. The measured Native improvement
supports investigating guest pacing as part of the slowdown, while substantial
remaining cost and graphical faults still need explanation. None of these
results reaches the 99% target.

The new deferred CPU queue still executes one packet per wave on its coordinator,
with no auxiliary fan-out in all three closed cases. Same-draw vertex workers
remain active. Useful whole-app CPU work is about two logical cores, including
those workers; a higher speed reading does not complete the portable multicore
objective. The .40 static TEV fragment route is actually selected, but route
counts do not identify GPU execution cost or prove a version-to-version gain.

Per-core CPU counters measure whole-device activity. The exposed GPU busy counter
is one driver aggregate with an opaque sampling window, not per-GPU-core usage.
CPU intervals overlapping the slowest/best window remain contextual when no
interval fits wholly inside it. Process RSS, GPU allocations and system available
memory have different, non-additive scopes. Measured DDR bandwidth remains
unavailable; clocks, votes, pressure and spare RAM are not substitutes.

The separate [architecture review](UBERHAR_ARCHITECTURE_0.1.40.md) and
[cleanliness review](UBERHAR_CLEANLINESS_0.1.40.md) carry these constraints into
future optimization. After these results, the owner authorized implementing the next concrete fix
and testing it on hardware, superseding the previous pause. The owner separates
Architect from Developer/Engineer and dedicates the Calculated agent exclusively
to its independent prototype. No .41 candidate is ready and no beta or 4x
qualification is implied.

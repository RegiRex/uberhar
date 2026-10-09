# Uberhar .33–.34 test review

<!-- CodexAstraLocal: Bind completed release and Dark Moon evidence to the finite two-build batch; keep the added cross-title comparison separate. -->
This review covers the owner's two-build batch after .32. Released .33
is `2dc50832c4fefa16f15ba86e6cccb76a6b432452`; .34 source is
`6893c10b5dab28bb60135efa4a5ebe07f82bd349`, tree
`15baf5ddbaa47e4c31dc1f3b644b8906ce33afb1`. All 23 required .34 gates passed in
[run 37885064863](https://github.com/RegiRex/uberhar/actions/runs/37885064863).
Exact source/tag, APK checksum, package and signing compatibility were independently
verified before the data-preserving .34 / 33991465 installation. APK SHA-256 is
`8d31aa4bb5aa44055558fa269fe43183d994b354c1fd4da2a4f897587e19aebe`.
Cryptographic signature verification passed in the exact CI run; local certificate
parsing establishes identity only. Both builds completed one strict diagnostic and one Native and full Combo
opening each, with normal returns. The separate architecture and cleanliness
audits are complete. Development stops for owner inspection; no .35 is selected.

<!-- CodexAstraLocal: A strict diagnostic with missing output has no comparable complete-title throughput. -->
## Calculated isolation and the separate benchmark

The single .33 strict Calculated launch reported 837,453 attempted and omitted
unsupported draws, zero computed draws and zero guest graphics fallback. The game
region was blank. Shared CPU vertex processing still ran; isolation does not make
the existing restricted rectangle implementation a complete independent renderer.
No game-speed result is scored for that incomplete output.

Its consume-once scratch benchmark failed shader preparation before recording
work. No route completed, no pixels were validated and no timing ratio exists.
The actual compiler rejected a conflicting Vulkan macro definition. The .34
release removes that preamble and adds a regression that executes the same
production compiler and extracted benchmark shader/call. Its successful target execution below remains separate from the twelve validated
host modules and original-failure control.

The single .34 strict launch also produced blank guest output: 1,032,364 attempts
were all omitted for unsupported state, with zero computed guest draws and zero
guest graphics fallback. No gameplay input was sent into the blank scene.

The separate .34 scratch experiment completed all 32 pairs / 64 routes and 2,048
operations. Submission, GPU completion, full-image validation, identity and
available finite timestamps pass the independent retained-evidence checks. Each
case has eight alternating-order pairs, with 32 operations per route.

| Supported workload | Median paired compute / graphics route time |
| --- | ---: |
| 256×256, RGBA | 0.904998 |
| 256×256, R+B only | 0.926360 |
| 800×480, RGBA | 1.005424 |
| 800×480, R+B only | 1.006753 |

Values below one mean lower elapsed compute route time for these supported
synthetic commands. GPU TOP-to-BOTTOM includes route barriers, with one graphics
pass versus 32 compute dispatches. Preparation, reset/upload/readback and host
enqueue/validation are outside those intervals. These are neither exclusive
kernel measurements nor per-draw/game-speed gains. The device reports full-image
validation; raw device pixel buffers were not retained for independent replay.
This establishes comparable supported fragment work, not general title coverage.

<!-- CodexAstraLocal: Keep normal-speed intervals inside the actual Start-to-first-Back boundary and report lower-tail/stall scope. -->
## Ordinary cold openings

Each completed ordinary control had one opening, File 1 reset, zero title application
shader caches, Vulkan 2x, normal/frame and CPU limits 100%, JIT and accurate
multiplication enabled. Driver-private caches remain unknown. Native and full
Combo retain complete rendering. Generic was not repeated for .33. The .33
controls share the process used first by its strict smoke.

| Build/method | Complete report windows | Mean normal speed | p05 window average | Worst reported interval |
| --- | ---: | ---: | ---: | ---: |
| .33 Native | 40 / 201.200 seconds | 29.099% | 21.167% | 200.716 ms |
| .33 full Combo | 40 / 201.204 seconds | 28.310% | 21.893% | 214.836 ms |
| .34 Native | 40 / 201.363 seconds | 29.333% | 21.382% | 220.934 ms |
| .34 full Combo | 40 / 201.396 seconds | 26.241% | 20.238% | 1012.491 ms |

Every accepted ordinary window averages below 99%. Scene progression, observation,
process ordering and temperature differ; these means do not establish causal
mode/version gains. Native and Combo both report zero ordinary skipped draws
and required-fallback failures. Those complete routes must not be merged with
Calculated's explicitly omitted draws.

The .33 Native control's separate all-batch LoadVertices brackets occupy 82.441% of their
reporting wall; Combo's occupy 66.891%. These include loading, JIT, FIFO/output/
assembly and intervening scheduling or waits. They are not exclusive shader CPU
shares; the residual is not GPU time. .34's five recipe counters identify actual
transport invocations, not time or visible actor coverage.
Native's .34 separate brackets occupy 82.293% of reporting wall. Its 37
contained recipe spans include 878,445,968 no-geometry-shader input invocations:
759,361,942 (86.444%) use the four recipes, 119,084,026 use the generic prepared
path, and zero use the legacy path. This establishes actual coverage, without
isolating input-loading cost or proving a causal speed improvement.

Combo's corresponding 37 spans contain 596,612,086 recipe invocations out of
706,532,601 no-GS inputs (84.442%), with zero legacy inputs. Its separate
LoadVertices brackets occupy 67.533% of reporting wall. About eleven minutes of
menu preparation for the new activity measurements preceded the Combo opening;
this and the different observer/temperature conditions prevent a matched
mode/version gain claim.

<!-- CodexAstraLocal: Count consecutive decoded anomalies within the actual clips and preserve absent scene coverage. -->
## Consecutive-frame visual evidence

For .33 Native, all 456 decoded moon frames and 635 ghost frames were reviewed.
There are three isolated single-frame moon events and three separate isolated
ghost blocks; all six clear in the next decoded frame. Combo's 428 moon and 654 ghost frames contain no comparable events.
The older dense ghost patches were not identified in the covered later regions.
Native misses the quiet near/early-wide checkpoints; Combo includes the late
wide/right book-holder tail but misses the earlier near-ghost and initial pullback.
Electrical bloom and unequal phases limit interpretation. The recordings do not prove renderer cause,
physical-display persistence, comparative event frequency or full-game correctness.

Moon events remain below the owner's priority rule: more than five events, any
longer than two consecutive frames, or at least three lasting two or more frames.
Ghost disturbances remain separate.

For .34 Native, all 455 moon and 642 ghost decoded frames were reviewed. Five
isolated moon events and five separate isolated ghost block/band events each clear
in the next decoded frame. Five single moon events do not exceed the owner's
threshold. Older dense irregular ghost patches were not identified in the visible
later cores; the earlier quiet near/full-wide checkpoints are absent.

Combo's .34 review covers all 410 moon and 669 ghost decoded frames, with
zero comparable block/band events identified. Its ghost clip includes the near
ghost tail, pullback/right book-holder wide scene, electrical beam, chalkboard
and broom; it ends before the later close book angle. Neither recording
establishes whole-title correctness or comparative event frequency.

<!-- CodexAstraLocal: Describe sampled conditions without reviving routine exit sweeps or claiming long-play bounds. -->
## Conditions and memory

At opening +40/+165 seconds, .33 Native KGSL is 1075.43→1075.54 MiB and RSS
912.29→909.09 MiB, with battery temperature 23→24 C. Combo KGSL is
2519.62→3029.80 MiB and RSS 1022.41→1034.39 MiB, battery 25→26 C. External
power is reported, battery 80%, saver off. Battery temperature is not SoC/GPU
temperature; thermal HAL is not ready and clock residency is not established.

The one .33 strict-smoke ownership observation records same-process KGSL
10.77→925.51→13.32 MiB; the final sample starts 26.095 seconds after normal exit.
That demonstrates delayed recovery of this failed-preparation session, not a
successful scratch-owner lifecycle or a peak/long-run growth bound. Ordinary
controls had no routine post-exit sweep. The .34 strict/scratch control used a new
process and battery temperature 24 C; its same-process KGSL observations were
10.78→925.60→13.53 MiB. The last sample started 23.994 seconds after normal exit.
This is one justified scratch-owner cleanup observation, not a long-play bound.
The .34 Native opening samples were KGSL 1087.73→1078.32 MiB and RSS
908.43→905.83 MiB, with battery temperature 25 C at both readings. Native
returned normally; its navigation tail is excluded from the opening metrics.
Combo's .34 samples were KGSL 2519.70→2956.37 MiB and RSS
992.63→1007.28 MiB, battery temperature 28 C. It returned normally. Its roughly
one-second worst interval lies outside both recorded video and observer brackets;
that excludes direct capture overlap, without identifying the stall's cause.

<!-- CodexAstraLocal: The owner expands this batch with one preserved-save cross-title activity observation; activity is distinct from emulation speed or theoretical capacity. -->
## CPU/GPU activity comparison

The same .34 Dark Moon Combo opening includes two activity windows, each with
five snapshots about five seconds apart. Actual fixed-path queries take roughly
62–80 ms and are included in the ordinary observer exclusions. CPU percentages
below are process or thread running time divided by elapsed time: 100% means one
logical core's time, not the whole eight-core processor.

| Sampled scene interval | App CPU, one-core equivalents | Main emulation thread | GPU reported busy windows |
| --- | ---: | ---: | ---: |
| Dark Moon, opening +45 to +65 s | 93.4–215.0% | 76.8–98.2% | 78.29–99.90% |
| Dark Moon, opening +175 to +195 s | 99.2–113.4% | 86.6–99.0% | 86.08–99.91% |
| MH4U, initial load/transition only | 49.0–61.8% | 28.8–39.6% | 16.97–85.76% |

Dark Moon's app uses approximately 12–27% of accounted machine core time in
these windows. One emulation thread is often nearly fully occupied; all eight
CPU cores are not saturated. Compilation workers contribute to the earlier
higher process total. GPU samples frequently report high activity, with observed
Dark Moon endpoint clocks ranging from 475 to 680 MHz. These device-wide driver
windows do not measure maximum shader/arithmetic throughput. Their exact shipped
kernel implementation is unverified; they are not treated as cumulative
counters or aligned CPU interval percentages. Endpoint frequencies and last-CPU
values do not establish residency, and heterogeneous cores are not interchangeable.

MH4U uses the existing character save, full Combo, Vulkan 2x, CPU/frame limits
100% and verified empty application shader-cache inventories. Its saved title
data and extdata were backed up privately before launch. The retained activity
window covers initial loading/transition, not verified interactive gameplay.
A later twelve-second clip shows a populated cinematic; a 100% overlay there
cannot qualify sustained gameplay. Normal-speed limiting may also deliberately
leave hardware idle. The earlier blue-field still does not establish a rendering
defect. No causal cross-title performance or headroom claim follows from these
unmatched phases.

The single MH4U launch reached a save prompt before interactive gameplay was
verified. It was explicitly paused, then exited normally without confirming that
prompt or continuing beyond it. A private readback nevertheless found 1,042
changed character-save bytes; system data, metadata and extdata matched the
backup. After retaining original, paused and exited copies, only that changed
file was restored. All fifteen original title/save/extdata files then matched
byte-for-byte. The change's in-game meaning is unestablished. The bounded
transition comparison is retained, but verified gameplay comparison remains
unqualified. The original screen timeout was restored and the device put to sleep.

<!-- CodexAstraLocal: Keep raw device evidence private and leave qualification separate from batch completion. -->
All six .33/.34 Dark Moon controls returned normally. Raw artifacts and exact action/provenance
records remain private. Complete independent rendering, sustained 99% at 2x,
4x, longer gameplay and the retained Fire Emblem Awakening beta baseline remain
unqualified. The two-build development and audit boundary is complete; the added MH4U
comparison establishes transitions only, with gameplay unqualified. Do not
start .35 automatically.

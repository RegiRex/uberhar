# 0.1.0: Fire Emblem, Ocarina and Sonic on Thor

<!-- AstraEH: Six device sessions, source revalidation and primary-source compatibility research. -->

Reviewed 2026-09-27. Both supplied logs identify released 0.1.0 revision
`7af34fbfe61cdae68f598fb43a16178ec0b5ab78` on AYN Thor/Adreno 740:
`uberhar_log_9_27_1138_FEA.txt` (1,638 lines) and
`uberhar_log_9_27_1143_TLOZOOT3D_SASRT.txt` (15,584 lines). They contain four
Fire Emblem Awakening sessions, one Ocarina session and one Sonic session.
All use Native/Vulkan, CPU vertex JIT, forced generic TEV and CPU clock 100%.
New 3DS model selection is true in every settings snapshot. Sonic changes from
2x to 1x during its run; the other titles stay at 2x.

## Fire Emblem: smoothness retained, CPU work reduced

| Measurement | Normal cold | Normal warm | Fast cold | Fast warm |
| --- | ---: | ---: | ---: | ---: |
| Intended-band speed, 0.0.15 | 98.858% | 99.795% | 283.379% | 292.518% |
| Intended-band speed, 0.1.0 | 98.895% | 99.810% | 270.601% | 274.831% |
| Whole-session generic wait, ms | 662.341 | 47.810 | 713.079 | 41.815 |
| Generic families / pipelines | 4 / 37 | 4 / 37 | 4 / 37 | 4 / 37 |
| CPU vertex stage, ms | 18,450.107 | 18,579.769 | 18,248.964 | 23,674.052 |
| Vertex-stage ns/input, 0.0.15 | 185.40 | 186.32 | 108.02 | 107.55 |
| Vertex-stage ns/input, 0.1.0 | 167.41 | 168.41 | 97.95 | 97.00 |
| Worst frame interval, ms | 375.414 | 71.310 | 425.874 | 59.765 |

Header lines 146/567/981/1350; speed-band lines 435/840/1220–1221/1618–1619.
Fast-only comparisons exclude initial normal-speed portions and one transition
interval each. The new vertex stage costs **9.3–9.8% less per input** in all four
observations. This supports the intended CPU improvement; it is not a controlled
benchmark or proof of an equal whole-emulator speedup.

Fast-forward averages are actually **4.5% and 6.0% lower** than 0.0.15. Do not hide
this behind the favorable vertex metric. Workload differs: fast-cold inputs per
game submission rise from about 27,185 to 30,644; fast-warm rises 25,698 to 31,516.
The latter run processes 244.1 million inputs versus 189.9 million previously.
Different scene/animation weighting can explain part of the difference, but the
logs cannot exclude a remaining performance regression. Compare the same replay
before claiming improved total headroom.

The final-vertex cache reuses 71.5/71.6/119.8/157.5 million conversions. There are no
mapping/debugger admission failures; 7,405/7,353/14,246/18,276 GS batches deliberately
retain the established path. CPU JIT work remains about 5–6 ms. All sessions have
zero skipped draws, generic failures and module rejection/write failures. Normal
speed and user-reported image quality are retained. Keep 0.1.0 as the successful
beta baseline, with 0.0.15 available for controlled headroom comparison.

## Ocarina: broader-title success

Title `0004000000033500`, header line 135, totals 1089–1114:

- Normal band: **99.912%** across 293.666 active seconds.
- 400%-cap fast band: **374.628%** across 16.128 seconds, worst interval 17.600 ms.
- One generic fragment family, 24 native pipelines, **132.196 ms** generic wait.
- All 919,913 renderer draws use the generic route; zero recovery, failures or skips.
- One CPU shader program compiles in **0.387 ms**. Final-vertex reuse covers all
  no-GS batches, with 149.6 million hits and no mapping failures.

Whole-session speed 114.223% mixes normal and fast play and is not a useful fast
benchmark. The short fast segment is promising, not proof that every dungeon or
effect can sustain that rate. User reports correct, nearly flawless play.

## Sonic: sustained rendering workload, not recurring compilation

Title `00040000000B3500`, header line 1240, totals 15554–15580:

- **3 generic families / 33 pipelines**, all created by **72.832 seconds** after the
  run header. The sustained race slowdown appears afterward and persists without
  further generic builds.
- **612.534 ms** total generic foreground wait over **328.445 active seconds**;
  zero specialized pipeline builds, scheduler pipeline waits, recovery draws,
  generic failures, rejected modules or skipped draws.
- Five CPU shader programs compile in just **2.076 ms** total.
- **13,090,869 renderer draws**, 13,090,826 ordinary CPU vertex batches,
  **1,728,939,195 input vertices**, 769,640,847 shader invocations and 959,298,348
  indexed hits. These are distinct counters: explicit skipped draws remain zero, and ordinary-
  batch accounting excludes the separate immediate-vertex route.
- CPU vertex-stage work totals **116.060 seconds**. Sustained racing windows often
  spend about **40–58% of wall time** in that stage and process roughly 40,000–66,000
  batches per second. This broad stage includes loading, execution, conversion and
  assembly; it is not just shader arithmetic.

| Selected complete racing windows | 2x earlier race | 1x later race |
| --- | ---: | ---: |
| Window end times relative to header | 95.49–175.80 s | 200.09–310.41 s |
| Number / active duration | 17 / 85.342 s | 23 / 115.360 s |
| Duration-weighted speed | **53.270%** | **69.037%** |
| Individual window range | 44.512–62.462% | 49.852–95.102% |

The resolution change helps, but does not restore full speed. These are different
laps/scenes and are not a controlled resolution benchmark. The overall 74.667%
includes faster menus/loading and overstates steady race performance. The 1x image
also has one quarter of 2x's internal pixel count; the log cannot diagnose any
additional visual corruption without screenshots.

The compile-free portion makes repeat laps useful warm-state evidence. Generic
compilation demonstrably ends before the racing slowdown; this is stronger than
assuming the same track guarantees identical shaders, objects or effects on each
lap. It does not prove every texture/data access is warm.

**Interpretation:** compilation is not the principal sustained bottleneck. CPU
vertex/draw volume is a demonstrated major cost, and renderer submission, guest CPU,
memory transfers and GPU execution remain possible additional limits. Our CPU vertex
route may still contribute to the slowdown: being compilation-independent does
not make it unrelated to Uberhar's renderer. The game uses our generic route for
every draw; there is no evidence that it falls outside supported shader coverage.

Recorded Android thermal status stays zero, no low-memory/power-save flag appears,
and battery temperature rises roughly 24–27 C during Sonic. These readings cannot
rule out CPU/GPU clocks or scheduling limits. The stock Azahar 4x comparison and
aspect-ratio flashing are the owner's observations; no stock log/video accompanies
these files, so backend/settings differences and the precise visual cause remain
unresolved. Do not silently apply a layout, right-eye or precision hack.

## Compatibility research and New 3DS distinction

Primary sources checked 2026-09-27:

1. [Azahar compatibility issue #99](https://github.com/azahar-emu/compatibility-list/issues/99)
   reports Sonic racing drops from 60 to about 25 fps or lower with Vulkan on
   Azahar 2123.1, and still worse OpenGL performance on the reporter's Windows/Vega
   system. It is an individual report awaiting another reporter, not a Thor backend
   recommendation or a confirmed diagnosis. Switching to OpenGL is not an established fix.
2. [Luma optional-feature documentation](https://github.com/LumaTeam/Luma3DS/wiki/Optional-features)
   distinguishes forced clock, forced L2 and their combination; it acknowledges
   rare stability problems in games that do not officially use those capabilities.
3. [Luma issue #742](https://github.com/LumaTeam/Luma3DS/issues/742) contains a 2017
   report specifically associating Sonic hard locks with forced L2, while clock-only
   behaved differently. The issue concerns toggle behavior and was closed with a
   cannot-reproduce label; it is historical supporting evidence, not proof of a
   present Azahar defect. The owner independently recalls the L2-specific problem.
4. [Citra MMJ's configuration](https://github.com/weihuoya/citra/blob/master/src/android/app/src/main/assets/config/config-games.ini)
   lists both Sonic region IDs in its New 3DS mode section. That is a different
   implementation, but reinforces that hardware-CFW behavior must not be translated
   into an unconditional emulator rule without testing.

In our pinned Azahar source, `is_new_3ds` selects model/core-count/memory behavior.
`AppLoader_NCCH::LoadNew3dsHwCapabilities` reads the title's 804 MHz and L2 request
bits; `KernelSystem::UpdateCPUAndMemoryState` applies the 804 flag only from that
request. `SetRunning804MHz` stores metadata consumed by APT's running-mode query;
it does not itself triple `Core::Timing`'s CPU scale. The L2 request is read and
serialized but is not used to simulate the physical L2 cache. CPU clock percentage
is an independent setting, **100% in these runs**. Native graphics profiles do not
force New 3DS model or CPU overclock settings.

Thus New 3DS mode is **not equivalent to Luma Clock+L2**. Turning model emulation
off may still affect game compatibility, but neither this log nor the old Luma
report establishes that it fixes Sonic's emulator performance. A controlled same-
backend/resolution warm test with only model mode changed is the useful next probe.
Exit/relaunch between tests; keep CPU clock at 100%. Do not combine an OpenGL change
with the model test, as that would confound the result.

## Focused 0.1.1 response and release scope

Sonic exhausts the 8192 detailed-vertex sample budget at **100.526 seconds**, before
most of the race and before the 1x switch. Broad stage/frame records continue,
but detailed samples cannot explain the later slowdown. Replace lifetime quotas
with one sample per 50 ms using the existing draw timestamp; retain a rotating
selected vertex and add whole sampled-batch setup/vertex/DrawTriangles durations.
Reports stay on the existing five-second/shutdown cadence, with fixed memory.

The log also contains **13,141** repeated GetFriendKeyList warnings: 12,412 before
80 seconds, none during 80–325 seconds, and 729 afterward. They are menu/log noise,
**not an explanation for the race slowdown**. Keep the exact IPC response, record
first-four/power-of-two summaries plus shutdown totals, reducing this workload to
15 warning records plus one final aggregate. Add one launch snapshot of model,
requested capabilities, kernel metadata and clock setting for future comparisons.

Do not expand renderer math, add a speculative Sonic override, or promise a speed
fix in this diagnostic iteration. The user explicitly prefers a focused alpha.
[Automatic compatibility profiles](UBERHAR_COMPATIBILITY_PROFILES.md) are recorded
for **after 1.0.0**, pending reproduced rules. 1.0.0 remains the shader-upgrade
milestone; unrelated upstream game compatibility defects need not all be solved
before it. The full architectural review cadence remains after 0.1.3 by default;
this is a device analysis, not another full review.

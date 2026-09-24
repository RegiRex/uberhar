<!-- AstraEH: Owner-supplied device evidence and the targeted 0.0.9 response. -->
# Awakening 0.0.8 analysis

Input: `uberhar_log_9_24_1442_FEA.txt`, session date 2026-09-24 14:42:41 -04:00.
Revision `2aa367d0a10f554f6612bb8beb7bee7303d0b535`, published 0.0.8. The private
log is not committed. Both complete cold/warm game sessions use the same Thor,
Adreno 740, Qualcomm 512.676.53, Vulkan, 2x resolution, hybrid on, force off,
CPU bridge on, SPIR-V generation on and frontend optimization off.
The cold session has no driver pipeline cache; the warm session reconstructs
289 host pipelines. No renderer errors, fallback failures or bridge mismatches
are recorded. Service/BOSS and missing-save-slot messages recur in the warm run.

## Measured results

| Cold measurement | 0.0.7 | 0.0.8 |
| --- | ---: | ---: |
| Draws | 193,352 | 182,345 |
| Scheduler wait events | 127 | 127 |
| Total scheduler wait | 18,957.466 ms | 20,789.642 ms |
| Maximum scheduler wait | 1,062.682 ms | 1,336.832 ms |
| Waits at least 50 ms | 64 | 79 |
| Specialized builds | 314 | 289 |
| Specialized driver-call wall time | 67,785.019 ms | 76,442.603 ms |
| Fallback families built | 18 | 9 |
| Fallback pipelines built | 50 | 41 |
| Fallback draws | 210 | 419 |
| Fallback driver-call wall time | 10,075.305 ms | 13,386.540 ms |
| Used / unused fallback pipelines | 15 / 35 | 23 / 18 |
| Unused fallback driver-call wall time | 7,808.628 ms | 6,187.318 ms |
| CPU bridge draws | Not available | **0** |
| Warm scheduler wait | 0 ms | 0 ms |

Cold waiting increases **9.7%**, maximum waiting **25.8%**, despite fewer draws.
The owner felt smoother most of the time but worse during the bad pauses.
Higher reuse and more fallback-served draws are encouraging, but these logs
do not establish improved typical frame delivery. They measure scheduler waits,
not frame-time percentiles, and the runs are not a deterministic replay.
This is not evidence that every perceived pause is exactly 1.337 seconds: several
waits, GPU contention and other emulation work can affect delivered frames.

The warm session has 182,352 draws, zero new specialization misses and zero
pipeline waits. The loaded 289 pipelines take 207.205 ms of driver-call wall
time in total; the maximum is 1.722 ms. The cold/warm paths in this capture have
almost identical draw counts, although that alone does not prove identical play.

## What the new diagnostics establish

**The CPU bridge did no work.** It was enabled, yet pending/selected/warming,
draws/batches/vertices and CPU time all remain zero. There are 162,251
ineligible draw observations. In 0.0.8 eligibility required complete list
batches, at most 4,096 input vertices. The log does not split the rejected
observations by topology, size or completeness, so it cannot establish which
restriction dominates. Zero CPU time rules out CPU bridge execution as the
cause of this run's severe pauses. Broadening coverage is a correction to
0.0.8's limited usefulness, not a demonstrated speedup yet.

Host pipeline reuse works: 317 guest pipeline records map to 289 host pipelines;
27 vertex configurations map to ten modules. Live vertex GLSL generation totals
8.654 ms, maximum 0.568 ms. That foreground translation is not the dominant
recorded delay. Fragment fallback generation/module work totals 102.054 ms.
The largest logged scheduler wait chooses a fallback whose driver call took
1,335.273 ms, with only 9.372 ms waiting for its VS dependency. Other waits also
track driver creation or queued work, not a multi-second GLSL translation.

Source/state reduction continues: 125 raw fragment families become 37 canonical
families, and 235 raw candidate pipelines become 147 canonical candidates. The
census is uncapped. Fewer builds do not guarantee less total driver time, and
larger generic programs can cost more to optimize. Driver thermal/load behavior
is not captured; do not assign all timing changes to one source change.

**The current driver advertises neither graphics pipeline libraries nor shader
objects.** Extended dynamic state and custom border color are advertised but
remain disabled by inherited compatibility policy. Max push constants is 256
bytes. These results rule out simply enabling a pipeline-library fast-link path
on this driver. They do not justify removing workarounds or assuming a replacement
driver will fix the pauses.

## 0.0.9 response

1. Admit bounded strips/fans and the list-equivalent Shader topology when guest
   geometry is disabled. Also cap expanded output at 4,096 vertices, limiting
   strips/fans to 1,367 input vertices.
2. Make the CPU bridge contract explicit at the PICA draw boundary. Use isolated
   assembly for this already-acceleratable draw so a strip/fan tail cannot force
   subsequent draws to stay on the ordinary software route. Build its generic
   pipeline with the actual **list output topology**, matching the final bind.
3. Log bounded admission reasons and seen/selected topologies. Keep the ready-only
   requirement, single speculative compiler, exact key validation and independent
   bridge switch.
4. Retain eight worst waits over the entire renderer session and report an
   exhaustive wait histogram. 0.0.8's first-20 detail cap can miss later events.

The accelerated path already assumes strip/fan state does not carry across host
draws; this update preserves that inherited behavior and documents its existing
limitation. Ordinary software assembly remains persistent. No claim of new
hardware-accurate cross-draw assembly is made.

## Next decision criteria

The next log must first demonstrate bridge selections and validated draws, with
no mismatches. Compare CPU maximum and total time, scheduler wait distribution,
worst waits and fallback utility. If bridge-off is faster or visual differences
appear, narrow admission using that evidence. If size rejection still dominates,
evaluate bounded chunked execution with correct vertex/index caching rather
than removing limits blindly. If generic-state warming still dominates, the
next architectural step is startup generic coverage and reduced native-state
variation. A ready-only on-demand bank cannot guarantee a stall-free first draw.
The full review remains targeted after 0.0.10, within the 0.0.9–0.0.11 window.

Topology reference: [Khronos primitive assembly](https://docs.vulkan.org/spec/latest/chapters/drawing.html).
Production tests compare the real PICA assembler with independent list, strip
and fan triangle sequences, including repeated/degenerate indices and winding.

# Bounded CPU vertex timing

<!-- CodexAstraLocal: Define the diagnostic's population and observer limits before presenting durations as optimization evidence. -->

This opt-in 0.1.27 diagnostic measures selected
contiguous chunks in Combo's prepared no-geometry-shader CPU vertex path. It does
not profile all emulator work, sample stacks, measure GPU execution, or qualify
performance. All inputs still pass through the existing indexed FIFO and ordered
primitive assembly. A chunk starts inside a draw; its FIFO and shader-register
state must include preceding inputs from that same draw.

`boundary` records owner-thread CPU and wall time around the selected chunk.
`detailed` additionally records actual input lookup, loading, shader execution,
output conversion, cache maintenance and assembly intervals for every input in
that chunk. Cache hits have no shader invocation or conversion. Neither mode
multiplies one vertex's timing by an unobserved draw size. Work outside selected
chunks remains unmeasured, including draw setup, `DrawTriangles` and other guest
CPU work. The diagnostic is restricted to Combo/ComboGeneric;
an absent or disabled request leaves the ordinary path selected.

## One finite request

<!-- CodexAstraLocal: Keep the explicit arm separate from the opening action and use safe unique IDs so a later run cannot overwrite earlier evidence. -->

Place a UTF-8 JSON file named `uberhar_vertex_timing.json` in Uberhar's user
`config` directory before launching the title. Use a fresh `diagnostic_id` for
each run. The following request targets a ten-second moon interval;
the actual scene must still be verified on the tested build:

```json
{
  "schema": 1,
  "enabled": true,
  "mode": "detailed",
  "diagnostic_id": "moon-detailed-01",
  "title_id": "0004000000055F00",
  "trigger": "next_gameplay_transition",
  "delay_ms": 20000,
  "duration_ms": 10000,
  "period_ms": 100,
  "chunk_vertices": 64,
  "max_chunks": 64,
  "max_vertices": 4096,
  "seed": 1
}
```

The title ID must match exactly. IDs contain one through 48 ASCII letters,
digits, underscores or hyphens. Modes are `off`, `boundary` and `detailed`.
The parser rejects duplicate/unknown fields, wrong types and out-of-range values.
Limits are: delay 0–180,000 ms, duration 1–10,000 ms, period 50–1,000 ms,
chunk size 1–64 inputs, 1–64 chunks, at most 4,096 total observed inputs and a
32-bit unsigned seed. Only draws of at most 4,096 inputs enter the separate
diagnostic runner, so its full-draw prefix/suffix changes affect at most 262,144
inputs across 64 selections. The report retains that full-draw total separately
from measured inputs. Larger draws use the ordinary runner and are counted as
outside diagnostic coverage; no rendering budget or draw suppression is imposed.

Prepare the title's cold application cache and required save-slot reset first.
At the opening's Start screen, use the existing **Test phase → Gameplay** marker
to arm once, close the menu, then start the opening. This requires a transition
from Automatic/Loading to Gameplay; selecting an already active Gameplay label
does not arm the request. Record marker time and the
actual game-start input separately: they are different boundaries. A phase label
is an operator annotation, not automatic scene detection. Failure to arm within
300 seconds of the first observed CPU draw expires the request. Changing the
title/run/phase or exhausting the finite budget terminates measurement.

Return normally to the game list to retain the bounded report under
`dump/uberhar_vertex_timing/<diagnostic_id>.json`. Existing files are never
overwritten. A crash, unavailable CPU clock or failed storage can leave evidence
incomplete or absent; emulation must continue normally. Remove the request after
the diagnostic run so later launches stay ordinary. The report contains metadata,
counts and durations, not shader words, uniforms or vertex payloads. Retain raw
reports privately and publish only reviewed derived findings.

<!-- CodexAstraLocal: Refuse providers that cannot preserve exclusive creation; optional capture failure cannot change game storage behavior. -->
Raw Android/Linux filesystem access supports exclusive report creation. True
Android SAF, Windows and libretro providers disable this diagnostic because their
current file adapters do not preserve that exclusive-create contract. Normal
emulation and storage remain available on those providers.

## Interpretation and controls

<!-- CodexAstraLocal: Complete accounting within a small selected population still does not make that population representative or eliminate timing perturbation. -->

Compare a request-free run, a boundary-only run and a detailed run with the same
cold setup, normal speed limit, resolution, power conditions and scene evidence.
Verify effective settings and complete zero application-cache inventories in
ordinary logs; driver-private caches remain unknown. Avoid video, screenshots,
log pulls or process-counter sweeps inside measured intervals. Brief visual
evidence outside them establishes scene context, not exact guest-frame identity.

Use the report's actual chunk ranges, program/entry identity, input counts,
FIFO hits/misses and coverage before comparing timings. Periodic first-eligible
selection is not random workload sampling. A different scene mixture or set of
programs can change the result even when the host delay is equal. Never scale
these measurements to all vertices, add nested intervals to their parent total,
or call CPU assembly time Vulkan submission time.

Program/swizzle hashes and entry identify a code family, not identical dynamic
work. Live inputs and boolean/integer uniforms can change shader branches and
loops within that family. The same seed can also select different CPU draws as
asynchronous GPU readiness changes. Retain scene evidence and actual cohort
coverage before attributing a cross-run difference to an optimization.

Detailed clocks perturb the work they measure. Retain calibration and compare
against boundary-only controls; calibration does not justify subtracting a fixed
clock cost as if it recovered an uninstrumented execution. Chunk wall time
includes preemption. Owner-thread CPU time excludes off-CPU time, but wall-minus-
CPU cannot distinguish sleeping from runnable delay or identify GPU time.
Boundary-only interior timing is unmeasured: its serialized zero durations must
not be interpreted as zero-cost operations.
Missing clocks, partial chunks, caps, rejected records and suppressed overlapping
legacy samples must remain visible in analysis. An empty report proves no
measured coverage, not zero cost.

Both enabled modes suppress the old sparse vertex sample in a selected draw to
avoid overlapping timing brackets. Off/on comparisons therefore measure the net
observer change, including that suppression, rather than purely added clock cost.

New explanatory comments and diagnostic attribution use **CodexAstraLocal**.

<!-- CodexAstraLocal: The independent host reader validates counts and flags before deriving only retained-population totals. -->
Read a retained report locally with:

```sh
python3 tools/uberhar/vertex_timing.py /private/path/moon-detailed-01.json
```

The reader rejects inconsistent counts, budgets, clock domains and brackets.
It reports exclusion reasons separately and returns null timing totals for empty
accepted populations or unmeasured interior phases. Its output is not a release
qualification report or an estimate of full-run stage shares.

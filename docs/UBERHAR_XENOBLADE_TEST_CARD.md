# Xenoblade Chronicles 3D comparison test

<!-- CodexAstraLocal: Owner added Xenoblade on 2026-10-22 as a demanding New 3DS workload with an initial 2x target. This card adds suite coverage, not automatic permission to run games. -->

Xenoblade Chronicles 3D is part of the performance suite alongside Dark Moon,
Fire Emblem Awakening and Sonic. Its current target is **2x internal
resolution**. Keep 4x qualification deferred until 2x results justify it; this is
a title-specific staging decision, not a change to the broader 4x objective.

<!-- CodexAstraLocal: Owner clarified cadence to avoid replaying a long opening for every small alpha change. -->
Use Xenoblade as a periodic stress test every few builds, when another game's
results show a notable or statistically significant performance change, or when
the owner selects it for a particular build. Include at least the cutscenes in
an authorized agent-run assessment; the lengthy opening is not mandatory for
every build. The initial baseline below includes the battle where feasible.

The owner reports that the opening cutscene's transition into battle can fall to
approximately 33% speed, and that stock Azahar does not sustain 4x. These are
owner observations, not newly captured measurements or proof of a CPU/GPU cause.
The owner ended that playthrough before capture began and will restart later.

## Repeatable scope

<!-- CodexAstraLocal: Separate workload transitions so the easy cutscene cannot conceal the battle slowdown. -->

| Phase | Use |
|---|---|
| Opening cutscene | Matched starting scene and pre-battle demand |
| Transition into battle | Compilation, loading, queue and synchronization bursts |
| First battle | Primary sustained stress comparison and five-second lows |

Use the same replay/save, battle section and approximately comparable camera and
actions. Record where the owner begins and ends each section. Preserve Xenoblade
saves; the standing permission to reset Dark Moon File 1 does not apply here.
Exit normally after the measured segment. The owner controls gameplay unless
they explicitly delegate this particular test.

## Initial comparison

<!-- CodexAstraLocal: Warm stock gameplay is the intended comparison for cold Uberhar; driver experiments must not replace the stock reference. -->

<!-- CodexAstraLocal: Owner explicitly selected all five configurations at both caps for the initial baseline. -->
| Application / mode | Use CPU vertices in Combo | Cache treatment | Speed conditions |
|---|---|---|---|
| Uberhar / Virtual PICA: native rasterization | Not applicable | Cold | Turbo ON 400%, then OFF 100% |
| Uberhar / Virtual PICA: automatic combination | ON | Cold | Turbo ON 400%, then OFF 100% |
| Uberhar / Virtual PICA: automatic combination | OFF | Cold | Turbo ON 400%, then OFF 100% |
| Stock Azahar | Not applicable | Cold | Turbo ON 400%, then OFF 100% |
| Stock Azahar | Not applicable | Warm | Turbo ON 400%, then OFF 100% |

All five configurations use **Vulkan, Internal Resolution 2x, System GPU driver**.
These are ten comparison conditions, not a requirement to run all of them in one
sitting. Match the measured phases at both caps: turbo measures available
throughput; normal speed measures pacing and consistency. Keep cold and warm
results separate. Clear the applicable shader cache before each cold run, and
replay the corresponding stock scene without clearing it for its warm run.
Record any newly encountered stock shader compilation even in a warm replay.

Record every other changed graphics option. Select useful follow-up controls
from the initial measurements rather than multiplying unrelated options into
this baseline. The owner will explicitly notify root before capture begins.

Use Vulkan and matching guest model, GPU-timing simulation, CPU clock, graphics
features and internal resolution. Keep the Thor power profile, charging state,
fan setting and starting temperatures comparable. Check for the known Wi-Fi
background CPU fault. Verify the loaded driver name/version, rather than only
the UI selection. Confirm identical intact game content before a new comparison;
the previously damaged Uberhar Xenoblade copy has now been replaced and verified.

Normal/default speed and Turbo Speed Limit are separate from internal resolution.
Keep normal speed at the owner's configured 100% and turbo at 400% unless changed
explicitly. A new game should start with turbo OFF. The owner can enable turbo
for headroom measurements and toggle it to mark transitions. Record actual
transitions; do not invent markers for an unmarked phase. Keep in-game fast-forward
separate from emulator turbo.

## Capture and interpretation

<!-- CodexAstraLocal: Measure useful output and per-frame demand; aggregate busy percentages alone cannot assign a bottleneck. -->

- Root alone owns capture and verifies it is advancing before gameplay. Continue
  until explicit owner completion, without an invented duration or sample cutoff.
- Retain complete ordinary logs, final diagnostics, settings/version, phase
  markers, brief visuals, app CPU core-equivalents, per-core system utilization,
  available thread attribution, GPU busy/frequency, memory and thermal conditions.
- Lead with the slowest complete five-second scene windows, then average/best
  windows and recurring stalls. Report speed only at the precision supported by
  each emulator's telemetry; sparse stock screenshots cannot establish a
  continuous mean or five-second low.
- Separate shader-compilation bursts from steady gameplay CPU demand. Compare
  cold Uberhar against warm stock in the same phase; retain cold stock separately
  when useful for compilation behavior.
- Correlate slow phases with CPU preparation, shader selection, GPU commands,
  uploads/copies, queue submission, barriers and waits where existing diagnostics
  expose them. Missing timings remain missing, not inferred measurements.
- Use output rate and GPU frequency alongside busy percentage. A GPU at 100%
  producing twice as many comparable frames is doing more useful work; the
  objective is lower GPU cost per frame, not low occupancy by itself.
- The observed 33% speed does not by itself establish that Xenoblade pressures
  the same resource as Dark Moon. Identify CPU execution, GPU work, driver work
  or synchronization pressure before choosing a change.

## Optional Turnip comparison

<!-- CodexAstraLocal: Driver changes may alter compiler costs and enabled renderer paths; retain an exact stock-driver baseline. -->

Use one compatible Turnip build as a separate controlled experiment after the
stock-driver measurements. Record package identity/hash and actual loaded Vulkan
driver. Repeat the same 2x scene and renderer settings, with equivalent cache
treatment and temperature. If the difference is useful, compare stock Azahar
with that same Turnip build to distinguish emulator behavior from driver effects.
Judge five-second lows, throughput, pacing, correctness and stability together.
Do not assume a new driver improves every game or preserves an optimization's
effect. Revert using **Options → GPU Driver Manager → System GPU driver**.

## Current state

The test case is added; the new 2x battle baseline and driver comparison remain
pending owner play. No new capture, driver installation or game run was performed
while writing this card. The separately implemented turbo startup correction
requires a future built APK before it changes behavior on the Thor.

# Uberhar 0.0.10: eight-session device analysis

<!-- AstraEH: Device measurements, unequal-duration caveats and the 0.0.11 response. -->

Source: `uberhar_log_9_24_2157_FEA.txt`, build
`f948671c858149514c0b2035a469c33c7411c177`, diagnostics 7, Fire Emblem Awakening,
Ayn Thor / Adreno 740. The capture contains eight complete renderer sessions:
exactly two each for Native, Compute, Automatic and Custom. Every new-profile
session logs 1x; the custom sessions log 2x. The owner's earlier 2x profile
attempt and any additional Automatic run are not in this file.

| Run | Mode / cache evidence | Resolution | Renderer lifetime (s) | Generic foreground waits (s) | Scheduler waits (s) | Largest individual wait (ms) | CPU vertex stage (s) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Native cold | 1x | 218.74 | 19.052 | 0.091 | 374.102 | 111.966 |
| 2 | Native warm (long interruption) | 1x | 616.31 | 5.279 | 0.001 | 161.105 | 489.080 |
| 3 | Compute cold | 1x | 118.33 | 13.183 | 0.092 | 365.971 | 73.624 |
| 4 | Compute warm | 1x | 98.47 | 6.544 | 0.001 | 359.014 | 63.883 |
| 5 | Automatic cold | 1x | 103.81 | 11.140 | 0.091 | 341.598 | 63.554 |
| 6 | Automatic warm | 1x | 98.41 | 5.802 | 0.001 | 179.411 | 63.775 |
| 7 | Custom TEV+bridge cold | 2x | 100.66 | 0.000 | 21.921 | 1400.161 | not captured |
| 8 | Custom TEV+bridge retained cache | 2x | 82.89 | 0.000 | 12.599 | 570.110 | not captured |

Renderer lifetime runs from construction to the final renderer totals, including
loading and any idle gameplay. The long Native warm session is 616 seconds
(about 10 minutes 16 seconds on this definition). Human absence cannot be located
or subtracted reliably: no interaction marker identifies it, and rendering can
continue while the user is away. The Native cold session also contains more
work/states than the shorter later runs (316,540 draws and 61 generic families
versus approximately 190,000 draws and 37 families). Do not rank the modes by
session length or aggregate waits as if these were equal replays.

Generic foreground waiting and command-worker waiting occur on different threads
and can overlap; the columns are deliberately separate. Worker compilation totals
also overlap and must not be added to these waits. The largest column is the
maximum individual observation across the two wait counters, not a measured
complete frame hitch. No frame-time series or battle-boundary markers exist here.

## Sustained battle slowdown

**Every compute-eligible count is zero.** In all six profile sessions,
`unsupported_state == considered`, `native_draws == considered`, `compute_draws=0`
and both GPU sample counts are zero. Thus this game did not compare native versus
compute execution: all three presets executed the same CPU/native route. There
were no fallback compilation failures or silently skipped draws in any session.
The old aggregate rejection counter cannot identify the blocking state component.

The CPU vertex stage totals 63.883 seconds out of 98.471 renderer seconds in warm
Compute, and 63.775 out of 98.405 in warm Automatic, approximately 65% in each.
The long Native warm session records 489.080 seconds in this stage out of 616.314.
These are stage wall times, including loading/setup, shader execution, geometry
and possible memory synchronization; they are not a CPU utilization measurement.
Input counts include indexed reuse and therefore do not count shader invocations.
The user's sustained half-speed battles at both 1x and 2x, unchanged warm behavior,
and these counters strongly implicate CPU vertex processing. They do not prove
that every lost millisecond is arithmetic in the interpreter.

0.0.10 deliberately disabled the established CPU shader JIT to make a reference
interpreter path. That was too costly for this workload. 0.0.11 restores cached
CPU translation for the profiles, while still avoiding GPU vertex specialization.
This is explicitly a CPU JIT, not the unfinished GPU interpreter and not zero
compilation. New records measure its compilation separately. The original CPU
vertex cache's 64-entry scan is replaced by a fixed index with identical FIFO
behavior. The reference interpreter's three per-invocation heap-allocated control
stacks also become fixed arrays, preserving overflow behavior; that optimization
helps interpreter fallback, not the newly selected JIT itself.

## Why even warm profiles paused

The driver cache loads correctly on every second run. In Native warm, 80 generic
pipelines spend 42.863 ms in the pipeline-build wrapper, while frontend module
work consumes 5,224.571 ms. Automatic warm similarly records 41.182 ms versus
5,749.060 ms. Compute warm still has 1,068.751 ms of pipeline work, including six
slow driver builds; retained driver data is not a guarantee of complete coverage.

0.0.10 recreated generic GLSL/SPIR-V modules on every launch and forced the optional
SPIR-V optimizer on. The custom setup has that optimizer disabled. 0.0.11 skips
that optional pass in the profiles and persists generated generic modules with
source/compiler fingerprints, bounded lengths and checksums. Cache misses still
compile; new driver pipelines can still pause. This does not prepare a universal
bank or eliminate first-playthrough compilation.

## Custom run caveat

Custom cold records 21.921 seconds of scheduler waiting and a 1.400-second maximum.
Its retained-cache run records 12.599 seconds and a 0.570-second maximum, rather
than the zero waits observed in earlier logs. The first run ends with 317 guest
pipeline records / 289 host pipelines; the second ends with 414 / 386, and no live
VS code generation. The cache loaded, but 97 additional host pipelines existed by
shutdown. The log cannot prove whether that reflects additional gameplay states
or an avoidable identity difference. New startup-versus-live counters will make
that distinction more visible; do not describe this as a fully warmed baseline.

## Next device test and roadmap

Use **2x resolution** for future comparisons as requested. Start with one Native
cold/warm pair, including the same battle at normal speed. Report the battle FPS
and whether the remaining pauses improve. The new windowed vertex counters, CPU
JIT records, module-cache hits/misses and per-state compute blockers should identify
which work changed. Custom TEV+bridge remains a useful optional separate baseline.
Repeating all three preset pairs is unnecessary while compute coverage remains zero.

GPU vertex interpretation and broader correct compute rasterization remain the
architectural targets. The next compute expansion must follow measured blockers
and preserve depth, stencil, culling and blending. Screen synchronization and model
presentation remain later items. The scheduled full review is still after 0.0.13
(allowed 0.0.12–0.0.14); this results analysis does not reset that cadence.

<!-- CodexAstraLocal: Close the every-three-build test review with actual delivered controls, retaining incomplete observations and unmet qualification targets. -->
# Test review: 0.1.35–0.1.37

The six authorized Native/Combo controls are complete, one per method per build.
Actual .36/.37 worker execution is substantial, but every retained ordinary-speed
population remains well below the sustained 99% target. Native .37 crosses the
owner's moon-event investigation threshold. These results do not qualify beta,
4x, general graphics or long-play memory behavior.

Corrected .37 source `f6e0d88842ff8204e112a7368035e4565848146d` passed all 27 required
steps across three jobs in [the gated workflow](https://github.com/RegiRex/uberhar/actions/runs/37939663409).
Exact assets, source and compatible signing were verified before installation.
The first attempt's host failure was a test injection that hid `SYS_futex` before
GCC13 standard-library headers needed it. Its repaired fixture and fresh full CI
passed with unchanged runtime source; the failed attempt was not waived.

The [architecture review](UBERHAR_ARCHITECTURE_0.1.37.md) explains decisions and
next hypotheses. The independent [cleanliness review](UBERHAR_CLEANLINESS_0.1.37.md)
records finite source/purpose coverage. This review reports retained outcomes,
not new runs or a causal performance comparison.

<!-- CodexAstraLocal: Define actual measured populations before comparing numeric values; retain the exceptional Native cutoff and incomplete Combo observer. -->
## Conditions and scope

Each control used Dark Moon File 1 Empty, complete zero application shader-cache
inventory, Vulkan 2x, 100% normal limiter, CPU 100%, shader JIT and accurate multiply.
Driver-private caches were not proven cold. Source, settings, title session,
normal return, route closure and zero reported skipped draws/required fallback
failures pass the ordinary evidence checks. Frame, vertex and cumulative-counter
windows have independently qualified boundaries; missing optional records are not
filled with an assumed five-second cadence.

.35 and .36 used separately recorded process/condition histories. In .37, Combo
reused Native's app process under a new title session. Battery temperature,
power, observation and visible scene progression differ; battery temperature is
not SoC/GPU temperature, and endpoint frequency is not residency.

Native .37 needs a special scope warning. Unexpected operator latency after its
second CPU capture left a prolonged later dialogue wait. Before speed outputs
were inspected, the analysis end was fixed at that capture's final timestamp,
175.135036 seconds after A. The table uses only complete reporting windows inside
that declared observation. It is not a fabricated Back or a matched complete
opening. Actual A-to-Back was 711.990926 seconds; the full-span mean of 35.404240%
is descriptive only and includes the wait.

Combo .37's actual A-to-Back was 215.382332 seconds. Its first CPU wrapper was
interrupted after three raw snapshots without a complete host manifest. It was
not repeated or reconstructed. Only the second five-snapshot capture qualifies
CPU activity. The canonical known-observer subset therefore has an incomplete
observer limit; a separate conservative subset excludes the entire declared
containing scheduling-endpoint envelope. This does not label its sleep gaps as
continuous sampling or estimate observer cost. All navigation after actual Back,
including a dismissed multiplayer sheet without joining, is excluded.

## Ordinary speed and CPU vertex wall

|Control and scored scope|Complete frame windows /wall seconds|Mean normal speed|Wall-weighted p05 of window averages|Worst reported interval|
|---|---:|---:|---:|---:|
|.35 Native, A-to-Back|49 /246.739490|27.664270%|17.581%|203.714 ms|
|.35 Combo, A-to-Back|40 /201.481499|28.279028%|22.073%|392.377 ms|
|.36 Native, A-to-Back|51 /256.619690|30.500411%|17.553%|205.410 ms|
|.36 Combo, A-to-Back|43 /216.482050|30.411427%|19.412%|403.560 ms|
|.37 Native, declared bounded observation|34 /171.085689|32.082040%|23.417%|204.200 ms|
|.37 Combo, A-to-Back|42 /211.153218|31.978393%|21.239%|385.032 ms|

All complete windows in these primary populations are below both 95% and 99%.
The p05 is a reporting-window statistic, not a frame percentile. Recorded scene
phase counters remain unknown; title speed does not establish which actor or
rendering route produced a visual event.

.37 Native's no-video/no-observer subset has seven windows/35.276995 seconds,
mean 28.142853%, p05 23.417%, worst 78.156 ms. Combo's conservative subset has ten
windows/50.294426 seconds, mean 31.470598%, p05 21.239%, worst 88.823 ms. Different
subsets have different scene populations and are not matched controls. Native's
204.200 ms maximum may overlap moon capture; Combo's 385.032 ms report may overlap
a scheduling endpoint. Neither overlap establishes cause. The .36 Combo maximum
remains in its no-video/no-observation subset.

Inclusive LoadVertices wall shares in the separate vertex windows are:

|Control|.35|.36|.37|
|---|---:|---:|---:|
|Native|79.434151%|60.882309%|63.793198% bounded|
|Combo|68.113714%|52.086447%|51.590241%|

For .37, Native has 108.666759/170.342234 seconds across 33 vertex windows;
Combo has 108.333681/209.988708 seconds across 40. This bracket includes setup,
input, JIT, conversion, FIFO/assembly and synchronization. It ends before Vulkan
upload/submission and is neither exclusive shader CPU time nor a frame budget
whose remainder measures GPU time. The table does not establish causal speedup.

<!-- CodexAstraLocal: Count useful completed work with the exact remaining-CPU denominator and keep finite sampled clocks separate from time attribution. -->
## Actual parallel work and phases

.35's initial full-arithmetic certificate admitted only about 2.10% of Native and
0.40% of Combo completed attempted CPU invocations. .36's explicitly guarded
selected-output contract raised that to 92.10% and 93.03%. .37 retains broad reach:

|Contained .37 cumulative population|Native bounded|Combo opening|
|---|---:|---:|
|Checks|1,843,200|1,879,470|
|Completed certified batches|870,200|794,859|
|Completed attempted invocations|787,390,961|869,544,930|
|Certified invocations|732,204,055|809,479,569|
|Worker invocations|526,762,229|597,908,779|
|Certified /worker share of attempts|92.991168% /66.899705%|93.092322% /68.761114%|

All certified work here uses SelectedOutputValues. Carry, submission-writing,
allocation and startup failures are zero in these spans; unsupported draws retain
serial rendering. Small, observer, mapping and concurrent-memory refusals remain
explicit. Input recipe coverage is 86.402187% Native and 86.017650% Combo in its own
contained report populations, with zero legacy invocations there. These counts
are not savings estimates. Combo's ready GPU batches are outside the CPU invocation
denominator; GPU shader invocations are unknown.

The actual transport reports `futex`, available eight CPUs, seven created workers
and peak eight participants. Those are gauges, not utilization. Sparse stride-257
measurements contain only the selected completed draws and their chunks:

|.37 sample|Native bounded|Combo opening|
|---|---:|---:|
|Draws /chunks /invocations|3,386 /3,729 /2,919,694|3,093 /3,637 /3,496,975|
|Plan /pool /submit wall|32.153 /251.396 /65.486 ms|38.497 /283.550 /72.438 ms|
|Pool share of sampled outer wall|72.026%|71.879%|
|Nested owner /join /other pool wall|135.753 /58.146 /57.497 ms|145.390 /70.472 /67.688 ms|

These deterministic samples are not multiplied by 257 or extrapolated to the
whole title. Owner and join are nested within pool. Remaining pool time includes
multiple operations and is not isolated wake cost; join is not worker CPU time.
Preparation/admission before the chunk clocks is excluded.

## Thread activity, scheduling and cleanup

Seven stable named workers account for 0.08/0.12 CPU-seconds in .35 Native's two
roughly 20-second windows and 0.01/0.16 in Combo's. In .36 the corresponding values
are 13.68/13.80 and 10.18/15.84. The .37 captures show:

|Finite .37 CPU capture|Seven workers CPU-seconds|Dominant owner CPU-seconds|Whole-process CPU-seconds|
|---|---:|---:|---:|
|Native first|13.95|13.37|29.68|
|Native second|17.44|17.80|38.76|
|Combo second; only qualifying capture|15.62|17.26|38.45|

Exact task generations and actual interval bounds are retained. Process CPU
already includes workers and owner. These are finite accounted core-time samples,
not exclusive shader work, whole-opening CPU use, homogeneous processor capacity
or continuous use of seven cores. Opaque GPU busy windows are device-wide and not
aligned with CPU intervals; high readings cannot establish title GPU saturation.

Separate wider scheduling endpoints show many short worker timeslices and runnable
queue delay. Native's roughly 43.8-second span has 31.618 seconds worker runtime,
19.670 seconds summed runnable wait and 1.184 million timeslices; Combo's roughly
41.7-second span has 21.644 seconds, 13.825 seconds and 0.922 million timeslices.
These spans are not the 20-second CPU captures. Waits can overlap between tasks;
timeslices are not identified wake calls/jobs, and totals divided by counts are
not wake-latency quantiles. Endpoint masks/nice/top-app membership do not establish
residency or rule out contention. No scheduler policy was changed.

One required Combo .37 ownership observation was retained 40.095938 seconds after
verified normal return, in the same process. KGSL had recovered from the last
observed during-run 3170.917969 MiB to 13.339844 MiB; RSS from 1027.687500 MiB to
483.949219 MiB. This demonstrates later recovery only, not immediate release,
peak usage or extended-growth safety. Native had no required post-exit memory
sample. .37 Native's nominal preopening temperature sample was several minutes
before A; it must not be relabeled immediate pre-A temperature.

<!-- CodexAstraLocal: Score each retained consecutive-frame population independently and preserve the owner's threshold without assigning an unsupported cause. -->
## Temporal graphics

All retained moon and ghost clips received consecutive-frame review. Counts below
refer to captured frames and discrete observed events, not matched scene exposure.

|Control|Moon reviewed frames /events|Ghost reviewed frames /events|
|---|---:|---:|
|.35 Native|645 /4 isolated singles|649 /3 isolated singles|
|.35 Combo|629 /0|623 /0|
|.36 Native|540 /3 isolated singles|734 /7: six singles and one three-frame event|
|.36 Combo|530 /0|661 /0|
|.37 Native|536 /12 events, 13 affected frames, maximum two consecutive|733 /6 isolated singles|
|.37 Combo|548 /0|639 /0|

Native .37 exceeds the owner's moon threshold of more than five events. None of
its events exceeds two consecutive frames. Ghost corruption remains a separate
review obligation; .36 Native's three-frame disturbance lasts about 184 ms to the
next clear captured frame. No sustained earlier dense irregular ghost pattern
was identified in these reviewed populations, which is not proof of its absence
in unobserved phases.

The .37 clips cover later wide/electrical/rear/broom/bucket/book phases; earlier
close E. Gadd/near-ghost phases are absent. Other builds end at different scene
points. Different counts therefore do not establish version/mode fault-frequency
changes. No CPU batching, stream, renderer or capture cause is assigned.

<!-- CodexAstraLocal: Report the later bounded source-order witness separately from actual device and pixel evidence. -->
A separate forced-wrap host model executes twelve exact production methods plus
the CPU upload/draw and selected binding statements. Its seven cases and 128
assertions reproduce a current-tick flush leaving the following draw without a
render pass or pipeline in the new command buffer; older-tick controls pass.
Queue/completion and Vulkan endpoints are modeled, payloads are a repeated byte
pattern, and no actual driver or title occurrence is proven. This supports a
conditional source defect and separate correction review, not a diagnosis
of these clips or a correction already present in 0.1.37.

## Completion and remaining limits

Local host/A64 correctness, merged real input/JIT/FIFO/hardware-output controls,
mandatory release gates and the six finite device controls are complete. The
source reviews preserve exact output/carry contracts, exception drains and
ownership boundaries; they do not prove every driver interleaving or title.
Lifetime route/owner totals include menus and exit and are not substituted for
opening spans. Optional logging omissions, the interrupted observer and unequal
scene populations remain explicit.

The .37 Combo lifecycle closes 8,666,060 graphics draws across generic, required
recovery and both optional fragment routes. Its CPU compute attempts all reject
state; there is no guest compute admission/dispatch. No new Calculated device
control was performed in this batch. Private renderer and ARM experiments remain
outside the delivered source until separately qualified.

Useful multicore execution is established; sustained normal speed, smoothness and
complete graphical correctness remain unmet. The architecture audit ranks the
next bounded investigations without selecting an untested change or masking the
Native moon priority.

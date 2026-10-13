# Uberhar Claude test branch

This branch (`claude/wizardly-newton-14cvfa`) carries the changes from the
never-skip audit and the multi-core plan on top of Uberhar, currently merged up to
0.1.46 from `uberhar/hybrid-shaders`. It only reads that branch and never pushes
to it.

## Getting a test APK

Every push to a `claude/**` branch runs the **Uberhar Claude test APK** workflow
(`.github/workflows/uberhar-claude.yml`):

1. Open the repository's **Actions** tab and pick the newest run of
   *Uberhar Claude test APK* for this branch.
2. When the run is green, use the **Download the Uberhar Claude APK** link at the
   top of the run page, or the `uberhar-claude-<version>-<run>-arm64.apk` artifact
   at the bottom. It downloads the APK file itself; there is nothing to unzip.
   Downloads need a GitHub sign-in in the browser and expire after 30 days.
   The checksum, signature and source commit are in the separate
   `uberhar-claude-apk-details` artifact.
3. Install it on the phone. It is a separate app called **Uberhar Claude**
   (`org.uberhar.uberhar_emu.claude`), so it sits next to Uberhar and never
   replaces it, its signing key or its data.
4. On first launch choose a new, empty data folder such as `UberharClaude`.
   Copy keys, games or settings into it if you want them; sharing Uberhar's folder
   would let the two builds overwrite each other's caches.

The workflow also runs the shader/pixel gate, the native C++ tests and the ARM64
contract tests. The APK is built alongside them, so only install an APK from a
run where all three jobs (`shaders`, `native-tests`, `android`) are green.

If the run summary warns about a new signing key, uninstall the previous
Uberhar Claude build before installing the new one. Uberhar itself is unaffected.

## What changed

### Draws are never skipped

| Audit item | Change |
|---|---|
| D1: async shader compilation dropped draws | The Vulkan graphics path always waits for its pipeline. The async setting is logged and ignored. |
| D2: "Disable right eye rendering" skipped whole command lists | The disabler is never enabled. The setting is logged and ignored. |
| D3: a draw with no render target left its CPU vertices queued for the next draw | Those vertices are discarded with the draw. |
| D4/D5: invalid input and missing targets were uncounted | Every draw request is counted in a **draw ledger**. |

The ledger is logged every 30 seconds and at shutdown:

```
Claude draw ledger progress: requests=… graphics=… compute=… empty=… no_target=… invalid_input=… skipped=… render_passes=…
```

`skipped` must stay 0. Any nonzero value is also logged as an error.
`no_target` and `invalid_input` count draws the guest asked for that have nothing
to draw into or no valid vertex data; they are reported, not hidden.
`render_passes` counts Vulkan render passes begun. Each pass costs a tile load
and store on a phone GPU, and that cost grows with internal resolution, so
draws per pass is the number to watch when comparing 2x and 4x.

### Presets

* Native, Automatic, ComboGeneric and Compute presets turn on **simulated 3DS GPU
  timings**, so every mode is measured the same way.
* Native no longer creates the compute rectangle helper, so it does no GPU compute
  work at all.

### Fewer threads

Vertex batches and parallel texture decoding used separate worker pools, each
up to one thread per core. They now share one pool (threads named
`UberharWorker`). Both run on the emulator thread one after the other, so no
throughput is lost; on an 8-core phone this removes up to 5 idle threads.
Texture decoding still leaves two cores of headroom per job.

The vertex log line gains `pool_busy_fallbacks=`; it should stay 0. A nonzero
value means two jobs tried to use the pool at once and one ran on its own thread.

The CPU draw executor keeps its own pool; it runs on a separate coordinator
thread and will move into a shared job system later.

### Less waiting on the emulator thread

Small and medium CPU draws (96–255 vertices) are queued for the CPU draw pool.
In 0.1.45, before the next ordinary draw could run, the emulator thread waited
for every queued packet to finish. Now an ordinary List draw that cannot observe the queued
packets runs right away; its Vulkan commands are still recorded after theirs.
The draw joins first, as before, whenever it could observe them: other
topologies, geometry shaders, partial triangles, debugging, timing capture,
interrupts, the end of a command list and save states.

The `Uberhar deferred CPU draws` log line gains `serial_overlaps=`, the number of
draws that ran without waiting. `boundaries=` counts the joins that remain.
Each remaining join is the `Uberhar: join before serial draw` trace slice.

### Fragment specialization

The demand gate that decides when a fragment configuration has been used often
enough to get its own specialized shader was direct-mapped. Two busy
configurations whose hashes landed in the same slot reset each other on every
draw, so both stayed on the slower generic shader forever. It is now 4-way
set-associative with least-recently-used replacement, using the same 64 entries.

### Combo uses CPU vertices by default

"Use CPU vertices in Combo" now defaults to on. In the 0.1.44 Thor runs it gave
the best slowest window (63.6% against 41.0% with GPU vertices), and it keeps the
GPU on required graphics. Turn it off in the renderer settings to compare.

### CPU Software renderer uses every core per draw

The CPU Software renderer used to split each triangle's rows across workers,
one fork-join per triangle. Most 3DS triangles are small, so the wake-and-join
cost was larger than the work: on a 4-core test host, 4,000 small triangles
took 38 ms that way against 32 ms on one core (0.1.46 code; 76 ms against 61 ms
on 0.1.45).

Now a draw's triangles are queued and drawn together at the end of the draw.
Each worker owns whole 8-row bands of the screen and draws every triangle in
its original order inside them, so every pixel sees exactly the same sequence
as before and no two workers touch the same pixel. The same draw takes 16 ms
on 0.1.46 (26–31 ms on 0.1.45; `[.software-bench]` in the C++ tests). A test
checks the output against the one-core path byte for byte. Band workers share
the draw's texture preparation and 0.1.46's TEV decode instead of redoing them.

A draw keeps the old immediate path when its vertex or index data could
overlap its color or depth buffer (a later vertex might read pixels this draw
just wrote), or when its textures overlap its own buffers. Triangles still
split their rows when drawn one at a time, now in 16-row pieces, so small
triangles stay on one thread.

### CPU clocks follow frame time on Android 13+

The emulator reports each frame's work time against the 3DS frame time (at the
current speed limit) through Android's performance hint API. The kernel can then
raise CPU clocks for the emulator thread, the Vulkan recording thread and the
CPU worker pools before frames run late, instead of reacting after several
slow frames. The log shows `Claude: performance hint session threads=N` when it
is active, and traces gain an `Uberhar: frame work us` counter. Older Android
versions skip this.

## Capturing a trace

The Claude build is *profileable*, and on Android it marks its phases as
Perfetto/systrace slices:

* the existing profiler scopes: `ARM JIT`, `GPU: Cmdlist Processing`,
  `GPU: DisplayTransfer`, `RasterizerCache: UploadSurface`, `Vulkan: Pipeline Bind`,
  `Vulkan: Pipeline Building`, `Vulkan: Submit Exectution`, `Vulkan: Swapchain Present`
  and others;
* `Uberhar: vertex batch`, `Uberhar: pool job` (on each worker),
  `Uberhar: parallel texture decode`, `Uberhar: CPU draw packet`,
  `Uberhar: CPU draw drain`, `Uberhar: join before serial draw`,
  `Uberhar: software band draw`, `Uberhar: swap buffers`;
* counters `Uberhar: draws recorded`, `Uberhar: draws skipped`,
  `Uberhar: render passes`, `Uberhar: CPU draw wave`, `Uberhar: frame work us`.

Per-vertex, per-triangle and per-SVC scopes are left out so tracing does not
distort timing. When no trace is recording each marker costs one check.

To record from a computer with `adb`:

1. Open <https://ui.perfetto.dev>, choose **Record new trace**, connect the phone.
2. Under **CPU**, enable scheduling details and frequency.
   Under **GPU**, enable frequency if offered.
   Under **Android apps & svcs**, enable *Atrace userspace annotations*, select the
   `gfx`, `view` and `sched` categories, and add `org.uberhar.uberhar_emu.claude`
   as an app to trace.
3. Start recording, play the scene for 10–20 seconds, stop, and save the trace.

The phone's own **Settings → System → Developer options → System Tracing** app can
also record. Whether it includes app slices depends on the Android version.

A trace shows which thread is busy, which cores it runs on, where the emulator
thread waits (pipeline builds, drains, presents) and how often the workers
actually run.

## Deliberately not changed yet

* **Specialized fragment shader budget.** Native and Combo stop compiling new
  specialized fragment shaders after 64 attempts per session, so later scenes stay
  on the generic shader. The owner's tests treat that cap as intentional, and the
  allocation review tied extra driver memory to pipeline population. Driver
  pipeline memory is not visible to the allocator, so a byte budget cannot be
  measured from the app yet. A KGSL memory reading per pipeline build on the Thor
  would show whether the cap can safely become a rolling budget.
* **One job system for every pool.** Vertex batches and texture decoding share one
  pool. The CPU draw executor and the Software renderer keep their own because
  they run on other threads or need their own participant setting. Merging them
  needs a pool that runs several fork-joins at once.
* **Asynchronous PICA thread.** Moving command-list processing off the CPU
  emulation thread is the largest remaining multi-core step, but it changes
  guest-visible timing and memory coherency everywhere. It needs device testing
  across many games and should start behind a setting that is off by default.
* **Small-draw batching for the CPU draw pool.** Draws under 96 vertices still
  run on the emulator thread. Combining consecutive small draws into one packet
  needs per-draw state capture inside the packet.
* **Shader compiler threads.** Pipeline and shader compilers keep half the cores
  each. Draws now wait for their pipelines, so fewer compile threads would make
  those waits longer.

## Checking a build on the phone

1. Install the APK and pick an empty data folder.
2. Play the usual test scene with the usual settings.
3. In the log, check `Claude draw ledger ... skipped=0`,
   `pool_busy_fallbacks=0`, and the `serial_overlaps=` count in
   `Uberhar deferred CPU draws`.
4. Compare speed with Uberhar 0.1.45 on the same scene. For Combo, compare with
   "Use CPU vertices in Combo" on (the new default) and off.
5. If a scene looks wrong, note the mode and scene, and capture a trace if you can.

# Uberhar Claude test branch

This branch (`claude/wizardly-newton-14cvfa`) starts from Uberhar 0.1.45 and carries
the changes from the never-skip audit and the multi-core plan. It never touches
`uberhar/hybrid-shaders`.

## Getting a test APK

Every push to a `claude/**` branch runs the **Uberhar Claude test APK** workflow
(`.github/workflows/uberhar-claude.yml`):

1. Open the repository's **Actions** tab and pick the newest run of
   *Uberhar Claude test APK* for this branch.
2. When the `android` job is green, download the **uberhar-claude-apk** artifact
   at the bottom of the run page. GitHub delivers artifacts as a zip; unzip it to get
   `uberhar-claude-<version>-<run>-arm64.apk`.
3. Install it on the phone. It is a separate app called **Uberhar Claude**
   (`org.uberhar.uberhar_emu.claude`), so it sits next to Uberhar and never
   replaces it, its signing key or its data.
4. On first launch choose a new, empty data folder such as `UberharClaude`.
   Copy keys, games or settings into it if you want them; sharing Uberhar's folder
   would let the two builds overwrite each other's caches.

The workflow also runs the shader/pixel gate, the native C++ tests and the ARM64
contract tests. A red run produces no APK.

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
Claude draw ledger progress: requests=… graphics=… compute=… empty=… no_target=… invalid_input=… skipped=…
```

`skipped` must stay 0. Any nonzero value is also logged as an error.
`no_target` and `invalid_input` count draws the guest asked for that have nothing
to draw into or no valid vertex data; they are reported, not hidden.

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

## Capturing a trace

The Claude build is *profileable*, and on Android it marks its phases as
Perfetto/systrace slices:

* the existing profiler scopes: `ARM JIT`, `GPU: Cmdlist Processing`,
  `GPU: DisplayTransfer`, `RasterizerCache: UploadSurface`, `Vulkan: Pipeline Bind`,
  `Vulkan: Pipeline Building`, `Vulkan: Submit Exectution`, `Vulkan: Swapchain Present`
  and others;
* `Uberhar: vertex batch`, `Uberhar: pool job` (on each worker),
  `Uberhar: parallel texture decode`, `Uberhar: CPU draw packet`,
  `Uberhar: CPU draw drain`, `Uberhar: swap buffers`;
* counters `Uberhar: draws recorded`, `Uberhar: draws skipped`,
  `Uberhar: CPU draw wave`.

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

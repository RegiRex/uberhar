<!-- CodexAstraUlt: Local USB readiness instructions; this is not remote access or a full game runner. -->

# Thor USB readiness on Nobara

Run these steps on your Nobara laptop. This helper checks that the laptop can read
the Thor's identity and the installed Uberhar version. It does not run a game,
install an APK, change phone settings, or connect the cloud session to your laptop.

1. Install the ordinary local tools:

   ```bash
   sudo dnf install android-tools python3
   ```

2. Connect the Thor with a USB data cable. Enable **Developer options → USB
   debugging** on the Thor. If Developer options is hidden, tap **Build number**
   seven times in the device's About screen. Unlock the Thor for its authorization
   prompt.

   <!-- CodexAstraUlt: USB role switching is not a prerequisite for ADB readiness. -->
   You do **not** need to switch **USB controlled by** to **Connected device**.
   That setting concerns USB roles; the useful check here is whether the laptop
   lists the Thor as an authorized ADB device. Leave the role selection alone if
   the Thor cannot switch it.

3. On the laptop, run:

   ```bash
   adb devices -l
   ```

   This intentionally starts the local ADB server if needed. Accept the USB
   debugging authorization on the Thor for your laptop, then run the command
   again. The device's state should be `device`. `unauthorized` means the device
   prompt still needs approval. If it says `no permissions`, resolve the laptop's
   USB/udev access before continuing; this probe does not change permissions.
   An empty list calls for checking the data cable, USB port and USB debugging.
   Keep the Thor unlocked while checking for the authorization prompt. Do not
   use `sudo adb`, disable SELinux, or expose an ADB port publicly to fix this.

4. Save `device_probe.py` on the laptop, open a terminal in its folder, and run:

   ```bash
   python3 device_probe.py
   ```

   When more than one device/emulator appears, choose the exact serial shown by
   `adb devices -l`:

   ```bash
   python3 device_probe.py --serial YOUR_THOR_SERIAL
   ```

   Optional: also create a local report file. An existing file is preserved.

   ```bash
   python3 device_probe.py --serial YOUR_THOR_SERIAL --output thor-readiness.json
   ```

Success returns `status: "ready"`, manufacturer/model, Android API and the
version name/code of `org.uberhar.uberhar_emu`. Exit code 0 means these checks
passed; exit code 1 includes a short reason. `package_missing` means this package
is not installed for the queried user. It is reported without installing anything.
The JSON includes the selected ADB serial; share only where you intend that device
identifier to be visible. It does not contain authentication keys or the raw package
dump.

The probe requires an **already-running local ADB server on 127.0.0.1:5037**.
It ignores custom/remote ADB server environment settings. It invokes only
`adb version`, then sends fixed read-only ADB smart-socket requests to that local
server. This avoids implicit server starts/restarts by normal ADB client commands.
It never runs `adb connect`, pairing, network setup, `start-server`, `kill-server`,
installation, game-launch or phone-write commands. Each request has a default
five-second timeout and device replies are capped at 128 KiB.

This is **device readiness only**. A private cloud-to-laptop connection is not
created by this helper. USB authorization does not by itself create such a
connection. Remote automation, a repeatable game/input sequence, captures and
controlled comparison runs still need setup. The separate
[local Codex handoff](LOCAL_CODEX_HANDOFF.md) concerns an agent running on your
laptop; this cloud chat does not
automatically control it.

To verify the helper itself locally without a phone, run:

```bash
python3 test_device_probe.py
```

The tests use a temporary fake `adb` executable and an ephemeral loopback test
server. They never contact your device or your real ADB server.
Real Thor USB communication has not been validated from this cloud environment.

## Download without the chat bundle

<!-- CodexAstraUlt: Ordinary repository files replace the inaccessible chat-only bundle; no file is downloaded or executed automatically. -->

Open [device_probe.py](device_probe.py) in GitHub and use its **Raw** download,
then run it with Python as above. The [handoff](LOCAL_CODEX_HANDOFF.md) and
[tests](test_device_probe.py) are also ordinary files in this directory.

Alternatively, in a new local folder, download the probe from the project's
release branch:

```bash
curl --fail --location --proto '=https' --output device_probe.py \
  'https://raw.githubusercontent.com/RegiRex/uberhar/refs/heads/uberhar/hybrid-shaders/tools/uberhar/device_testing/device_probe.py'
```

This downloads a file; inspect it before running `python3 device_probe.py`.
The repository URL becomes available after the commit containing these tools is
published to `uberhar/hybrid-shaders`; an unpublished draft link can return 404.

## Passive memory observations

<!-- CodexAstraLocal: Define the fixed external observer and its missing-bandwidth contract without changing emulator runtime. -->

`memory_probe.py` records device-global memory pressure and occupancy through the
same existing local ADB socket client. It requires an explicit serial and a new
output directory. Keep it beside `device_probe.py`, or use `--repo PATH` to select
that file from a checkout. One operator must serialize it with other device work.
It does not start ADB, wake a screen, modify settings or device files, invoke a
profiler, or retry through another transport.

```bash
python3 tools/uberhar/device_testing/memory_probe.py \
  --serial YOUR_DEVICE_SERIAL --output NEW_PRIVATE_CAPTURE_DIRECTORY \
  --samples 45 --period 5
```

The fixed read order is `/proc/uptime`, `/proc/pressure/memory`, `/proc/vmstat`,
`/proc/meminfo`, then `/proc/uptime`. Per-file caps and return codes, total reply
limits, a remote timeout and the socket deadline bound each attempt. The schedule
allows 2–61 samples, 5–30 seconds apart, at most 300 seconds between first and last
scheduled reads. An optional initial delay is at most 60 seconds. Missed slots
remain missing; failed transport stops collection without catch-up or retries.

The report preserves PSI cumulative stalled microseconds and its kernel rolling
averages; selected vmstat counter increments; and meminfo kB gauges converted to
bytes. These are global observations, not app-only accounting. Missing counters,
permission errors, malformed replies, uptime resets and decreasing counters stay
explicit. Overlapping memory gauges must not be summed. PSI is pressure, and free
RAM is occupancy; neither measures memory-controller traffic.

`ddr_bytes`, `ddr_bytes_per_second`, `bandwidth_percent`, CPU cache events and CPU
memory-stall events are always null with an unavailable reason. No validated PMU
or DDR reader is present. Frequency, requested bandwidth votes, inferred page
traffic and theoretical peak throughput must not fill those fields.

Uptime readings conservatively enclose each non-atomic batch, including printed
quantization. Host timestamps retain failed-query intervals too. Join them to an
explicit app lifecycle before comparing game windows, and treat every observer
interval as potential overhead. The helper does not prove boot/process identity,
exact event/frame alignment, or a performance cause. Raw files contain the serial
and should remain private. Reply hashes describe the existing client's decoded
UTF-8 text; arbitrary non-UTF8 wire bytes are not preserved.

`passive_observed` requires at least one usable adjacent time enclosure and one
passive source. It does not assert all scheduled reads succeeded. Consumers must
retain requested/actual counts, every file's status, missed slots, report reasons
and raw hashes. The [Linux PSI interface](https://docs.kernel.org/accounting/psi.html)
and [proc documentation](https://docs.kernel.org/filesystems/proc.html) define the
pressure and occupancy fields; they do not provide DDR bandwidth utilization.

The host gate uses only synthetic replies and a fake clock/client; it performs no
ADB request or socket connection:

```bash
python3 tools/uberhar/device_testing/test_memory_probe.py
```

Each invocation creates a separate result directory under
`build/uberhar-probe/passive-memory/`, retaining exact source and parser/schedule
controls. The existing LocalAdb wire implementation has separate tests.

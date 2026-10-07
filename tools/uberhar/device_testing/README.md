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

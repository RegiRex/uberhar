# Uberhar: Nobara laptop and Thor test handoff

<!-- CodexAstraUlt: Transfer verified project context to a local device-testing agent without copying game data or pretending cloud USB/VPN access exists. -->

Updated 2026-10-06 for the owner's normal-prerelease authorization and USB setup
feedback. This file is a handoff, not an executable test runner.

## Where commands run

Run the commands below on the owner's **Nobara laptop**. The earlier ChatGPT
“Configure setup instructions” scripts target the cloud and do not belong here.

```sh
sudo dnf install android-tools python3 nodejs npm git
npm install --global --prefix "$HOME/.local" @openai/codex
"$HOME/.local/bin/codex" --version
```

The Codex npm installation is the method documented in the
[official Codex repository](https://github.com/openai/codex#quickstart).
Launch Codex and choose **Sign in with ChatGPT**. The browser session and the
local CLI are separate sessions; authentication does not connect their agents or
give this cloud session access to the laptop. Account limits still apply.

Enable USB debugging in the Thor's Developer options, connect it using a USB
data cable, unlock it, and approve the laptop's USB-debugging prompt. Run:

```sh
adb devices -l
```

The Thor must appear with state `device`. `unauthorized` requires approval on
the Thor; an empty list or `no permissions` needs cable/USB/udev diagnosis. Do
not use `sudo adb`, disable SELinux, or open network ADB as a workaround.
The Thor's **USB controlled by** selection does not need to switch to **Connected
device** for this check. Follow the [readiness instructions](README.md) and use
`adb devices -l` to determine whether USB debugging works.

For a new local checkout, choose an unused directory:

```sh
git clone --branch uberhar/hybrid-shaders https://github.com/RegiRex/uberhar.git uberhar-device-testing
cd uberhar-device-testing
python3 tools/uberhar/device_testing/device_probe.py
"$HOME/.local/bin/codex"
```

Existing checkouts should be inspected first, not overwritten or hard-reset.
Building locally may require submodules and the repository's pinned toolchains;
the first device test can use an already-validated APK.

Give the local session this handoff and the prompt below. Keep the game on the
Thor. No Proton Drive transfer is necessary for testing on that device.

## Prompt for the local Codex session

<!-- CodexAstraUlt: This prompt constrains local device work to reproducible Uberhar tests and preserves the owner's existing data and concurrent development branches. -->

Continue the owner's Uberhar testing work from this handoff. First read AGENTS.md
and the current project progress, diagnostics, code map and release notes. Use
the connected Ayn Thor Max through USB ADB, explicitly selecting its serial when
multiple devices are connected. Inspect the installed app and USB readiness
before making device changes. The package is `org.uberhar.uberhar_emu`.

The owner wants agents to build, review, test and iterate with less manual work.
Prioritize Luigi's Mansion Dark Moon's Combo rendering faults and unexplained
system-memory decline. Use actual Vulkan on the Thor for performance conclusions.
Keep the game, saves, keys and personal device data out of the public repository.
Read only the device/app data needed for this task. Do not root the Thor.

<!-- CodexAstraUlt: Replace the earlier comparison-only restriction because the owner authorized normal repository prereleases; preserve source-branch integration as a separate review. -->
The owner now authorizes normal GitHub prereleases in `RegiRex/uberhar`; their
parallel experiments will use a side branch. The tested release workflow runs
from `uberhar/hybrid-shaders`. The earlier comparison branch was
`uberhar/codexastra-diag-comparison`, and comparison PR #1 already exists. Inspect
current refs and coordinate ownership before pushing; never force-push over
other work or create a duplicate PR.

The owner confirmed normal prereleases in **RegiRex/uberhar only**, never the
upstream Azahar repository. Literal integration into `master` is unnecessary
for that request and deferred: that branch contains upstream commits not yet
merged into the tested Uberhar lineage. Do not overwrite it or silently add
those changes to a device comparison. Read current guidance before continuing.

The 0.1.22 baseline is commit
`d742cd5e9e90b7ba3f67367460ef2c7d3f27b5bc`. Its shader and Android gates passed:
https://github.com/RegiRex/uberhar/actions/runs/37524017429
Artifact: `uberhar-0.1.22-arm64`.
The owner supplied a 0.1.22 Thor log and reports continued ghost/moon glitches,
more visible glitches overall, and a significant perceived speed improvement.
Read the current progress and analysis before assigning a measured outcome.
**0.1.23 is provisional** until its source,
validation and release status are recorded. Do not call a candidate APK ready
before its package, signing and shader gates pass.
The displayed ZIP archive digest is
`824b452294f5cc11d911f96d2245226655ed7b092c4b73c24268de51558257da`;
this is not the contained APK's digest. Verify the chosen artifact's provenance
and package/signature before installation. Use an update preserving app data;
if signing is incompatible, stop rather than uninstalling or clearing data.

First establish one repeatable, bounded run: full Combo, Vulkan, 2x resolution,
normal speed, the same opening through the previously broken ghost scene, then
a normal return to the game list. Read `docs/releases/0.1.22.md` and
`docs/UBERHAR_LOG_ANALYSIS_0.1.21.md`. Inspect actual UI/launch capabilities and
game location locally; do not invent an intent, replay script or frame target.
Use existing saves without overwriting the owner's progress. Ask for only the
one-time access or scene setup that cannot be automated.

Capture ordinary app logs, a small visual sample around the ghost and before/
after-run memory evidence. Establish that the correct game and scene actually
ran; an exit status or an app launch is not a gameplay pass. Preserve the normal
native-return snapshot. Stop promptly for app errors, rapid memory pressure or
thermal limits rather than repeatedly driving the device into a crash.

Record exact commit/APK/version, settings, driver/OS, cache state, scene, speed,
temperature/power conditions where available, memory trends and visual outcome.
Use matched runs for comparisons. Do not call a run cold without verifying the
relevant cache state; preserve user saves and obtain a concrete scoped plan
before any cache removal. Do not run concurrent benchmarks on one Thor.

Parallelize source analysis, review and log analysis when supported, while one
agent owns device operations. Make evidence-based, focused changes; build with
the existing correctness/package/signing gates and independently review each
candidate. All new logical code sections need `CodexAstraUlt` commentary. When
replacing AstraEH/AstraPro blocks, record the removed behavior, reason and
replacement adjacent to the new code, preserving remaining historical markers.

Work in bounded experiment batches with a report at each checkpoint. This setup
does not establish a perpetual unattended service or unlimited sessions. Start
by reporting device readiness and one verified run on the agreed validated
build before expanding to a repeated build/test loop.

## Interpretation of the baseline

<!-- CodexAstraUlt: Keep hypotheses separate from measured ownership and device validation at transfer time. -->

The 0.1.21 run lost about 7.5 GiB of system available memory while warmed process
RSS and explicitly tracked Vulkan allocations plateaued. It returned normally.
That identifies real system pressure, not a proven app-owned leak or its owner.

0.1.22 rejects optional lit GPU draws when neither enabled quaternion-correction
path is available, fixes three-component attribute `w` padding, and adds bounded
own-process KGSL/system-memory evidence to the existing sampling cadence. Host
regressions passed. The owner's two-run log shows a larger live Combo KGSL
footprint that is released on normal exit; the owner still sees visual faults.
Read the [0.1.22 analysis](../../../docs/UBERHAR_LOG_ANALYSIS_0.1.22.md) for scope
and limits. Lit CPU fallback can reduce throughput; the owner's perceived speed
improvement is not a matched benchmark. The cloud host cannot reproduce Qualcomm
driver behavior.

At preparation time, the cloud environment had no VPN or private TCP destination
grants. No background runner or cloud-to-laptop connection is installed by these
instructions. The manual `adb devices -l` command may start the laptop's local ADB
server; the device probe itself is only a readiness check.

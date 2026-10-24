# Android per-game settings and Testing controls

<!-- CodexAstraLocal: Record the owner's approved scope and the implementation boundary without claiming an unbuilt feature is installed. -->
This feature is included in the 0.1.54 candidate. It is not part of the installed
0.1.53 APK. Full Android build gates and device UI validation are separate steps;
the owner performs the .53/.54 performance comparison before adoption.

## Owner controls

- Long-press a game in the library and select **Game Settings**. The in-game menu
  also provides **Game Settings** for that title.
- General, Graphics, Layout and Audio expose supported per-game preferences.
  A setting marked **Global** inherits the current global value; **Game override**
  identifies a saved or pending title-specific choice.
- Long-press an individual setting to **Use Global Settings**. Confirm the dialog
  to remove its override, or cancel to leave it unchanged.
- **Use Global Settings** on the game's main settings page resets all supported
  per-game overrides after confirmation. This restores inheritance from current
  global preferences, rather than factory values. Other titles are unaffected.
- Close the game before changing renderer resources or resetting all settings.
  Ordinary runtime-editable controls retain their existing behavior.

## Temporary Testing overlay

<!-- CodexAstraLocal: The owner explicitly selected temporary overrides, with saved settings restored when Testing is disabled. -->
The top of the main Settings page has a **Testing** switch. When enabled it exposes:

| Rendering preset | Separate resolution selector |
|---|---|
| Native | 2× or 4× |
| Combo · CPU Vertices ON | 2× or 4× |
| Combo · CPU Vertices OFF | 2× or 4× |

Testing changes only the rendering mode, Combo CPU-vertex choice and resolution;
other saved options continue to apply. It applies to every game until switched
off. Disabling it reveals the untouched global/per-game choices. A scope banner
distinguishes the saved values being edited from the active Testing preset.
Change the Testing controls between games. Resetting saved game preferences does
not disable an active Testing overlay.

## Storage and application order

<!-- CodexAstraLocal: Maintain explicit ownership of global, sparse title and test-only settings so one layer cannot overwrite another. -->
1. Read `config/config.ini` as the global configuration.
2. Apply supported sparse overrides from `GameSettings/<16-digit-title-ID>.ini`.
3. Apply the separate `config/testing.ini` overlay when enabled.
4. Apply the existing Uberhar rendering profile rules.

Zero/invalid title IDs use globals and do not share a title-override file.
Console identity, accounts, storage, networking, controllers and turbo cap stay
global. Unknown title-file keys are retained when editing known settings.
New preset IDs must be added consistently to the UI catalog, Kotlin overlay
model and native overlay mapping. The Java gameplay controls share the Activity's
title-scoped settings model, including games launched through external URIs.

## Validation

<!-- CodexAstraLocal: These executable boundaries establish settings behavior, not Android UI or gameplay qualification. -->
`tools/uberhar/test_android_settings_scope.py` compiles the production Kotlin
settings/model/INI code with real ini4j and host substitutes for Android file,
resource and JNI-key boundaries. It checks title isolation, sparse persistence,
inheritance reset, unknown-key retention, legacy turbo cap and Testing separation.
Supply `--kotlinc` and `--ini4j`, with a compatible `JAVA_HOME`.

`tools/uberhar/test_android_settings_layers.py` executes the production native
typed layering methods with actual settings types and INIReader. It covers
inheritance, malformed values, global-only isolation, Android defaults, runtime
resource protection, ordinary live resolution and enabling/disabling Testing.

Before shipping, build the Android app and verify entrypoints, confirmed and
cancelled individual/all resets, title switching, external launches, temporary
Testing restore and normal-speed new-game startup on the Thor. Do not change
existing owner settings or start gameplay merely to read this document.

## 0.1.55 additions

<!-- CodexAstraLocal: One shared UI edits separate Testing/title-driver stores and preserves normal preferences. -->
Home **Options** now presents Testing directly below the flower. Choose global
Testing defaults or a library game, then select Native / Combo CPU Vertices ON /
OFF and 2× / 4×. The same controls remain on the main Settings page. The Testing
master is global; preset/resolution exceptions are stored per title in
`testing.ini`, independently of ordinary title configuration. **Use global Testing
choices** requires confirmation and removes only that title's Testing overrides.
Testing OFF restores normal saved settings at the next launch.

While a game is running, Testing resolution can change live. Rendering preset,
Testing master and driver require exit because they own launch-time resources.
The ordinary resolution setting stays separate; use the Testing buttons to change
an active override without overwriting normal preferences.

Each title's panel also offers **GPU driver for this game**: inherit the installed
global driver, use System, or choose an imported driver archive. These exceptions
live in `drivers.ini`. Launch preparation verifies the archive and creates an
immutable content-addressed installation without changing the global driver.
Missing/incompatible archives fall back visibly to global. Native title-driver
load failures retry the saved global choice before System; logs record the actual
effective library and fallback is shown to the user. Archive snapshots, extracted
entries/bytes and retained immutable installations have explicit bounds. Driver
changes are refused while the native run/teardown owns its lifecycle lock. Old
extracted directories are bounded and only pruned after prior native users drain.

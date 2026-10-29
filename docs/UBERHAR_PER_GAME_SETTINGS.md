# Android per-game settings and Testing controls

<!-- CodexAstraLocal: Record the owner's approved scope and the implementation boundary without claiming an unbuilt feature is installed. -->
Introduced in 0.1.54 and expanded in 0.1.55/0.1.56, these controls support separate
global defaults and per-game choices. The corrected 0.1.56 rebuild also exposes
all five new experiments on Options. Build checks and device UI validation remain
separate steps; this document describes the current source behavior.

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

<!-- CodexAstraLocal: Global experiments and title overrides never overwrite ordinary preferences. -->
Testing selects the rendering mode, Combo CPU-vertex choice and resolution, with
separate experiments described below. Global Testing defaults apply to every game
unless that title has an override. The overlay remains active until
switched off. Disabling it reveals the untouched global/per-game choices. A scope banner
distinguishes the saved values being edited from the active Testing preset.
Change the Testing controls between games. Resetting saved game preferences does
not disable an active Testing overlay.

## Storage and application order

<!-- CodexAstraLocal: Maintain explicit ownership of global, sparse title and test-only settings so one layer cannot overwrite another. -->
1. Read `config/config.ini` as the global configuration.
2. Apply supported sparse overrides from `GameSettings/<16-digit-title-ID>.ini`.
3. Apply the separate `config/testing.ini` overlay when enabled on an alpha build.
4. Apply the existing Uberhar rendering profile rules, retaining the launch-latched
   Testing experiments through live resolution reloads.

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
<!-- CodexAstraLocal: JNI launch snapshots are test boundaries, not proof of native lifecycle behavior. -->
It also checks global experiment persistence and inheritance, defaults/bounds, title isolation/reset, CPU-only hardware
veto, displays against substituted native launch snapshots, beta/full refusal to
rewrite preserved alpha preferences, and independent per-title driver editing.
<!-- CodexAstraLocal: Real controller listeners are exercised with host substitutes, not a device UI. -->
The fixture also compiles `TestingPanelController` and invokes its actual listeners
in global and title contexts. Host widget/dialog/lifecycle/JNI substitutes check
control availability, delay input validation, reset cancellation/confirmation,
launch locks and resolution reload calls; they do not qualify Android rendering,
native lifecycle execution or gameplay.
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
master is global; exceptions for every Testing control are stored per title in
`testing.ini`, independently of ordinary title configuration. **Use global Testing
choices** requires confirmation and removes only that title's Testing overrides.
Testing OFF restores normal saved settings at the next launch.

While a game is running, Testing resolution can change live. Rendering preset,
Testing master and driver require exit because they own launch-time resources.
The ordinary resolution setting stays separate; use the Testing buttons to change
an active override without overwriting normal preferences.

<!-- CodexAstraLocal: Document the explicit right-eye experiment, restart boundary and observer limits. -->
The panel includes **Disable Right Eye Render** beside Testing preset/resolution.
<!-- CodexAstraLocal: The repaired global Options panel can set experiments inherited by games. -->
It defaults OFF and can be changed globally or for a selected game. Native and both Combo choices honor
the override without selecting Custom. It uses the inherited heuristic to skip
command lists/display transfers and may cause flickering or missing graphics;
it does not prove the omitted work was redundant. The title compatibility gate
can still forbid suppression. The running switch shows the native requested
setting and requires exiting the game to change it; live resolution changes
preserve that launch request. **Use global Testing choices** restores the current
global right-eye choice. Testing OFF restores ordinary saved/profile
behavior at the next launch, including Native/Combo's normal OFF default.

## 0.1.56 alpha Testing controls

<!-- CodexAstraLocal: Gate experiments by release.beta.alpha metadata while preserving normal driver access. -->
Testing is available only when the compiled version has a nonzero alpha component.
The UI and native overlay share that version gate; Android product flavor is not
the gate. Beta/full builds hide Testing and ignore stale `testing.ini` overlays
without rewriting the file. Home hides the Testing panel; **Game Settings** keeps
the ordinary per-game GPU driver selector available.

<!-- CodexAstraLocal: Repair Options by enabling global experiments with the same title inheritance. -->
With Testing ON, edit these controls directly under **Testing defaults for all
games**, or select a title to save its own choices. All require exiting the game.
While running, the UI shows the native launch
values; only the resolution selector can apply a change immediately.

| Testing control | Baseline | Behavior |
|---|---|---|
| Disable Right Eye Render | OFF | Existing heuristic opt-in and compatibility exclusions above. |
| Hardware Shaders | OFF | Enables ordinary GPU-vertex admission. Combo can still promote ready GPU vertices while OFF. Combo CPU Vertices ON vetoes it and displays OFF while retaining the saved request for other presets. |
| Render-thread delay | 0µs | Whole-number input from 0 through16000µs;100µs is the captured Azahar comparison. |
| Linear Filtering | ON | Final presentation sampling; OFF selects nearest filtering. |
| Guest 3D slider | 0% | Select0% or1%. This value reaches guest HID even with mono display;1% matches the captured Azahar comparison. |

<!-- CodexAstraLocal: Missing title keys inherit; malformed explicit values retain documented baselines. -->
Global requests are stored in `[Testing]`, with title requests in `[Testing.<title-ID>]`, under
`disable_right_eye_render`, `use_hw_shader`, `delay_game_render_thread_us`,
`filter_mode` and `factor_3d`. Missing title keys inherit global choices; malformed
explicit values and missing global values use the listed baselines.
**Use global Testing choices** removes all five title requests as well as the
title preset/resolution override, restoring current global choices for all controls.
It retains normal global/per-game preferences
and the independent driver choice. Hardware Shaders ON changes ordinary Native
CPU-vertex behavior and is an experiment, not a renamed standard Native route.
Graphics and Debug show effective profile values while preserving saved Custom
values. Other profile locks and rendering safeguards remain in place.

The Settings scope/Testing panel is now the first item of the same RecyclerView
as the settings rows. Its controls scroll away with the list, retaining usable
space and system-bar/cutout padding in short phone and Thor landscape windows.

Temporary `Testing right-eye` logs distinguish overlay request from the final
setting. `Right-eye heuristic` logs report requested setting, compatibility gate,
frame-active flag, and cumulative seen/evaluated/skipped command-list and display
transfer decisions, at most every five seconds plus state changes and shutdown.
The callback count includes the heuristic's pending fallback; it is not a
completed-frame count. Last guessed viewport/color-buffer values are a heuristic
sample, not eye identity. Counts do not measure skipped draws or saved time.
Remove these `CodexAstraLocal TEST-ONLY LOG` observers, counters and shutdown hook
after the owner accepts the Native/Combo right-eye comparison; preserve the
existing heuristic and interrupt behavior.

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

## Retiring the alpha controls

<!-- CodexAstraLocal: Temporary experiments need a decision and removal path, not permanent UI growth. -->
At each candidate review, classify each control as a useful normal per-game
preference, a validated default, an unsuccessful experiment, or still under test.
Promote the first category into ordinary settings with explicit baseline-preserving
migration; remove the Testing duplicate once that preference is available. Fold
validated defaults into documented profile policy. Remove unsuccessful controls
and their exclusive runtime code/diagnostics. Unresolved experiments stay alpha-only.
Do not silently activate old hidden Custom values when removing profile locks.

Hardware Shaders and render-thread delay need correct-output and cold/warm cost
comparisons before promotion. Linear Filtering can become an ordinary presentation
preference. The 0/1% guest slider comparison informs a normal guest-slider preference
or compatibility default. Right-eye suppression remains a per-game compatibility
choice only if visual output is acceptable; its heuristic is not proof of duplicate
work. The preset/resolution shortcuts should eventually become ordinary saved
profiles if useful, rather than requiring the Testing overlay in beta/full releases.

Beta/full builds already hide and ignore Testing. Before 1.0.0, remove any obsolete
Testing implementation and TEST-ONLY observers after useful preferences have moved
to ordinary settings. Preserve old testing.ini files during migration; hidden
stale experiments must never influence a beta/full run.

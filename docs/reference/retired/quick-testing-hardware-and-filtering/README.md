# Retired quick Testing Hardware Shaders and Linear Filtering

<!-- CodexAstraLocal: Preserve the removed experiments by feature, outside compiled and packaged source, with the exact original context and restoration dependencies. -->

Source commit: `0723b003d92d20df45778b046672c77739e78599` (.56-r2). The manifest identifies exact source paths and SHA-256 values. `origin/` contains verbatim original files with `.txt` suffixes; retained live code appears only as historical context, not a second implementation. Original copyright notices and GPLv2-or-later license are preserved.

## Hardware Shader quick experiment

Retired sections are `TestingSettings.hardwareShaders`, its conflict/effective accessors, controller bindings, `testingHardwareShaders` layout row, the `use_hw_shader` Testing resolver/allowlist, and the dedicated JNI/NativeLibrary effective accessor. The old common profile is included because it controlled the experiment's effective behavior. The owner retired this rejected experiment after .56 testing: Native and Combo now fix ordinary hardware shaders OFF; independent ready-GPU promotion in Combo CPU-OFF remains necessary and is not retired. Ordinary Custom/OpenGL preferences and ordinary Custom-only hardware controls remain supported.

Restoration requires the matching Kotlin resolver/UI/layout/string resources, C++ Testing allowlist/launch snapshot, JNI declarations, graphics profile and precedence fixtures together. Preserve the Combo CPU-only safety veto and independent ready-GPU route. Do not restore the obsolete key as a hidden active override.

## Linear Filtering quick duplicate

Retired sections are `TestingSettings.linearFiltering`, its effective accessor, controller bindings, `testingLinearFiltering` row, `filter_mode` Testing override and dedicated JNI accessor. Ordinary Graphics `Renderer.filter_mode`, default ON, remains available. The quick duplicate was removed for clarity, not because filtering was disabled or shown universally harmful.

The .57 migration preserves active legacy choices in the matching ordinary Renderer scope only where no explicit ordinary value exists; it archives the retired key under inert `Retired.056.Testing[.<title>]` metadata before deleting the active key. Restoring quick access would require the resolver/UI/JNI and migration/precedence fixtures to agree. Do not undo migrated explicit title choices or activate inert archival metadata.

The approved owner scope is documented in the current per-game settings guide. No private recordings, title IDs, device information or measurements are included in this archive.

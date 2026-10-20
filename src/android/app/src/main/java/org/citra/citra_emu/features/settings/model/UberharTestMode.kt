// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

// CodexAstraUlt: Extend AstraEH's single persisted mode with a generic-fragment Combo control.
// CodexAstraLocal: Supported IDs stay fixed; retired ID 2 migrates to Native below.
enum class UberharTestMode(val id: Int) {
    // CodexAstraLocal: CPU graphics is a separate implementation, never Native fallback.
    CUSTOM(0), NATIVE(1), AUTOMATIC(3), COMBO_GENERIC(4), SOFTWARE(5);

    // CodexAstraUlt: Match common/uberhar_test_profile.h for the locked effective display.
    val allowsSpecializedFragments: Boolean get() = this == AUTOMATIC

    companion object {
        // CodexAstraLocal: Never recycle retired mode 2 or display it as a different renderer.
        fun from(value: Int) = if (value == 2) NATIVE else entries.firstOrNull { it.id == value } ?: CUSTOM
    }
}

// AstraEH: UI-only booleans delegate to one integer; never save these synthetic keys.
class UberharTestModeSwitch(
    val backing: AbstractIntSetting,
    val mode: UberharTestMode
) : AbstractBooleanSetting {
    override val key = "uberhar_test_switch_${mode.id}"
    override val section get() = backing.section
    override val defaultValue = false
    override val isRuntimeEditable = false
    override val valueAsString get() = boolean.toString()
    override var boolean: Boolean
        get() = UberharTestMode.from(backing.int) == mode
        set(value) {
            if (value) backing.int = mode.id
            else if (boolean) backing.int = UberharTestMode.CUSTOM.id
        }
}

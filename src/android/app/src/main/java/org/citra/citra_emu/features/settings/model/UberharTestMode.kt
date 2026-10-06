// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

// CodexAstraUlt: Extend AstraEH's single persisted mode with a generic-fragment Combo control.
// IDs 0–3 and saved custom values retain their meanings; no independent conflicting toggles.
enum class UberharTestMode(val id: Int) {
    CUSTOM(0), NATIVE(1), COMPUTE(2), AUTOMATIC(3), COMBO_GENERIC(4);

    // CodexAstraUlt: Match common/uberhar_test_profile.h for the locked effective display.
    val allowsSpecializedFragments: Boolean get() = this == AUTOMATIC

    companion object {
        fun from(value: Int) = entries.firstOrNull { it.id == value } ?: CUSTOM
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

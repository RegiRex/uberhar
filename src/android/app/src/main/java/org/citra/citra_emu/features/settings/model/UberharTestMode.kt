// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

// AstraEH: A single persisted value makes the three test switches mutually exclusive.
// The lower settings remain untouched, so returning to CUSTOM restores them exactly.
enum class UberharTestMode(val id: Int) {
    CUSTOM(0), NATIVE(1), COMPUTE(2), AUTOMATIC(3);

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

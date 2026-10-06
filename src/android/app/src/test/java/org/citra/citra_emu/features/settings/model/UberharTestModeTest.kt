// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.features.settings.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

// AstraEH: Test the actual shared backing switch model without loading Android/JNI settings.
class UberharTestModeTest {
    private class Backing : AbstractIntSetting {
        override var int = 0
        override val key = "uberhar_test_mode"
        override val section = "Renderer"
        override val defaultValue = 0
        override val isRuntimeEditable = false
        override val valueAsString get() = int.toString()
    }

    @Test fun modesAreExclusiveAndOffRestoresCustom() {
        val backing = Backing()
        val switches = UberharTestMode.entries.filter { it != UberharTestMode.CUSTOM }
            .map { UberharTestModeSwitch(backing, it) }
        for (first in switches) for (next in switches) {
            first.boolean = true
            assertEquals(1, switches.count { it.boolean })
            next.boolean = true
            assertEquals(next.mode.id, backing.int)
            assertEquals(1, switches.count { it.boolean })
            for (inactive in switches.filter { it !== next }) inactive.boolean = false
            assertTrue(next.boolean)
            next.boolean = false
            assertEquals(0, backing.int)
            assertTrue(switches.none { it.boolean })
        }
    }

    @Test fun loadedModeAndUnknownValuesAreHandled() {
        val backing = Backing()
        backing.int = 2
        assertTrue(UberharTestModeSwitch(backing, UberharTestMode.COMPUTE).boolean)
        assertFalse(UberharTestModeSwitch(backing, UberharTestMode.NATIVE).boolean)
        assertEquals(UberharTestMode.CUSTOM, UberharTestMode.from(99))
        assertFalse(UberharTestModeSwitch(backing, UberharTestMode.AUTOMATIC).isRuntimeEditable)
    }

    // CodexAstraUlt: Persisted IDs remain stable and the effective UI fragment choice matches
    // the native preset policy. The existing exclusivity test includes the appended mode.
    @Test fun fragmentIsolationPreservesIdsAndEffectiveDisplayPolicy() {
        assertEquals(listOf(0, 1, 2, 3, 4), UberharTestMode.entries.map { it.id })
        assertEquals(UberharTestMode.COMBO_GENERIC, UberharTestMode.from(4))
        for (mode in UberharTestMode.entries) {
            assertEquals(mode == UberharTestMode.AUTOMATIC, mode.allowsSpecializedFragments)
        }
        val backing = Backing().apply { int = 4 }
        val generic = UberharTestModeSwitch(backing, UberharTestMode.COMBO_GENERIC)
        assertTrue(generic.boolean)
        assertFalse(generic.isRuntimeEditable)
        generic.boolean = false
        assertEquals(UberharTestMode.CUSTOM.id, backing.int)
    }
}

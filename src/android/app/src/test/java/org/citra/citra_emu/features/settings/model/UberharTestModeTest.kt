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
        // CodexAstraLocal: The legacy persisted ID activates Native, never Combo or Software.
        assertTrue(UberharTestModeSwitch(backing, UberharTestMode.NATIVE).boolean)
        assertFalse(UberharTestModeSwitch(backing, UberharTestMode.AUTOMATIC).boolean)
        assertEquals(UberharTestMode.NATIVE, UberharTestMode.from(2))
        assertEquals(UberharTestMode.CUSTOM, UberharTestMode.from(99))
        assertFalse(UberharTestModeSwitch(backing, UberharTestMode.AUTOMATIC).isRuntimeEditable)
    }

    // CodexAstraUlt: Persisted IDs remain stable and the effective UI fragment choice matches
    // the native preset policy. The existing exclusivity test includes the appended mode.
    @Test fun fragmentIsolationPreservesIdsAndEffectiveDisplayPolicy() {
        // CodexAstraLocal: Retired profiles migrate; only unchanged Native/Combo IDs remain.
        assertEquals(listOf(0, 1, 3), UberharTestMode.entries.map { it.id })
        assertEquals(UberharTestMode.AUTOMATIC, UberharTestMode.from(4))
        assertEquals(UberharTestMode.NATIVE, UberharTestMode.from(5))
        for (mode in UberharTestMode.entries) {
            assertEquals(mode == UberharTestMode.AUTOMATIC, mode.allowsSpecializedFragments)
        }
        val backing = Backing().apply { int = 4 }
        val generic = UberharTestModeSwitch(backing, UberharTestMode.AUTOMATIC)
        assertTrue(generic.boolean)
        assertFalse(generic.isRuntimeEditable)
        generic.boolean = false
        assertEquals(UberharTestMode.CUSTOM.id, backing.int)
    }
}

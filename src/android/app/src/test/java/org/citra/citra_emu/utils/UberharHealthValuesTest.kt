// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils
import org.junit.Assert.*
import org.junit.Test
// AstraPro: Tests are pure JVM; no Android sensor or device support is simulated.
class UberharHealthValuesTest {
    @Test fun normalization() {
        assertEquals("unknown", UberharHealthValues.positive(0))
        assertEquals("unknown", UberharHealthValues.positive(-1))
        assertEquals("unknown", UberharHealthValues.positive(null))
        assertEquals("42", UberharHealthValues.positive(42))
        for (raw in listOf(null, "", "0", "-1", "NaN", "10000001", "9999999999999999999999999"))
            assertEquals("unknown", UberharHealthValues.frequency(raw))
        assertEquals("2400000", UberharHealthValues.frequency(" 2400000\n"))
    }
    @Test fun boundedSnapshots() {
        var calls=0
        val values=UberharHealthValues.cpuSnapshot { core,node ->
            calls++
            assertTrue(core in 0..15)
            assertTrue(node=="scaling_cur_freq" || node=="scaling_max_freq")
            if (core==0) "2400000" else null
        }
        assertEquals(32,calls)
        assertTrue(values.startsWith("[0:2400000/2400000,1:unknown/unknown"))
        assertTrue(values.endsWith("15:unknown/unknown]"))
    }
}

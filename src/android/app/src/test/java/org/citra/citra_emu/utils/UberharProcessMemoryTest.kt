// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import org.junit.Assert.*
import org.junit.Test

// CodexAstraUlt: Pure JVM coverage of bounds and unknown-value semantics; no simulated
// Android RSS/driver result is used as evidence of device memory consumption.
class UberharProcessMemoryTest {
    @Test fun kernelUnitsAndRealZero() {
        val sample = UberharProcessMemory.parse(
            "Name:\tuberhar\nVmRSS:\t 12345 kB\nVmHWM: 20000 kB\n" +
                "RssAnon: 8000 kB\nRssFile: 4345 kB\nRssShmem: 0 kB\nVmSwap: 0 kB\n"
        )
        assertEquals(12345L, sample.rssKiB)
        assertEquals(20000L, sample.highWaterKiB)
        assertEquals(8000L, sample.anonymousKiB)
        assertEquals(4345L, sample.fileKiB)
        assertEquals(0L, sample.sharedKiB)
        assertEquals(0L, sample.swapKiB)
    }

    // CodexAstraUlt: Invalid/missing counters are not converted into zero or a capacity
    // claim. A duplicate remains unknown even when a third value looks valid.
    @Test fun invalidAndDuplicateFieldsStayUnknown() {
        for (raw in listOf("-1 kB", "42 B", "42", "1.5 kB", "999999999999999999999 kB"))
            assertNull(UberharProcessMemory.parse("VmRSS: $raw\n").rssKiB)
        val sample = UberharProcessMemory.parse(
            "VmRSS: 42 kB\nVmRSS: 43 kB\nVmRSS: 44 kB\nRssFile: 9 kB\n"
        )
        assertNull(sample.rssKiB)
        assertNull(sample.highWaterKiB)
        assertEquals(9L, sample.fileKiB)
    }

    // CodexAstraUlt: An endless source, short reads and denied proc access all have
    // bounded work; the overflow sentinel never becomes a partial valid sample.
    @Test fun boundedReadsAndUnavailableProc() {
        var bytesRead = 0
        val endless = object : InputStream() {
            override fun read(): Int { bytesRead++; return 'X'.code }
        }
        assertEquals("truncated", UberharProcessMemory.read(endless).source)
        assertEquals(UberharProcessMemory.MAX_BYTES + 1, bytesRead)
        val short = object : ByteArrayInputStream("VmRSS: 31 kB\n".toByteArray()) {
            override fun read(bytes: ByteArray, offset: Int, length: Int): Int =
                super.read(bytes, offset, minOf(length, 2))
        }
        assertEquals(31L, UberharProcessMemory.read(short).rssKiB)
        val denied = object : InputStream() {
            override fun read(): Int = throw IOException("Unavailable")
        }
        assertEquals(UberharProcessMemory.Snapshot(), UberharProcessMemory.read(denied))
        val stalled = object : InputStream() {
            override fun read(): Int = 0
            override fun read(bytes: ByteArray, offset: Int, length: Int): Int = 0
        }
        assertEquals("invalid_read", UberharProcessMemory.read(stalled).source)
    }

    // CodexAstraUlt: Accept a complete exactly-at-limit status file, reject one byte
    // more, and retain zero as a valid counter independent of system page size.
    @Test fun exactBoundary() {
        val prefix = "VmRSS: 0 kB\n"
        val exact = prefix + "X".repeat(UberharProcessMemory.MAX_BYTES - prefix.length)
        assertEquals(0L, UberharProcessMemory.read(exact.byteInputStream()).rssKiB)
        assertNull(UberharProcessMemory.read((exact + "X").byteInputStream()).rssKiB)
    }
}

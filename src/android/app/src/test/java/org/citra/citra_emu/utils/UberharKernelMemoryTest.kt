// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import org.junit.Assert.*
import org.junit.Test

// CodexAstraUlt: Parser and fixed-path bounds only; these tests do not simulate
// KGSL driver allocation costs, device permissions or resident GPU memory.
class UberharKernelMemoryTest {
    @Test fun fixedOwnProcessPathsAndUnits() {
        val opened = mutableListOf<String>()
        var closed = 0
        val snapshot = UberharKernelMemory.sample(123) { path ->
            opened.add(path)
            val text = when (path) {
                "/proc/meminfo" -> "MemTotal: 10000 kB\nMemAvailable: 8765 kB\n"
                "/sys/class/kgsl/kgsl/proc/123/kernel" -> "1048576\n"
                "/sys/class/kgsl/kgsl/proc/123/gpumem_mapped" -> "0\n"
                else -> error("Unexpected file: $path")
            }
            object : ByteArrayInputStream(text.toByteArray()) {
                override fun close() { closed++; super.close() }
            }
        }
        assertEquals(3, opened.size)
        assertEquals(3, closed)
        assertEquals(8765L, snapshot.availableKiB)
        assertEquals(1048576L, snapshot.kgslKernelBytes)
        assertEquals(0L, snapshot.kgslCpuMappedBytes)
    }

    // CodexAstraUlt: Permission denial and disappeared post-teardown nodes never
    // imply zero ownership. Failure of one independent source preserves the others.
    @Test fun optionalAndIndependentSources() {
        val sample = UberharKernelMemory.sample(77) { path ->
            if (path == "/proc/meminfo") "MemAvailable: 0 kB\n".byteInputStream()
            else throw IOException("Denied or unavailable")
        }
        assertEquals(0L, sample.availableKiB)
        assertNull(sample.kgslKernelBytes)
        assertNull(sample.kgslCpuMappedBytes)
        var opened = 0
        val absent = UberharKernelMemory.sample(0) {
            opened++
            throw IOException("Unavailable")
        }
        assertEquals(1, opened)
        assertEquals(UberharKernelMemory.Snapshot(), absent)
    }

    // CodexAstraUlt: The kernel's KiB estimate and plain-byte GPU counters must
    // not accept each other's units, negative values, duplicates or overflow.
    @Test fun invalidValuesRemainUnknown() {
        for (text in listOf(null, "", "MemAvailable: -1 kB", "MemAvailable: 2 B",
                            "MemAvailable: 1.5 kB", "MemAvailable: 999999999999999999999 kB",
                            "MemAvailable: 1 kB\nMemAvailable: 2 kB"))
            assertNull(UberharKernelMemory.parseAvailableKiB(text))
        for (text in listOf(null, "", "-1", "+1", "12 kB", "2.5", "1\n2",
                            "999999999999999999999", "0".repeat(33)))
            assertNull(UberharKernelMemory.parseCounterBytes(text))
        assertEquals(0L, UberharKernelMemory.parseCounterBytes(" 0\n"))
    }

    // CodexAstraUlt: Endless data consumes only each file's bound plus one byte;
    // no truncated prefix is accepted, and each stream closes despite rejection.
    @Test fun boundedReadsAndCleanup() {
        var bytesRead = 0
        var closed = 0
        val sample = UberharKernelMemory.sample(42) {
            object : InputStream() {
                override fun read(): Int { bytesRead++; return '1'.code }
                override fun close() { closed++ }
            }
        }
        assertEquals(UberharKernelMemory.Snapshot(), sample)
        assertEquals(UberharKernelMemory.MAX_MEMINFO_BYTES + 1 +
                         2 * (UberharKernelMemory.MAX_COUNTER_BYTES + 1), bytesRead)
        assertEquals(3, closed)
    }

    // CodexAstraUlt: Short reads work; zero-progress and exactly-over-limit sources
    // terminate with unknown instead of looping or guessing a truncated value.
    @Test fun shortReadsAndExactBounds() {
        val prefix = "MemAvailable: 56 kB\n"
        val exact = prefix + "X".repeat(UberharKernelMemory.MAX_MEMINFO_BYTES - prefix.length)
        val sample = UberharKernelMemory.sample(42) { path ->
            if (path == "/proc/meminfo") object : ByteArrayInputStream(exact.toByteArray()) {
                override fun read(bytes: ByteArray, offset: Int, length: Int): Int =
                    super.read(bytes, offset, minOf(length, 2))
            } else object : InputStream() {
                override fun read(): Int = 0
                override fun read(bytes: ByteArray, offset: Int, length: Int): Int = 0
            }
        }
        assertEquals(56L, sample.availableKiB)
        assertNull(sample.kgslKernelBytes)
        assertNull(UberharKernelMemory.sample(42) { (exact + "X").byteInputStream() }.availableKiB)
    }

    // CodexAstraUlt: Sysfs accepts exactly 32 complete bytes, rejects a 33-byte
    // prefix, and never trusts digits delivered before a subsequent IO failure.
    @Test fun counterBoundariesAndPartialFailure() {
        val bounded = UberharKernelMemory.sample(42) { path ->
            when {
                path.endsWith("/kernel") -> ("0".repeat(31) + "7").byteInputStream()
                path.endsWith("/gpumem_mapped") -> ("0".repeat(32) + "7").byteInputStream()
                else -> "MemAvailable: 1 kB\n".byteInputStream()
            }
        }
        assertEquals(7L, bounded.kgslKernelBytes)
        assertNull(bounded.kgslCpuMappedBytes)
        var closed = 0
        val partial = UberharKernelMemory.sample(42) {
            object : InputStream() {
                private var delivered = false
                override fun read(): Int = throw IOException("Interrupted after prefix")
                override fun read(bytes: ByteArray, offset: Int, length: Int): Int {
                    if (delivered) throw IOException("Interrupted after prefix")
                    delivered = true
                    bytes[offset] = '1'.code.toByte()
                    return 1
                }
                override fun close() { closed++ }
            }
        }
        assertEquals(UberharKernelMemory.Snapshot(), partial)
        assertEquals(3, closed)
    }
}

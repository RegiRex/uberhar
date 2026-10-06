// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils

import java.io.File
import java.io.InputStream

// CodexAstraUlt: Optional fixed-node memory attribution, with no directory walk,
// permissions change or kernel allocation-list inspection. Published SM8550 KGSL
// kgsl_sharedmem.c mem_entry_show/gpumem_mapped_show use atomic64 counters in bytes:
// https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550/blob/oneplus/sm8550_v_15.0.0_oneplus11/vendor/qcom/opensource/graphics-kernel/kgsl_sharedmem.c
// "kernel" is process KGSL allocation accounting; "gpumem_mapped" counts CPU-mapped
// KGSL allocations, not resident GPU memory. They overlap RSS/VMA and each other.
object UberharKernelMemory {
    const val MAX_MEMINFO_BYTES = 16 * 1024
    const val MAX_COUNTER_BYTES = 32

    data class Snapshot(
        val availableKiB: Long? = null,
        val kgslKernelBytes: Long? = null,
        val kgslCpuMappedBytes: Long? = null
    )

    // CodexAstraUlt: Only the current process PID supplied by Android is used in
    // production. A missing process node after teardown remains unknown, not zero.
    fun sample(pid: Int, open: (String) -> InputStream = { File(it).inputStream() }): Snapshot {
        fun read(path: String, limit: Int): String? = runCatching {
            open(path).use { readBounded(it, limit) }
        }.getOrNull()
        val available = parseAvailableKiB(read("/proc/meminfo", MAX_MEMINFO_BYTES))
        if (pid <= 0) return Snapshot(availableKiB = available)
        val processRoot = "/sys/class/kgsl/kgsl/proc/$pid"
        return Snapshot(
            available,
            parseCounterBytes(read("$processRoot/kernel", MAX_COUNTER_BYTES)),
            parseCounterBytes(read("$processRoot/gpumem_mapped", MAX_COUNTER_BYTES))
        )
    }

    // CodexAstraUlt: MemAvailable is a kernel estimate of system availability in
    // KiB, not this app's memory. Invalid/duplicate/wrong-unit fields stay unknown.
    fun parseAvailableKiB(text: String?): Long? {
        if (text == null || text.length > MAX_MEMINFO_BYTES) return null
        var seen = false
        var available: Long? = null
        for (line in text.lineSequence()) {
            if (!line.startsWith("MemAvailable:")) continue
            if (seen) return null
            seen = true
            val match = availablePattern.matchEntire(line) ?: continue
            available = match.groupValues[1].toLongOrNull()
        }
        return available
    }

    // CodexAstraUlt: Sysfs emits one unsigned decimal byte count. Real zero is
    // valid; signs, suffixes, overflow and partial/oversized reads are unknown.
    fun parseCounterBytes(text: String?): Long? {
        if (text == null || text.length > MAX_COUNTER_BYTES) return null
        val trimmed = text.trim()
        if (trimmed.isEmpty() || trimmed.any { it !in '0'..'9' }) return null
        return trimmed.toLongOrNull()
    }

    // CodexAstraUlt: One extra byte detects truncation; short or zero-progress
    // reads cannot make work unbounded. Caller closes each stream on all paths.
    private fun readBounded(input: InputStream, limit: Int): String? {
        val bytes = ByteArray(limit + 1)
        var size = 0
        while (size < bytes.size) {
            val count = input.read(bytes, size, bytes.size - size)
            if (count < 0) break
            if (count == 0) return null
            size += count
        }
        return if (size > limit) null else String(bytes, 0, size, Charsets.US_ASCII)
    }

    private val availablePattern = Regex("MemAvailable:\\s*([0-9]+)\\s+kB\\s*")
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils

import java.io.InputStream

// CodexAstraUlt: Read six kernel-maintained counters, not smaps/PSS or every mapping.
// KiB units avoid assuming 4 KiB pages on Android devices with other page sizes.
object UberharProcessMemory {
    const val MAX_BYTES = 16 * 1024
    data class Snapshot(
        val rssKiB: Long? = null,
        val highWaterKiB: Long? = null,
        val anonymousKiB: Long? = null,
        val fileKiB: Long? = null,
        val sharedKiB: Long? = null,
        val swapKiB: Long? = null,
        val source: String = "unavailable"
    )

    // CodexAstraUlt: At most 16 KiB plus one overflow sentinel, even for a reader
    // supplying short chunks. Caller owns/ closes the stream; failed reads stay unknown.
    fun read(input: InputStream): Snapshot = runCatching {
        val bytes = ByteArray(MAX_BYTES + 1)
        var size = 0
        while (size < bytes.size) {
            val count = input.read(bytes, size, bytes.size - size)
            if (count < 0) break
            if (count == 0) return Snapshot(source = "invalid_read")
            size += count
        }
        if (size > MAX_BYTES) Snapshot(source = "truncated")
        else parse(String(bytes, 0, size, Charsets.US_ASCII))
    }.getOrElse { Snapshot() }

    // CodexAstraUlt: Missing, duplicated, wrong-unit, negative or overflowing values
    // remain unknown individually. A real zero (e.g. swap) is a valid measurement.
    fun parse(text: String): Snapshot {
        if (text.length > MAX_BYTES) return Snapshot(source = "truncated")
        val keys = arrayOf("VmRSS", "VmHWM", "RssAnon", "RssFile", "RssShmem", "VmSwap")
        val values = arrayOfNulls<Long>(keys.size)
        val seen = BooleanArray(keys.size)
        for (line in text.lineSequence()) {
            val colon = line.indexOf(':')
            if (colon < 0) continue
            val index = keys.indexOf(line.substring(0, colon))
            if (index < 0) continue
            if (seen[index]) {
                values[index] = null
                continue
            }
            seen[index] = true
            val match = valuePattern.matchEntire(line.substring(colon + 1)) ?: continue
            values[index] = match.groupValues[1].toLongOrNull()
        }
        return Snapshot(values[0], values[1], values[2], values[3], values[4], values[5],
                        "proc_self_status")
    }

    private val valuePattern = Regex("\\s*([0-9]+)\\s+kB\\s*")
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils

// AstraPro: Pure, bounded sensor normalization. Unknown never becomes zero capacity.
object UberharHealthValues {
    const val MAX_CORES = 16
    fun positive(value: Long?): String = value?.takeIf { it > 0 }?.toString() ?: "unknown"
    fun frequency(raw: String?): String = raw?.trim()?.toLongOrNull()
        ?.takeIf { it in 1..10_000_000 }?.toString() ?: "unknown"

    // AstraPro: At most 32 supplied reads; absent/offline/denied sysfs nodes are unknown.
    // Frequencies are instantaneous kHz, not utilization or proof of throttling.
    fun cpuSnapshot(read: (Int, String) -> String?): String =
        (0 until MAX_CORES).joinToString(",", "[", "]") { core ->
            "$core:${frequency(read(core, "scaling_cur_freq"))}/${frequency(read(core, "scaling_max_freq"))}"
        }
}

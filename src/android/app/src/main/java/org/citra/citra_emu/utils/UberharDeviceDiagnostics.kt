// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.utils

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import android.os.Debug
import android.os.PowerManager
import android.os.Process // CodexAstraUlt: Correlate process health and OS exit evidence.
import android.os.SystemClock
import java.io.File // AstraPro: Read-only, bounded CPU frequency context.

// AstraEH: Read-only, permission-free device context. Called on an IO worker while the
// game view is resumed; no network, debug bridge, per-frame binder calls or GPU waits.
object UberharDeviceDiagnostics {
    private var lastSampleMs: Long? = null

    @Synchronized
    fun sample(context: Context) {
        val now = SystemClock.elapsedRealtime()
        // AstraEH: Retain the cap across activity recreation and quick background/foreground changes.
        if (lastSampleMs?.let { now - it < 30_000L } == true) return
        lastSampleMs = now

        // AstraEH: A missing sensor is unknown, not a zero temperature or proof of no throttling.
        val power = runCatching { context.getSystemService(PowerManager::class.java) }.getOrNull()
        val thermal = runCatching { power?.currentThermalStatus }.getOrNull()
        val headroom = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            runCatching { power?.getThermalHeadroom(0)?.takeIf { it.isFinite() } }.getOrNull()
        } else {
            null
        }
        val powerSave = runCatching { power?.isPowerSaveMode }.getOrNull()
        val battery = runCatching {
            context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        }.getOrNull()
        val temperature = battery?.takeIf { it.hasExtra(BatteryManager.EXTRA_TEMPERATURE) }
            ?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0)?.div(10.0)
        val level = battery?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = battery?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
        val batteryPercent = if (level >= 0 && scale > 0) 100.0 * level / scale else null
        val plugged = battery?.takeIf { it.hasExtra(BatteryManager.EXTRA_PLUGGED) }
            ?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0)
        val memory = runCatching {
            val manager = context.getSystemService(ActivityManager::class.java)
                ?: return@runCatching null
            ActivityManager.MemoryInfo().also { manager.getMemoryInfo(it) }
        }.getOrNull()
        // CodexAstraUlt: Share the bounded process sample with the two lifecycle
        // records; their calls do not change this existing periodic sample gate.
        val processFields = processMemoryFields()
        // AstraPro: Read only fixed sysfs nodes, at most 24 characters each. No root,
        // permission changes, network, performance-mode writes or unbounded file reads.
        val cpuFrequencies = UberharHealthValues.cpuSnapshot { core, node ->
            runCatching {
                File("/sys/devices/system/cpu/cpu$core/cpufreq/$node").reader().use { reader ->
                    val chars = CharArray(24)
                    val count = reader.read(chars)
                    if (count in 1..23) String(chars, 0, count) else null
                }
            }.getOrNull()
        }

        // CodexAstraUlt Log Line: Replace AstraPro's schema-2 health record with schema 3
        // to distinguish process RSS from allocator/system memory while preserving its
        // cadence and sensor fields. Android NONE/unknown headroom is not thermal proof.
        Log.info(
            "Uberhar device health: schema=3 elapsed_ms=$now uptime_ms=${SystemClock.uptimeMillis()} model=${Build.MODEL} api=${Build.VERSION.SDK_INT} " +
                "thermal_status=${thermal ?: "unknown"} thermal_headroom=${headroom ?: "unknown"} " +
                "power_save=${powerSave ?: "unknown"} battery_percent=${batteryPercent ?: "unknown"} " +
                "plugged=${plugged ?: "unknown"} battery_c=${temperature ?: "unknown"} " +
                "available_mem_mib=${memory?.availMem?.div(1_048_576) ?: "unknown"} " +
                "low_memory=${memory?.lowMemory ?: "unknown"} " +
                "$processFields " +
                "cpu_freq_khz=$cpuFrequencies frequency_scope=instant_cur_max " +
                "host_performance_mode=unobserved gpu_clock=unknown thermal_source=android_api"
        )
    }

    // CodexAstraUlt: Existing native-run begin/finally hooks emit at most two records
    // per run. No sensor/service query, periodic timer or extra storage is added.
    fun reportProcessMemory(event: String) {
        runCatching {
            // CodexAstraUlt Log Line: Reliable lifecycle evidence precedes the existing
            // endRun flush; a process kill may prevent the final sample from existing.
            Log.info(
                "Uberhar process memory: schema=1 event=$event " +
                    "elapsed_ms=${SystemClock.elapsedRealtime()} wall_ms=${System.currentTimeMillis()} " +
                    processMemoryFields()
            )
        }
    }

    // CodexAstraUlt: Local bounded reads and allocator counters only. Kernel RSS,
    // malloc and JVM numbers overlap and may be sampled at slightly different times.
    private fun processMemoryFields(): String {
        val nativeHeap = runCatching { Debug.getNativeHeapAllocatedSize() }.getOrNull()
        val processMemory = runCatching {
            File("/proc/self/status").inputStream().use(UberharProcessMemory::read)
        }.getOrElse { UberharProcessMemory.Snapshot() }
        val javaHeap = runCatching {
            val runtime = Runtime.getRuntime()
            (runtime.totalMemory() - runtime.freeMemory()).takeIf { it >= 0 }
        }.getOrNull()
        return "pid=${Process.myPid()} native_heap_bytes=${UberharHealthValues.positive(nativeHeap)} " +
            "java_heap_used_bytes=${javaHeap ?: "unknown"} " +
            "process_rss_kib=${processMemory.rssKiB ?: "unknown"} " +
            "process_rss_hwm_kib=${processMemory.highWaterKiB ?: "unknown"} " +
            "process_rss_anon_kib=${processMemory.anonymousKiB ?: "unknown"} " +
            "process_rss_file_kib=${processMemory.fileKiB ?: "unknown"} " +
            "process_rss_shmem_kib=${processMemory.sharedKiB ?: "unknown"} " +
            "process_swap_kib=${processMemory.swapKiB ?: "unknown"} " +
            "process_memory_source=${processMemory.source} memory_counters_not_additive=true"
    }
}

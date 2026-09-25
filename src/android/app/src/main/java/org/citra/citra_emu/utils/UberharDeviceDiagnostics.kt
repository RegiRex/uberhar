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
import android.os.SystemClock

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
        val nativeHeap = runCatching { Debug.getNativeHeapAllocatedSize() }.getOrNull()

        // AstraEH Log Line: At most one record per 30 seconds; battery temperature is not GPU/CPU temperature.
        Log.info(
            "Uberhar device health: uptime_ms=$now model=${Build.MODEL} api=${Build.VERSION.SDK_INT} " +
                "thermal_status=${thermal ?: "unknown"} thermal_headroom=${headroom ?: "unknown"} " +
                "power_save=${powerSave ?: "unknown"} battery_percent=${batteryPercent ?: "unknown"} " +
                "plugged=${plugged ?: "unknown"} battery_c=${temperature ?: "unknown"} " +
                "available_mem_mib=${memory?.availMem?.div(1_048_576) ?: "unknown"} " +
                "low_memory=${memory?.lowMemory ?: "unknown"} native_heap_bytes=${nativeHeap ?: "unknown"}"
        )
    }
}

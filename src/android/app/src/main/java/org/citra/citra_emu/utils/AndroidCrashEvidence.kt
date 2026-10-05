// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import java.io.File
import org.citra.citra_emu.BuildConfig

// AstraEH: Best-effort, one-shot post-crash recovery. No monitor, SDK, new permission or
// gameplay dependency. Android owns crash-time collection; Uberhar saves it on restart.
object AndroidCrashEvidence {
    fun collect(context: Context, root: File) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        collectSupported(context, root)
    }

    @androidx.annotation.RequiresApi(Build.VERSION_CODES.R)
    private fun collectSupported(context: Context, root: File) {
        val manager = context.getSystemService(ActivityManager::class.java) ?: return
        runCatching {
            manager.setProcessStateSummary(
                AndroidCrashReportStore.identity(BuildConfig.GIT_HASH).toByteArray()
            )
        }
        val store = AndroidCrashReportStore(root)
        // AstraEH: At most eight OS records examined and two new incident traces read per launch.
        // Untagged exits cannot be attributed to an exact Uberhar build, so leave them alone.
        var captured = 0
        for (info in manager.getHistoricalProcessExitReasons(null, 0, 8)) {
            val identity = AndroidCrashReportStore.parseIdentity(
                info.processStateSummary?.toString(Charsets.UTF_8)
            ) ?: continue
            val kind = when (info.reason) {
                ApplicationExitInfo.REASON_CRASH -> "java_crash"
                ApplicationExitInfo.REASON_CRASH_NATIVE -> "native_crash"
                ApplicationExitInfo.REASON_ANR -> "anr"
                ApplicationExitInfo.REASON_LOW_MEMORY -> "low_memory_exit"
                ApplicationExitInfo.REASON_SIGNALED -> "signal_exit"
                else -> continue
            }
            val exit = AndroidCrashReportStore.Exit(
                identity, info.timestamp, info.pid, kind, info.status, info.processName,
                info.description.orEmpty(), info.pss, info.rss,
                info.reason == ApplicationExitInfo.REASON_CRASH_NATIVE &&
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
            )
            if (store.hasSeen(exit)) continue
            store.record(exit) { info.traceInputStream }
            if (++captured >= 2) break
        }
    }
}

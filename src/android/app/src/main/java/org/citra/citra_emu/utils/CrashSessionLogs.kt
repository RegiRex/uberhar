// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean
import org.citra.citra_emu.R

// AstraEH: Keep the existing logger and a tiny lifecycle marker. No duplicate live journal,
// scheduled Android worker, exit-history service or per-launch bundle is needed.
object CrashSessionLogs {
    private val started = AtomicBoolean(false)
    private val warned = AtomicBoolean(false)
    private var store: CrashLogStore? = null
    private var app: Context? = null

    @Volatile var storageWarning = false
        private set

    @Synchronized
    fun prepare(context: Context): Boolean {
        if (store != null) return false
        app = context.applicationContext
        val logs = crashStore(context)
        store = logs
        val rotate = runCatching {
            logs.prepare(
                System.currentTimeMillis(),
                PermissionsHandler.citraDirectory.toString()
            ) { tree ->
                val directory = DocumentFile.fromTreeUri(context, Uri.parse(tree))
                    ?: throw IOException("Log directory unavailable")
                if (!directory.canRead()) throw IOException("Log directory unreadable")
                val file = directory.findFile("log")?.findFile("azahar_log.txt")
                file?.let {
                    context.contentResolver.openInputStream(it.uri)
                        ?: throw IOException("Cannot read interrupted log")
                }
            }
        }.getOrElse {
            warn()
            false
        }
        // AstraEH: Legacy cleanup is an upgrade operation, never required to play.
        runCatching { logs.migrateLegacy(File(context.filesDir, "uberhar_sessions")) }
            .onFailure { warn() }
        return rotate
    }

    fun loggerStarted() {
        if (!started.compareAndSet(false, true)) return
        checkHealth()
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            // AstraEH: Persist bounded Java exception text before trying a queue flush.
            // The existing Android handler still owns process termination.
            noteFailure("uncaught_java thread=${thread.name}\n${error.stackTraceToString()}")
            runCatching { Log.flush() }
            if (previous != null) {
                previous.uncaughtException(thread, error)
            } else {
                Process.killProcess(Process.myPid())
            }
        }
    }

    fun checkHealth() {
        if (!Log.fileHealthy()) warn()
    }

    fun beginRun() = markRun(true)

    fun endRun() {
        // AstraEH: Normal teardown flushes the primary log before clearing its run marker.
        if (!runCatching { Log.flush() && Log.fileHealthy() }.getOrDefault(false)) warn()
        markRun(false)
    }

    private fun markRun(active: Boolean) {
        runCatching { store?.markRun(active, System.currentTimeMillis()) }.onFailure { warn() }
    }

    fun noteFailure(message: String) {
        runCatching { store?.noteFailure(message) }.onFailure { warn() }
    }

    fun crashStore(context: Context): CrashLogStore =
        store ?: CrashLogStore(File(context.filesDir, "uberhar_crash_logs"))

    private fun warn() {
        storageWarning = true
        if (warned.compareAndSet(false, true)) {
            app?.let { context ->
                Handler(Looper.getMainLooper()).post {
                    Toast.makeText(context, R.string.log_session_storage_warning, Toast.LENGTH_LONG)
                        .show()
                }
            }
        }
    }
}

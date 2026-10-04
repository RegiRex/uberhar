// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.widget.Toast
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.Properties
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import org.citra.citra_emu.BuildConfig
import org.citra.citra_emu.R

// AstraEH: Native crashes cannot reliably execute app cleanup. Keep the file throughout the
// process and collect Android's bounded exit history on the next launch.
object CrashSessionLogs {
    private val worker = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "UberharSessionLog").apply { isDaemon = true }
    }
    private val started = AtomicBoolean(false)
    private val warned = AtomicBoolean(false)

    @Volatile private var store: CrashSessionStore? = null

    @Volatile var current: CrashSessionStore.Session? = null
        private set

    @Volatile var storageWarning = false
        private set

    @Synchronized
    fun prepare(context: Context): String {
        current?.let { return it.log.absolutePath }
        return try {
            val sessionStore = CrashSessionStore(File(context.filesDir, "uberhar_sessions"))
            val session = sessionStore.create(
                System.currentTimeMillis(),
                Process.myPid(),
                BuildConfig.GIT_HASH,
                BuildConfig.VERSION_NAME
            )
            store = sessionStore
            current = session
            sessionStore.markRun(session, false, System.currentTimeMillis())
            session.log.absolutePath
        } catch (_: Exception) {
            warn(context)
            ""
        }
    }

    fun loggerStarted(context: Context) {
        if (!started.compareAndSet(false, true)) return
        val app = context.applicationContext
        worker.execute {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                runCatching {
                    app.getSystemService(ActivityManager::class.java)
                        .setProcessStateSummary(current?.id?.toByteArray())
                }
                collectExitEvidence(app)
            }
            // AstraEH: Remove empty idle launch records only after collecting any OS incident.
            runCatching { store?.removeEmptyIdleSessions(current?.id) }
            runCatching { store?.pruneConfirmedCleanSessions(current?.id) }
        }
        // AstraEH: A real timer also flushes the last quiet record. Fixed delay avoids queue
        // growth if the five-second native barrier times out; no work runs on the UI thread.
        worker.scheduleWithFixedDelay({
            runCatching {
                val flushed = Log.flush()
                val healthy = Log.sessionFileHealthy()
                if (!flushed || !healthy) {
                    if (!warned.get()) {
                        // AstraEH Log Line: Once per process; distinguish queue and journal failure.
                        Log.warning(
                            "Uberhar log health: flush_complete=$flushed journal_ok=$healthy"
                        )
                    }
                    warn(app)
                }
            }.onFailure { warn(app) }
        }, 1, 1, TimeUnit.SECONDS)
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            runCatching {
                noteFailure("uncaught_java thread=${thread.name}\n${error.stackTraceToString()}")
            }
            // AstraEH: Preserve Android's crash reporting and termination behavior.
            if (previous !=
                null
            ) {
                previous.uncaughtException(thread, error)
            } else {
                Process.killProcess(Process.myPid())
            }
        }
    }

    fun beginRun() = markRun(true)

    fun endRun() {
        // AstraEH: Called on the emulation thread after normal native teardown, never on a signal.
        runCatching { Log.flush() }
        markRun(false)
    }

    private fun markRun(active: Boolean) {
        val session = current ?: return
        runCatching { store?.markRun(session, active, System.currentTimeMillis()) }
            .onFailure { storageWarning = true }
    }

    fun noteFailure(message: String) {
        val session = current ?: return
        runCatching { store?.noteFailure(session, message) }.onFailure { storageWarning = true }
    }

    fun sessionStore(context: Context): CrashSessionStore =
        store ?: CrashSessionStore(File(context.filesDir, "uberhar_sessions"))

    private fun warn(context: Context) {
        storageWarning = true
        if (warned.compareAndSet(false, true)) {
            Handler(Looper.getMainLooper()).post {
                Toast.makeText(
                    context,
                    R.string.log_session_storage_warning,
                    Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    @androidx.annotation.RequiresApi(Build.VERSION_CODES.R)
    private fun collectExitEvidence(context: Context) {
        val sessionStore = store ?: return
        try {
            val exits = context.getSystemService(ActivityManager::class.java)
                .getHistoricalProcessExitReasons(null, 0, 32)
            val sessions = sessionStore.sessions()
            sessions.forEachIndexed { index, session ->
                if (session.id == current?.id) return@forEachIndexed
                // AstraEH: UUID matching survives clock changes. PID fallback is bounded by the
                // next recorded process start; ambiguous/missing evidence stays explicitly unknown.
                val nextStart =
                    sessions.getOrNull(index - 1)?.startedMs ?: System.currentTimeMillis()
                val exit = exits.firstOrNull {
                    sessionStore.matchesExit(
                        session,
                        it.pid,
                        it.timestamp,
                        it.processStateSummary?.toString(Charsets.UTF_8),
                        nextStart
                    )
                } ?: return@forEachIndexed
                val exitFile = File(session.directory, "exit.properties")
                if (!exitFile.exists()) {
                    CrashSessionStore.writeProperties(
                        exitFile,
                        Properties().apply {
                            setProperty("kind", exitKind(exit.reason))
                            setProperty("reason", exit.reason.toString())
                            setProperty("status", exit.status.toString())
                            setProperty("timestamp_ms", exit.timestamp.toString())
                            setProperty("timestamp_source", "android_process_exit")
                            setProperty("description", exit.description.orEmpty().take(4096))
                            setProperty("pss_kib", exit.pss.toString())
                            setProperty("rss_kib", exit.rss.toString())
                            setProperty("collected_ms", System.currentTimeMillis().toString())
                        }
                    )
                }
                val traceFile = File(session.directory, "trace.bin")
                if (!traceFile.exists()) {
                    // AstraEH: A native API31+ trace is a tombstone protobuf; ANR traces may be
                    // text. Save bytes and format metadata; a missing OS trace is a valid outcome.
                    val temporary = File(session.directory, "trace.tmp")
                    try {
                        val trace = exit.traceInputStream
                        if (trace != null) {
                            trace.use { input ->
                                FileOutputStream(temporary).use { output ->
                                    val buffer = ByteArray(8192)
                                    var total = 0L
                                    while (true) {
                                        val count = input.read(buffer)
                                        if (count < 0) break
                                        total += count
                                        if (total >
                                            32L * 1024 * 1024
                                        ) {
                                            throw IOException("OS trace exceeds 32 MiB")
                                        }
                                        output.write(buffer, 0, count)
                                    }
                                    output.fd.sync()
                                }
                            }
                            if (!temporary.renameTo(
                                    traceFile
                                )
                            ) {
                                throw IOException("Cannot retain OS trace")
                            }
                        }
                        CrashSessionStore.writeProperties(
                            File(session.directory, "trace.properties"),
                            Properties().apply {
                                setProperty("available", traceFile.isFile.toString())
                                setProperty(
                                    "format",
                                    if (exit.reason ==
                                        ApplicationExitInfo.REASON_CRASH_NATIVE &&
                                        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                                    ) {
                                        "android_tombstone_protobuf"
                                    } else {
                                        "android_os_trace"
                                    }
                                )
                            }
                        )
                    } catch (error: Exception) {
                        // AstraEH Log Line: One recovery warning per saved session on this launch.
                        Log.warning(
                            "Uberhar exit trace unavailable: session=${session.id} " +
                                error.javaClass.simpleName
                        )
                    } finally {
                        temporary.delete()
                    }
                }
            }
        } catch (error: Exception) {
            // AstraEH Log Line: OS/provider failure never deletes evidence or blocks play.
            Log.warning("Uberhar exit history unavailable: ${error.javaClass.simpleName}")
        }
    }

    @androidx.annotation.RequiresApi(Build.VERSION_CODES.R)
    private fun exitKind(reason: Int): String = when (reason) {
        ApplicationExitInfo.REASON_CRASH -> "java_crash"
        ApplicationExitInfo.REASON_CRASH_NATIVE -> "native_crash"
        ApplicationExitInfo.REASON_ANR -> "anr"
        ApplicationExitInfo.REASON_LOW_MEMORY -> "low_memory"
        ApplicationExitInfo.REASON_SIGNALED -> "signal_exit"
        ApplicationExitInfo.REASON_USER_REQUESTED,
        ApplicationExitInfo.REASON_USER_STOPPED -> "user_exit"
        ApplicationExitInfo.REASON_EXIT_SELF -> "self_exit"
        else -> "other_exit"
    }
}

// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.provider.DocumentsContract
import android.widget.Toast
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean
import org.citra.citra_emu.R

// AstraEH: Keep the existing logger and a tiny lifecycle marker. No duplicate live journal,
// scheduled Android worker, exit-history service or per-launch bundle is needed.
object CrashSessionLogs {
    private val started = AtomicBoolean(false)
    private val warned = AtomicBoolean(false)
    private val recovered = AtomicBoolean(false)
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
            ) { tree, snapshotName ->
                // CodexAstraUlt-2: Rename only; bulk copies/fsyncs run on the existing worker.
                // A deterministic name lets startup retry safely after an interrupted rename.
                val directory = logDirectory(context, tree)
                if (directory != null && findDocument(context, directory, snapshotName) != null) {
                    true
                } else {
                    val file = directory?.let { findDocument(context, it, "azahar_log.txt") }
                    if (file == null) {
                        false
                    } else {
                        val renamed = DocumentsContract.renameDocument(
                            context.contentResolver, file, snapshotName
                        )
                        if (renamed == null) {
                            throw IOException("Cannot preserve interrupted log before rotation")
                        }
                        if (findDocument(context, checkNotNull(directory), snapshotName) == null) {
                            throw IOException("Interrupted log rename could not be verified")
                        }
                        true
                    }
                }
            }
        }.getOrElse {
            warn()
            false
        }
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

    // AstraEH: One short-lived worker per process, no timer or polling. Existing saved text
    // is published even when OS capture is disabled, unsupported or fails.
    fun recoverFiles(androidEvidence: Boolean) {
        if (!recovered.compareAndSet(false, true)) return
        val context = app ?: return
        val logs = store ?: return
        Thread({
            // AstraEH: Recovery IO yields scheduling priority to gameplay and the UI.
            runCatching { Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND) }
            if (androidEvidence) {
                runCatching { AndroidCrashEvidence.collect(context, logs.root) }.onFailure {
                    // AstraEH Log Line: At most once per startup; missing OS data is nonfatal.
                    Log.warning(
                        "Uberhar optional crash evidence unavailable: ${it.javaClass.simpleName}"
                    )
                }
            }
            // CodexAstraUlt-2: Finish incident-only snapshots and legacy migration here,
            // after synchronous startup has secured the source and initialized the sole logger.
            runCatching {
                logs.recoverStaged(
                    openLog = { tree, name ->
                        logDirectory(context, tree)?.let { findDocument(context, it, name) }?.let {
                            context.contentResolver.openInputStream(it)
                                ?: throw IOException("Cannot read interrupted log snapshot")
                        }
                    },
                    deleteLog = { tree, name ->
                        logDirectory(context, tree)?.let { findDocument(context, it, name) }?.let {
                            if (!DocumentsContract.deleteDocument(context.contentResolver, it)) {
                                throw IOException("Cannot remove recovered snapshot")
                            }
                        }
                    }
                )
            }.onFailure { warn() }
            runCatching { logs.migrateLegacy(File(context.filesDir, "uberhar_sessions")) }
                .onFailure { warn() }
            runCatching { CrashReportFiles.publish(context, logs.reports()) }.onSuccess { count ->
                if (count > 0) {
                    // AstraEH Log Line: One file-location pointer after successful incident saves.
                    Log.info("Uberhar crash evidence saved: files=$count directory=log/crashes")
                }
            }.onFailure {
                // AstraEH Log Line: Keep staging bytes for a later retry; do not block play.
                Log.warning(
                    "Uberhar crash-file save pending: directory=log/crashes ${it.javaClass.simpleName}"
                )
            }
        }, "UberharCrashRecovery").apply { isDaemon = true }.start()
    }

    fun checkHealth() {
        if (!Log.fileHealthy()) warn()
    }

    // CodexAstraUlt: Keep the active marker before optional diagnostics, then compare
    // this process baseline with the normal native-return sample across title cycles.
    fun beginRun() {
        markRun(true)
        UberharDeviceDiagnostics.reportProcessMemory("before_native_run")
    }

    fun endRun() {
        // CodexAstraUlt: NativeEmulation's existing finally hook runs after native
        // cleanup on an orderly return. Failure to sample must not prevent the flush.
        UberharDeviceDiagnostics.reportProcessMemory("after_native_run")
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

    // CodexAstraUlt-2: Unavailable storage is an error, not proof that a log is absent.
    // Fail closed so native startup appends instead of rotating potentially unread evidence.
    private fun logDirectory(context: Context, tree: String): Uri? {
        val treeUri = Uri.parse(tree)
        val root = DocumentsContract.buildDocumentUriUsingTree(
            treeUri, DocumentsContract.getTreeDocumentId(treeUri)
        )
        return findDocument(context, root, "log", directory = true)
    }

    // CodexAstraUlt-2: DocumentFile.findFile hides provider query errors as a missing file.
    // Query directly so interrupted evidence is never rotated after an ambiguous lookup.
    private fun findDocument(
        context: Context,
        parent: Uri,
        name: String,
        directory: Boolean = false
    ): Uri? {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(
            parent, DocumentsContract.getDocumentId(parent)
        )
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        )
        val cursor = context.contentResolver.query(children, columns, null, null, null)
            ?: throw IOException("Cannot inspect interrupted log directory")
        cursor.use {
            // CodexAstraUlt-2: Cloud/provider partial results do not prove a file is absent.
            if (it.extras.getBoolean(DocumentsContract.EXTRA_LOADING) ||
                it.extras.getString(DocumentsContract.EXTRA_ERROR) != null
            ) {
                throw IOException("Interrupted log directory lookup incomplete")
            }
            while (it.moveToNext()) {
                if (it.getString(1) == name) {
                    val isDirectory = it.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR
                    if (isDirectory != directory) throw IOException("Unexpected log document type")
                    return DocumentsContract.buildDocumentUriUsingTree(parent, it.getString(0))
                }
            }
        }
        return null
    }

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

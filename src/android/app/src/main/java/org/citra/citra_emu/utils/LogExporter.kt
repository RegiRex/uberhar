// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Export the existing text logs and incident-only saved files through staged copies.
package org.citra.citra_emu.utils

import android.content.Context
import android.net.Uri
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.io.IOException
import java.time.ZoneId
import java.util.UUID

// AstraEH: A concrete provider subclass avoids device-specific direct FileProvider issues.
class LogFileProvider : FileProvider()

object LogExporter {
    const val DIRECTORY = "log_exports"
    data class Choice(
        val source: DocumentFile,
        val current: Boolean,
        val metadata: LogExportNames.Metadata,
        val report: File? = null,
        val older: Boolean = false
    )

    // AstraEH: All provider IO stays off the UI thread. Normal exports enumerate only three
    // rotating logs; the separate incident list contains plain text, never launch bundles.
    fun choices(context: Context, crashReports: Boolean = false): List<Choice> {
        if (crashReports) {
            return CrashSessionLogs.crashStore(context).reports().map { report ->
                val file = DocumentFile.fromFile(report)
                // AstraEH: An old binary OS trace remains exportable after the one-time upgrade.
                // Its companion text supplies titles/date; no new binary traces are collected.
                val text = if (report.name.endsWith(".trace.bin")) {
                    File(report.parentFile, report.name.removeSuffix(".trace.bin") + ".txt")
                } else {
                    report
                }
                val info = runCatching { metadata(context, DocumentFile.fromFile(text)) }
                    .getOrDefault(LogExportNames.Metadata(null, emptyList(), null))
                Choice(file, false, info, report)
            }
        }
        val flushed = Log.flush()
        CrashSessionLogs.checkHealth()
        val directory = runCatching {
            DocumentFile.fromTreeUri(context, PermissionsHandler.citraDirectory)?.findFile("log")
        }.getOrNull()
        return listOf("azahar_log.txt", "azahar_log.old.txt", "azahar_log.older.txt")
            .mapIndexedNotNull { index, name ->
                if (index == 0 && !flushed) return@mapIndexedNotNull null
                runCatching {
                    val file = directory?.findFile(name)?.takeIf { it.isFile }
                        ?: return@mapIndexedNotNull null
                    Choice(file, index == 0, metadata(context, file), older = index == 2)
                }.getOrNull()
            }
    }

    fun filename(choice: Choice, style: LogExportNames.Style): String =
        choice.report?.name ?: LogExportNames.filename(choice.metadata, style)

    fun mimeType(filename: String): String =
        if (filename.endsWith(".bin")) "application/octet-stream" else "text/plain"

    fun deleteSession(context: Context, choice: Choice) {
        val report = choice.report ?: throw IOException("Not a saved crash log")
        CrashSessionLogs.crashStore(context).delete(report)
    }

    private fun metadata(context: Context, source: DocumentFile): LogExportNames.Metadata {
        val input = context.contentResolver.openInputStream(source.uri)
            ?: throw IOException("Cannot read log")
        return input.bufferedReader().use {
            LogExportNames.parse(it.lineSequence(), source.lastModified(), ZoneId.systemDefault())
        }
    }

    // AstraEH: The file picker may outlive the current Activity or app process. Stage a
    // byte-for-byte snapshot first so later log rotation cannot export another session.
    fun snapshot(context: Context, choice: Choice, style: LogExportNames.Style): File {
        val flushed = !choice.current || Log.flush()
        if (!flushed) throw IOException("Log flush did not complete")
        val root = File(context.cacheDir, DIRECTORY).apply { mkdirs() }
        // AstraEH: Only expired export copies are cleaned; never touch source logs or saves.
        val expiry = System.currentTimeMillis() - 7L * 24 * 60 * 60 * 1000
        root.listFiles()?.filter {
            it.isDirectory && it.lastModified() < expiry
        }?.forEach { it.deleteRecursively() }
        val folder = File(root, UUID.randomUUID().toString())
        if (!folder.mkdirs()) throw IOException("Cannot create export directory")
        try {
            choice.report?.let { report ->
                val result = File(folder, report.name)
                report.inputStream().use { input -> result.outputStream().use { input.copyTo(it) } }
                return result
            }
            val temporary = File(folder, "snapshot.txt")
            val input = context.contentResolver.openInputStream(choice.source.uri)
                ?: throw IOException("Cannot read log")
            input.use { source -> temporary.outputStream().use { source.copyTo(it) } }
            val info = temporary.bufferedReader().use {
                LogExportNames.parse(
                    it.lineSequence(),
                    choice.source.lastModified(),
                    ZoneId.systemDefault()
                )
            }
            val result = File(folder, LogExportNames.filename(info, style))
            if (!temporary.renameTo(result)) throw IOException("Cannot name export")
            return result
        } catch (e: Exception) {
            folder.deleteRecursively()
            throw e
        }
    }

    // AstraEH: Saved-state paths can reference only a staged file inside the export root.
    fun restore(context: Context, relative: String): File {
        val root = File(context.cacheDir, DIRECTORY).canonicalFile
        val file = File(root, relative).canonicalFile
        if (!file.path.startsWith(root.path + File.separator) || !file.isFile) {
            throw IOException("Export copy is unavailable; select the log again")
        }
        return file
    }

    fun save(context: Context, snapshot: File, destination: Uri) {
        val output = context.contentResolver.openOutputStream(destination, "wt")
            ?: throw IOException("Cannot write destination")
        output.use { out -> snapshot.inputStream().use { it.copyTo(out) } }
    }
}

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
import org.json.JSONObject
import java.time.Instant

// AstraEH: A concrete provider subclass avoids device-specific direct FileProvider issues.
class LogFileProvider : FileProvider()

object LogExporter {
    const val DIRECTORY = "log_exports"
    data class Choice(
        val source: DocumentFile,
        val current: Boolean,
        val metadata: LogExportNames.Metadata,
        val older: Boolean = false,
        val telemetry: Boolean = false
    )

    // CodexAstraLocal: Required compact telemetry chunks remain independently exportable
    // even after the ordinary file cap; crash-only artifacts stay outside this picker.
    fun choices(context: Context): List<Choice> {
        val flushed = Log.flush()
        CrashSessionLogs.checkHealth()
        val directory = runCatching {
            DocumentFile.fromTreeUri(context, PermissionsHandler.citraDirectory)?.findFile("log")
        }.getOrNull()
        val ordinary = listOf("azahar_log.txt", "azahar_log.old.txt", "azahar_log.older.txt")
            .mapIndexedNotNull { index, name ->
                if (index == 0 && !flushed) return@mapIndexedNotNull null
                runCatching {
                    val file = directory?.findFile(name)?.takeIf { it.isFile }
                        ?: return@mapIndexedNotNull null
                    Choice(file, index == 0, metadata(context, file), older = index == 2)
                }.getOrNull()
            }
        val telemetry = if (!flushed) emptyList() else directory?.listFiles().orEmpty()
            .filter { it.isFile && LogTelemetrySnapshot.accepts(it.name.orEmpty()) }
            .sortedBy { it.name }.map { source ->
                Choice(source, current = true, metadata = LogExportNames.Metadata(
                    source.lastModified().takeIf { it > 0 }?.let {
                        Instant.ofEpochMilli(it).atZone(ZoneId.systemDefault()).toOffsetDateTime()
                    }, listOf(source.name.orEmpty()), "Frame telemetry schema 1"), telemetry = true)
            }
        return ordinary + telemetry
    }

    fun filename(choice: Choice, style: LogExportNames.Style): String =
        if (choice.telemetry) requireNotNull(choice.source.name) else LogExportNames.filename(choice.metadata, style)

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
            val temporary = File(folder, "snapshot.txt")
            val input = context.contentResolver.openInputStream(choice.source.uri)
                ?: throw IOException("Cannot read log")
            if (choice.telemetry) {
                val length = choice.source.length()
                val proof = input.use { LogTelemetrySnapshot.copy(it, temporary, length) }
                temporary.appendText(JSONObject().put("schema", 1).put("kind", "export_snapshot")
                    .put("source", choice.source.name).put("source_bytes", proof.sourceBytes)
                    .put("complete_bytes", proof.completeBytes).put("partial_tail_bytes", proof.partialTailBytes)
                    .put("scope", "explicit export endpoint; source untouched").toString() + "\n")
                val result = File(folder, filename(choice, style))
                if (!temporary.renameTo(result)) throw IOException("Cannot name telemetry export")
                return result
            }
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

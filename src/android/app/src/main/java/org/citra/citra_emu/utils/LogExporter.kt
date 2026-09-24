// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Read only the two emulator logs and export immutable, privately staged copies.
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
        val metadata: LogExportNames.Metadata
    )

    // AstraEH: All calls perform file IO and belong on Dispatchers.IO. Flushing before
    // enumeration makes the displayed titles/date agree with buffered current-session data.
    fun choices(context: Context): List<Choice> {
        val directory = DocumentFile.fromTreeUri(context, PermissionsHandler.citraDirectory)
            ?.findFile("log") ?: return emptyList()
        if (!Log.flush()) {
            throw IOException("Log flush did not complete; retry after exiting the game")
        }
        return listOf("azahar_log.txt", "azahar_log.old.txt").mapIndexedNotNull { index, name ->
            val file =
                directory.findFile(name)?.takeIf { it.isFile } ?: return@mapIndexedNotNull null
            Choice(file, index == 0, metadata(context, file))
        }
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
        if (choice.current && !Log.flush()) throw IOException("Log flush did not complete")
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

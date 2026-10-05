// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import android.content.Context
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.io.IOException

// AstraEH: Reports live in the user's existing log folder, not an in-app crash browser.
// Android's existing document permission suffices; inaccessible storage keeps private evidence.
object CrashReportFiles {
    fun publish(context: Context, reports: List<File>): Int {
        if (reports.isEmpty()) return 0
        val data = DocumentFile.fromTreeUri(context, PermissionsHandler.citraDirectory)
            ?: throw IOException("Data folder unavailable")
        val log = data.findFile("log") ?: data.createDirectory("log")
            ?: throw IOException("Log folder unavailable")
        val directory = log.findFile("crashes") ?: log.createDirectory("crashes")
            ?: throw IOException("Crash folder unavailable")
        val destination = object : CrashReportTransfer.Destination {
            override fun open(name: String) = directory.findFile(name)?.let {
                context.contentResolver.openInputStream(it.uri)
                    ?: throw IOException("Cannot read saved crash file")
            }
            override fun pending(name: String) = run {
                val file = directory.findFile("$name.pending")
                    ?: directory.createFile("application/octet-stream", "$name.pending")
                    ?: throw IOException("Cannot create crash file")
                context.contentResolver.openOutputStream(file.uri, "wt")
                    ?: throw IOException("Cannot write crash file")
            }
            override fun commit(name: String) {
                if (directory.findFile(name) != null) throw IOException("Crash file already exists")
                val file = directory.findFile("$name.pending")
                    ?: throw IOException("Pending crash file missing")
                if (!file.renameTo(name)) throw IOException("Cannot finish crash file")
            }
        }
        // AstraEH: One damaged destination must not block unrelated preserved reports.
        var failure: Exception? = null
        reports.forEach {
            try {
                CrashReportTransfer.publish(it, destination)
            } catch (
                error: Exception
            ) {
                failure = error
            }
        }
        failure?.let { throw it }
        return reports.size
    }
}

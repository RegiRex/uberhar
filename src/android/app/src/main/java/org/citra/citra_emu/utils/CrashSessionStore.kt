// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Properties
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

// AstraEH: Pure filesystem policy, shared with JVM fault/retention tests. No normal rotation
// touches these sessions. Only an explicit UI deletion removes a saved session.
class CrashSessionStore(val root: File) {
    data class Session(val directory: File, val properties: Properties) {
        val id: String get() = directory.name
        val log: File get() = File(directory, "log.txt")
        val startedMs: Long get() = properties.getProperty("started_ms").toLong()
        val pid: Int get() = properties.getProperty("pid").toInt()
    }

    fun create(nowMs: Long, pid: Int, build: String, version: String): Session {
        if (!root.isDirectory &&
            !root.mkdirs()
        ) {
            throw IOException("Cannot create session log folder")
        }
        val directory = File(root, "${stamp(nowMs)}_${UUID.randomUUID()}")
        if (!directory.mkdir()) throw IOException("Cannot create unique session")
        val info = Properties().apply {
            setProperty("schema", "1")
            setProperty("id", directory.name)
            setProperty("started_ms", nowMs.toString())
            setProperty("started_utc", Instant.ofEpochMilli(nowMs).toString())
            setProperty("pid", pid.toString())
            setProperty("build", build)
            setProperty("version", version)
            setProperty("retention", "manual_deletion_only")
        }
        writeProperties(File(directory, "session.properties"), info)
        if (!File(
                directory,
                "log.txt"
            ).createNewFile()
        ) {
            throw IOException("Session log already exists")
        }
        return Session(directory, info)
    }

    fun sessions(): List<Session> = root.listFiles().orEmpty().mapNotNull { directory ->
        if (!directory.isDirectory || !validId.matches(directory.name)) return@mapNotNull null
        val info = runCatching {
            val info = readProperties(File(directory, "session.properties"))
            require(info.getProperty("id") == directory.name)
            info.getProperty("started_ms").toLong()
            info.getProperty("pid").toInt()
            info
        }.getOrElse {
            // AstraEH: A damaged/missing manifest must not hide the log itself. The directory
            // carries its own creation time and UUID; unknown PID/build fields stay unknown.
            Properties().apply {
                setProperty("id", directory.name)
                setProperty(
                    "started_ms",
                    Instant.from(
                        formatter.parse(directory.name.substringBefore('_'))
                    ).toEpochMilli().toString()
                )
                setProperty("pid", "-1")
                setProperty("metadata_recovered", "true")
            }
        }
        Session(directory, info)
    }.sortedByDescending { it.startedMs }

    // AstraEH: Flush/sync the small lifecycle marker; never sync every rendered frame.
    fun markRun(session: Session, active: Boolean, nowMs: Long) {
        writeProperties(
            File(session.directory, "run.properties"),
            Properties().apply {
                setProperty("state", if (active) "active" else "idle")
                setProperty("updated_ms", nowMs.toString())
            }
        )
    }

    fun noteFailure(session: Session, message: String) {
        FileOutputStream(File(session.directory, "failure.txt"), true).use { output ->
            output.write(
                (Instant.now().toString() + " " + message.take(65536) + "\n").toByteArray()
            )
            output.fd.sync()
        }
    }

    fun outcome(session: Session): String {
        val exit = readOptional(File(session.directory, "exit.properties"))
        val kind = exit.getProperty("kind")
        return when {
            kind in setOf(
                "java_crash",
                "native_crash",
                "anr",
                "low_memory",
                "signal_exit"
            ) -> kind!!
            File(session.directory, "failure.txt").exists() -> "recorded_failure"
            kind != null -> kind
            readOptional(
                File(session.directory, "run.properties")
            ).getProperty("state") == "active" ->
                "interrupted_unknown"
            else -> "previous_session"
        }
    }

    // AstraEH: A PID alone can be reused. Prefer the process UUID from Android's state summary;
    // permit a timestamp-bounded PID fallback only when the OS did not preserve that summary.
    fun matchesExit(
        session: Session,
        pid: Int,
        timestamp: Long,
        summary: String?,
        nextStart: Long
    ): Boolean = if (!summary.isNullOrEmpty()) {
        summary == session.id
    } else {
        session.pid == pid && timestamp >= session.startedMs && timestamp < nextStart
    }

    fun exportName(session: Session, live: Boolean = false): String {
        val exit = readOptional(File(session.directory, "exit.properties"))
        val actual = exit.getProperty("timestamp_ms")?.toLongOrNull()
        val time = actual?.let { "exit_${stamp(it)}" } ?: "start_${stamp(session.startedMs)}"
        val kind = if (live) "current_session" else outcome(session)
        return "uberhar_${kind}_${time}_${session.id.substringAfter('_')}.zip"
    }

    fun snapshot(
        session: Session,
        destination: File,
        live: Boolean = false,
        flushed: Boolean = true
    ) {
        requireOwned(session)
        ZipOutputStream(destination.outputStream().buffered()).use { zip ->
            // AstraEH: Fixed filenames prevent traversal and exclude unfinished temporary writes.
            archiveFiles.forEach { name ->
                val source = File(session.directory, name)
                if (source.isFile) {
                    zip.putNextEntry(ZipEntry(name))
                    val snapshotLength = source.length()
                    source.inputStream().use { input ->
                        val buffer = ByteArray(8192)
                        var remaining = snapshotLength
                        while (remaining > 0) {
                            val count = input.read(
                                buffer,
                                0,
                                minOf(buffer.size.toLong(), remaining).toInt()
                            )
                            if (count < 0) break
                            zip.write(buffer, 0, count)
                            remaining -= count
                        }
                    }
                    zip.closeEntry()
                }
            }
            zip.putNextEntry(ZipEntry("export.properties"))
            zip.write(
                (
                    "live_session=$live\nflush_complete=$flushed\n" +
                        "metadata_recovered=${session.properties.getProperty(
                            "metadata_recovered",
                            "false"
                        )}\n"
                    ).toByteArray()
            )
            zip.closeEntry()
        }
    }

    fun delete(session: Session, currentId: String?) {
        requireOwned(session)
        require(session.id != currentId) { "Cannot delete the active process session" }
        if (!session.directory.deleteRecursively()) throw IOException("Cannot delete saved session")
    }

    fun sizeBytes(session: Session): Long = archiveFiles.sumOf {
        File(session.directory, it).length()
    }

    private fun requireOwned(session: Session) {
        require(
            validId.matches(session.id) &&
                session.directory.canonicalFile.parentFile == root.canonicalFile
        )
    }

    companion object {
        private val validId = Regex("[0-9]{8}T[0-9]{6}\\.[0-9]{3}Z_[0-9a-f-]{36}")
        private val formatter = DateTimeFormatter.ofPattern(
            "yyyyMMdd'T'HHmmss.SSS'Z'"
        ).withZone(ZoneOffset.UTC)
        private val archiveFiles =
            listOf(
                "log.txt",
                "session.properties",
                "run.properties",
                "failure.txt",
                "exit.properties",
                "trace.bin",
                "trace.properties"
            )

        fun stamp(ms: Long): String = formatter.format(Instant.ofEpochMilli(ms))

        fun readProperties(file: File): Properties = Properties().apply {
            if (file.isFile) file.inputStream().buffered().use { load(it) }
        }

        private fun readOptional(file: File): Properties = runCatching {
            readProperties(file)
        }.getOrDefault(Properties())

        fun writeProperties(file: File, properties: Properties) {
            val temporary = File(file.parentFile, "${file.name}.${UUID.randomUUID()}.tmp")
            try {
                FileOutputStream(temporary).use { output ->
                    properties.store(output, "AstraEH: Uberhar session evidence")
                    output.fd.sync()
                }
                try {
                    Files.move(
                        temporary.toPath(),
                        file.toPath(),
                        StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING
                    )
                } catch (_: AtomicMoveNotSupportedException) {
                    Files.move(
                        temporary.toPath(),
                        file.toPath(),
                        StandardCopyOption.REPLACE_EXISTING
                    )
                }
            } finally {
                temporary.delete()
            }
        }
    }
}

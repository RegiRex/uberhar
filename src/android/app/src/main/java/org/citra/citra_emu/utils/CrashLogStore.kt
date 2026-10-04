// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Properties
import java.util.UUID

// AstraEH: One small marker accompanies the existing rotating log. Only interrupted runs
// and recorded errors create retained text; detection and cleanup need no OS exit history.
class CrashLogStore(val root: File) {
    private val marker get() = File(root, "pending.properties")
    private var current: Properties? = null

    // AstraEH: Commit saved text before replacing the marker or allowing native rotation.
    // A failed copy leaves the original marker/log intact and makes the writer append.
    @Synchronized
    fun prepare(nowMs: Long, tree: String, openLog: (String) -> InputStream?): Boolean {
        current = null
        root.mkdirs()
        if (marker.exists()) {
            val previous = readProperties(marker)
            require(validId.matches(previous.getProperty("id", "")))
            if (previous.getProperty("active") == "true" || previous.containsKey("failure")) {
                val target =
                    File(root, "uberhar_interrupted_start_${previous.getProperty("id")}.txt")
                if (!target.isFile) {
                    saveText(target) { output ->
                        val input = openLog(previous.getProperty("tree"))
                        val available = input != null
                        input?.use { it.copyTo(output) }
                        output.write(
                            (
                                "\n\nUberhar interruption report\n" +
                                    "detected_utc=${Instant.ofEpochMilli(nowMs)}\n" +
                                    "log_available=$available\n" +
                                    "exit_reason=unknown; interruption does not prove a crash\n" +
                                    "timestamp_basis=process_start; crash time unknown\n" +
                                    "started_utc=${previous.getProperty("started_utc")}\n" +
                                    "run_active=${previous.getProperty("active")}\n" +
                                    "${previous.getProperty("failure", "")}\n"
                                ).toByteArray()
                        )
                    }
                }
            }
        }
        val next = Properties().apply {
            setProperty("id", "${stamp(nowMs)}_${UUID.randomUUID()}")
            setProperty("started_utc", Instant.ofEpochMilli(nowMs).toString())
            setProperty("tree", tree)
            setProperty("active", "false")
        }
        writeProperties(marker, next)
        current = next
        return true
    }

    @Synchronized
    fun markRun(active: Boolean, nowMs: Long) {
        val state = current ?: throw IOException("Previous log recovery is pending")
        state.setProperty("active", active.toString())
        state.setProperty("updated_utc", Instant.ofEpochMilli(nowMs).toString())
        writeProperties(marker, state)
    }

    @Synchronized
    fun noteFailure(message: String) {
        val state = current ?: throw IOException("Previous log recovery is pending")
        // AstraEH: Bound exception text even if several errors precede a restart.
        state.setProperty(
            "failure",
            (
                state.getProperty("failure", "") + "\n" +
                    Instant.now() + " " + message
                ).takeLast(65536)
        )
        writeProperties(marker, state)
    }

    fun reports(): List<File> = root.listFiles().orEmpty().filter {
        it.isFile &&
            it.name.startsWith("uberhar_") &&
            (it.extension == "txt" || it.name.endsWith(".trace.bin"))
    }.sortedByDescending { it.name }

    fun delete(report: File) {
        require(report.canonicalFile.parentFile == root.canonicalFile && report in reports())
        if (!report.delete()) throw IOException("Cannot delete crash log")
    }

    // AstraEH: Upgrade old folders once. Remove ordinary duplicate logs; retain incident text.
    // Move already-collected binary traces separately without creating new traces or ZIPs.
    fun migrateLegacy(legacyRoot: File) {
        legacyRoot.listFiles().orEmpty().filter {
            it.isDirectory && validId.matches(it.name)
        }.forEach { directory ->
            val run = optionalProperties(File(directory, "run.properties"))
            val exit = optionalProperties(File(directory, "exit.properties"))
            val trace = File(directory, "trace.bin")
            val failure = File(directory, "failure.txt")
            val log = File(directory, "log.txt")
            val incident = run.getProperty("state") != "idle" ||
                failure.length() > 0 ||
                trace.length() > 0 ||
                exit.getProperty("kind") in setOf(
                    "java_crash",
                    "native_crash",
                    "anr",
                    "low_memory",
                    "signal_exit"
                )
            if (incident &&
                (
                    log.length() > 0 ||
                        failure.length() > 0 ||
                        trace.length() > 0 ||
                        exit.isNotEmpty()
                    )
            ) {
                val target = File(root, "uberhar_legacy_${directory.name}.txt")
                if (!target.isFile) {
                    saveText(target) { output ->
                        if (log.isFile) log.inputStream().use { it.copyTo(output) }
                        output.write("\n\nUberhar preserved legacy evidence\n".toByteArray())
                        listOf(
                            "session.properties",
                            "run.properties",
                            "failure.txt",
                            "exit.properties",
                            "trace.properties"
                        ).forEach { name ->
                            val source = File(directory, name)
                            if (source.isFile) {
                                output.write("\n[$name]\n".toByteArray())
                                source.inputStream().use { input ->
                                    val bytes = ByteArray(65536)
                                    var total = 0
                                    while (total < bytes.size) {
                                        val count = input.read(bytes, total, bytes.size - total)
                                        if (count < 0) break
                                        total += count
                                    }
                                    output.write(bytes, 0, total)
                                }
                            }
                        }
                    }
                }
                if (trace.length() > 0) {
                    val targetTrace = File(root, "uberhar_legacy_${directory.name}.trace.bin")
                    if (targetTrace.exists() || !trace.renameTo(targetTrace)) {
                        throw IOException("Cannot move legacy crash trace safely")
                    }
                }
            }
            // AstraEH: Unknown files from a different layout are never silently discarded.
            val known = setOf(
                "session.properties",
                "run.properties",
                "log.txt",
                "failure.txt",
                "exit.properties",
                "trace.properties",
                "trace.tmp",
                "export.properties"
            )
            directory.listFiles().orEmpty().filter { it.isFile && it.name in known }.forEach {
                if (!it.delete()) throw IOException("Cannot remove migrated duplicate")
            }
            directory.delete()
        }
        legacyRoot.delete()
    }

    private fun saveText(target: File, write: (FileOutputStream) -> Unit) {
        root.mkdirs()
        val temporary = File(root, target.name + ".tmp")
        try {
            FileOutputStream(temporary).use { output ->
                write(output)
                output.fd.sync()
            }
            // AstraEH: Never replace committed evidence on a recovery retry.
            if (target.exists() ||
                !temporary.renameTo(target)
            ) {
                throw IOException("Cannot save crash log")
            }
        } finally {
            temporary.delete()
        }
    }

    companion object {
        private val formatter = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss.SSS'Z'")
            .withZone(ZoneOffset.UTC)
        private val validId = Regex("\\d{8}T\\d{6}\\.\\d{3}Z_[0-9a-fA-F-]{36}")
        private fun stamp(nowMs: Long) = formatter.format(Instant.ofEpochMilli(nowMs))
        private fun readProperties(file: File) = Properties().apply {
            file.inputStream().use { load(it) }
        }
        private fun optionalProperties(file: File) = runCatching { readProperties(file) }
            .getOrDefault(Properties())

        // AstraEH: Replace a synced tiny marker atomically on the app's own filesystem.
        private fun writeProperties(file: File, state: Properties) {
            val temporary = File(file.parentFile, file.name + ".tmp")
            FileOutputStream(temporary).use { output ->
                state.store(output, "AstraEH: Uberhar run marker")
                output.fd.sync()
            }
            if (!temporary.renameTo(file)) throw IOException("Cannot replace run marker")
        }
    }
}

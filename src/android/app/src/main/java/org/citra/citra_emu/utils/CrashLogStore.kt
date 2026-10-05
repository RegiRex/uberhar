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
    // CodexAstraUlt-2: Keep retry metadata until the old lifecycle marker is replaced, or a
    // failed startup followed by publication could reuse its incident ID for appended bytes.
    @Volatile private var recoveryBlockedId: String? = null

    // CodexAstraUlt-2: Snapshot by provider rename before native rotation. Only tiny metadata
    // is synced on startup; the existing recovery worker copies the immutable incident later.
    // A provider that cannot snapshot leaves the original marker/log intact for append mode.
    @Synchronized
    fun prepare(nowMs: Long, tree: String, stageLog: (String, String) -> Boolean): Boolean {
        current = null
        recoveryBlockedId = null
        root.mkdirs()
        if (marker.exists()) {
            val previous = readProperties(marker)
            val id = previous.getProperty("id", "")
            require(validId.matches(id))
            if (previous.getProperty("active") == "true" || previous.containsKey("failure")) {
                recoveryBlockedId = id
                val target = reportFile(id)
                if (!target.isFile) {
                    val recovery = File(root, "recovery_$id.properties")
                    val state = if (recovery.isFile) {
                        readProperties(recovery)
                    } else {
                        previous.apply {
                            setProperty("detected_utc", Instant.ofEpochMilli(nowMs).toString())
                        }.also { writeProperties(recovery, it) }
                    }
                    if (state.getProperty("captured") != "true") {
                        // CodexAstraUlt-2: A restart between rename and this commit reuses the
                        // deterministic snapshot instead of renaming a subsequent current log.
                        val available = stageLog(state.getProperty("tree"), snapshotName(id))
                        state.setProperty("log_available", available.toString())
                        state.setProperty("captured", "true")
                        writeProperties(recovery, state)
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
        recoveryBlockedId = null
        return true
    }

    // CodexAstraUlt-2: Recovery never holds the lifecycle-marker monitor during bulk IO.
    // Failed copies retain their sidecar/snapshot; cleanup follows the committed internal copy.
    fun recoverStaged(
        openLog: (String, String) -> InputStream?,
        deleteLog: (String, String) -> Unit
    ) {
        var failure: Exception? = null
        root.listFiles().orEmpty().filter {
            it.isFile && it.name.startsWith("recovery_") && it.extension == "properties"
        }.forEach { recovery ->
            try {
                val state = readProperties(recovery)
                val id = state.getProperty("id", "")
                require(validId.matches(id) && recovery.name == "recovery_$id.properties")
                if (id == recoveryBlockedId || state.getProperty("captured") != "true") {
                    return@forEach
                }
                val tree = state.getProperty("tree")
                val name = snapshotName(id)
                val available = state.getProperty("log_available") == "true"
                val target = reportFile(id)
                if (state.getProperty("saved") != "true") {
                    if (!target.isFile) {
                        saveText(target) { output ->
                            if (available) {
                                val input = openLog(tree, name)
                                    ?: throw IOException("Interrupted log snapshot unavailable")
                                input.use { it.copyTo(output) }
                            }
                            output.write(
                                (
                                    "\n\nUberhar interruption report\n" +
                                        "detected_utc=${state.getProperty("detected_utc")}\n" +
                                        "log_available=$available\n" +
                                        "exit_reason=unknown; interruption does not prove a crash\n" +
                                        "timestamp_basis=process_start; crash time unknown\n" +
                                        "started_utc=${state.getProperty("started_utc")}\n" +
                                        "run_active=${state.getProperty("active")}\n" +
                                        "${state.getProperty("failure", "")}\n"
                                    ).toByteArray()
                            )
                        }
                    }
                    // CodexAstraUlt-2: Publishing can retire the internal report even when
                    // cleanup fails. Commit that copy before deleting its provider snapshot.
                    state.setProperty("saved", "true")
                    writeProperties(recovery, state)
                }
                if (available) deleteLog(tree, name)
                if (!recovery.delete()) throw IOException("Cannot remove recovered marker")
            } catch (error: Exception) {
                // CodexAstraUlt-2: One unavailable provider must not block other incidents.
                failure = error
            }
        }
        failure?.let { throw it }
    }

    // CodexAstraUlt-2: IDs come only from validated markers, keeping snapshot paths local.
    private fun reportFile(id: String) = File(root, "uberhar_interrupted_start_$id.txt")
    private fun snapshotName(id: String) = "uberhar_interrupted_start_$id.txt.pending"

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

    // CodexAstraUlt-2: Retain a preexisting private report until its old lifecycle ID retires;
    // verified publication deletes private copies and must not reopen that ID for reuse.
    fun reports(): List<File> = root.listFiles().orEmpty().filter {
        it.name != recoveryBlockedId?.let { id -> reportFile(id).name } &&
            it.isFile &&
            it.name.startsWith("uberhar_") &&
            (
                it.extension == "txt" ||
                    it.name.endsWith(".trace.bin") ||
                    it.name.endsWith(".tombstone.pb")
                )
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

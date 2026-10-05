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
import java.util.UUID

// AstraEH: Android is only an optional evidence supplier. This bounded filesystem policy is
// testable without Android and never changes the normal logger or emulation lifecycle.
class AndroidCrashReportStore(private val root: File) {
    data class Identity(val build: String, val token: String)
    data class Exit(
        val identity: Identity,
        val timestampMs: Long,
        val pid: Int,
        val kind: String,
        val status: Int,
        val process: String,
        val description: String,
        val pssKiB: Long,
        val rssKiB: Long,
        val nativeTrace: Boolean
    ) {
        val key get() = "${timestampMs}_${pid}_${identity.token}"
    }

    private val seenFile get() = File(root, "android_reported.txt")
    private val seen = if (seenFile.isFile) {
        seenFile.inputStream().use { input ->
            val bytes = ByteArray(8192)
            val count = input.read(bytes)
            if (count > 0) {
                String(bytes, 0, count).lineSequence().filter { it.isNotEmpty() }
                    .take(32).toMutableSet()
            } else {
                mutableSetOf()
            }
        }
    } else {
        mutableSetOf()
    }

    fun hasSeen(exit: Exit) = exit.key in seen

    fun record(exit: Exit, openTrace: () -> InputStream?): File? {
        require(exit.timestampMs >= 0 && exit.pid > 0)
        require(parseIdentity("uberhar1|${exit.identity.build}|${exit.identity.token}") != null)
        if (hasSeen(exit)) return null
        root.mkdirs()
        val stamp = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss.SSS'Z'")
            .withZone(ZoneOffset.UTC).format(Instant.ofEpochMilli(exit.timestampMs))
        val base = "uberhar_android_${stamp}_${exit.pid}_${exit.identity.token}"
        val report = File(root, "$base.txt")
        if (!report.exists()) {
            val trace = File(root, base + if (exit.nativeTrace) ".tombstone.pb" else ".trace.bin")
            val traceState = if (trace.isFile) "available" else saveTrace(trace, openTrace)
            val text = """
                Uberhar optional Android exit report
                schema=1
                build=${exit.identity.build}
                process_token=${exit.identity.token}
                exit_utc=${Instant.ofEpochMilli(exit.timestampMs)}
                timestamp_source=android_process_exit
                collected_utc=${Instant.now()}
                pid=${exit.pid}
                process=${exit.process.take(256)}
                exit_kind=${exit.kind}
                exit_status_or_signal=${exit.status}
                pss_kib=${exit.pssKiB}
                rss_kib=${exit.rssKiB}
                memory_scope=last_os_sample_not_peak_or_proof_of_cause
                description=${exit.description.take(4096)}
                trace_status=$traceState
                trace_file=${if (trace.isFile) trace.name else "none"}
                trace_format=${if (exit.nativeTrace) "android_tombstone_protobuf" else "android_os_trace"}
                trace_limit_bytes=$TRACE_LIMIT
                interpretation=OS evidence; exit reason alone may not identify the underlying bug
            """.trimIndent() + "\n"
            commit(report) { it.write(text.toByteArray()) }
        }
        // AstraEH: Remember only 32 captured identities, including reports the owner later deletes.
        // A committed report survives a failed seen-marker write and is reused on the next launch.
        seen.add(exit.key)
        while (seen.size > 32) seen.remove(seen.first())
        commit(seenFile, replace = true) {
            it.write(seen.joinToString("\n", postfix = "\n").toByteArray())
        }
        return report
    }

    private fun saveTrace(target: File, open: () -> InputStream?): String {
        return try {
            val input = open() ?: return "unavailable"
            input.use { source ->
                commit(target) { output ->
                    val buffer = ByteArray(8192)
                    var total = 0
                    while (true) {
                        val count = source.read(buffer)
                        if (count < 0) break
                        total += count
                        if (total > TRACE_LIMIT) throw TraceTooLarge()
                        output.write(buffer, 0, count)
                    }
                    if (total == 0) throw IOException("Empty OS trace")
                }
            }
            "available"
        } catch (_: TraceTooLarge) {
            "omitted_size_limit"
        } catch (error: Exception) {
            "unavailable_${error.javaClass.simpleName}"
        }
    }

    private fun commit(target: File, replace: Boolean = false, write: (FileOutputStream) -> Unit) {
        val temp = File(root, target.name + ".tmp")
        try {
            FileOutputStream(temp).use {
                write(it)
                it.fd.sync()
            }
            if ((!replace && target.exists()) || !temp.renameTo(target)) {
                throw IOException("Cannot commit crash evidence")
            }
        } finally {
            temp.delete()
        }
    }

    private class TraceTooLarge : IOException()

    companion object {
        const val TRACE_LIMIT = 8 * 1024 * 1024

        // AstraEH: Tag only this process/build, once per launch; no paths, game names or PII.
        fun identity(build: String) = "uberhar1|${build.take(48)}|${UUID.randomUUID()}"
        fun parseIdentity(summary: String?): Identity? {
            val parts = summary?.split('|') ?: return null
            if (parts.size != 3 ||
                parts[0] != "uberhar1" ||
                !parts[1].matches(Regex("[a-zA-Z0-9._+-]{1,48}")) ||
                !parts[2].matches(Regex("[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}"))
            ) {
                return null
            }
            return Identity(parts[1], parts[2])
        }
    }
}

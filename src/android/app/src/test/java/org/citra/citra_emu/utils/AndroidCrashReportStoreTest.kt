// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

// AstraEH: Fault tests use production storage/identity policy, without Android services.
class AndroidCrashReportStoreTest {
    private fun withRoot(test: (File) -> Unit) {
        val root = Files.createTempDirectory("uberhar-android-crash").toFile()
        try {
            test(root)
        } finally {
            root.deleteRecursively()
        }
    }
    private fun exit(index: Long = 1) = AndroidCrashReportStore.Exit(
        AndroidCrashReportStore.parseIdentity(AndroidCrashReportStore.identity("99c5239"))!!,
        1791150000000 + index, 42, "native_crash", 11, "org.uberhar", "native failure",
        1234, 5678, true
    )

    @Test fun identityRejectsUnattributedAndMalformedRecords() {
        val summary = AndroidCrashReportStore.identity("a".repeat(40))
        assertTrue(summary.toByteArray().size <= 128)
        assertEquals("a".repeat(40), AndroidCrashReportStore.parseIdentity(summary)!!.build)
        assertNull(AndroidCrashReportStore.parseIdentity(null))
        assertNull(AndroidCrashReportStore.parseIdentity("old-session-id"))
        assertNull(AndroidCrashReportStore.parseIdentity(summary.replace("uberhar1", "other")))
        assertNull(AndroidCrashReportStore.parseIdentity(summary + "|extra"))
        assertNull(
            AndroidCrashReportStore.parseIdentity(summary.replace("a".repeat(40), "../path"))
        )
    }

    @Test fun ordinaryStoreCreatesNoArchives() = withRoot { root ->
        AndroidCrashReportStore(root)
        assertTrue(root.list()!!.isEmpty())
    }

    @Test fun capturesExactTraceWithKnownBuildAndTimestamp() = withRoot { root ->
        val event = exit()
        val bytes = byteArrayOf(0, -1, 7, 0, 42)
        val report = AndroidCrashReportStore(root).record(event) { bytes.inputStream() }!!
        val text = report.readText()
        assertTrue(text.contains("build=99c5239"))
        assertTrue(text.contains("exit_status_or_signal=11"))
        assertTrue(text.contains("timestamp_source=android_process_exit"))
        assertTrue(text.contains("trace_status=available"))
        assertArrayEquals(bytes, root.listFiles()!!.single { it.extension == "pb" }.readBytes())
    }

    @Test fun restartAndUserDeletionDoNotRecaptureSameExit() = withRoot { root ->
        val event = exit()
        AndroidCrashReportStore(root).record(event) { null }!!.delete()
        assertNull(AndroidCrashReportStore(root).record(event) { error("Must not reopen trace") })
        assertEquals(listOf("android_reported.txt"), root.list()!!.toList())
    }

    @Test fun missingOrFailingOsTraceStillSavesExitDetails() = withRoot { root ->
        val store = AndroidCrashReportStore(root)
        val absent = store.record(exit()) { null }!!
        assertTrue(absent.readText().contains("trace_status=unavailable"))
        val error = store.record(exit(2)) { throw IOException("gone") }!!
        assertTrue(error.readText().contains("trace_status=unavailable_IOException"))
        val empty = store.record(exit(3)) { byteArrayOf().inputStream() }!!
        assertTrue(empty.readText().contains("trace_status=unavailable_IOException"))
        assertFalse(root.list()!!.any { it.endsWith(".tmp") || it.endsWith(".pb") })
    }

    @Test fun oversizedTraceIsOmittedWithoutLosingExitSummary() = withRoot { root ->
        var consumed = 0
        val stream = object : InputStream() {
            override fun read(): Int {
                consumed++
                return 7
            }
        }
        val report = AndroidCrashReportStore(root).record(exit()) { stream }!!
        assertTrue(consumed <= AndroidCrashReportStore.TRACE_LIMIT + 8192)
        assertTrue(report.readText().contains("trace_status=omitted_size_limit"))
        assertFalse(root.list()!!.any { it.endsWith(".pb") || it.endsWith(".tmp") })
    }

    @Test fun committedTraceSurvivesInterruptedReportWrite() = withRoot { root ->
        val event = exit().copy(timestampMs = 1)
        // Fail only the final report commit by preoccupying its temporary path.
        val reportName = "uberhar_android_19700101T000000.001Z_42_${event.identity.token}.txt"
        val blocker = File(root, "$reportName.tmp").apply { mkdir() }
        assertThrows(IOException::class.java) {
            AndroidCrashReportStore(root).record(event) { byteArrayOf(1, 2, 3).inputStream() }
        }
        blocker.delete()
        val report = AndroidCrashReportStore(root).record(event) { error("Retain prior trace") }!!
        assertTrue(report.readText().contains("trace_status=available"))
        assertEquals(1, root.listFiles()!!.count { it.extension == "pb" })
    }

    @Test fun seenLedgerAndDescriptionAreBounded() = withRoot { root ->
        val store = AndroidCrashReportStore(root)
        repeat(40) {
            store.record(exit(it.toLong()).copy(description = "x".repeat(100000))) { null }
        }
        assertEquals(32, File(root, "android_reported.txt").readLines().size)
        assertTrue(File(root, "android_reported.txt").length() < 8192)
        assertTrue(
            root.listFiles()!!.filter { it.name.startsWith("uberhar_") }.all {
                it.length() <
                    6000
            }
        )
    }
}

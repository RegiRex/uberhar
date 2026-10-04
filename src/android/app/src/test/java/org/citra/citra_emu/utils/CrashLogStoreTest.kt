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
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

// AstraEH: Exercise restart/IO failures against production policy and real files without Android.
class CrashLogStoreTest {
    private fun withStore(test: (CrashLogStore) -> Unit) {
        val root = Files.createTempDirectory("uberhar-crash-logs").toFile()
        try {
            test(CrashLogStore(root))
        } finally {
            root.deleteRecursively()
        }
    }

    @Test fun ordinaryLaunchesKeepOnlyOneMarker() = withStore { first ->
        repeat(40) { index ->
            val store = CrashLogStore(first.root)
            assertTrue(store.prepare(index.toLong(), "tree") { error("No ordinary log copy") })
            store.markRun(true, index.toLong())
            store.markRun(false, index.toLong())
        }
        assertEquals(listOf("pending.properties"), first.root.list()!!.toList())
        assertTrue(first.root.walkTopDown().filter { it.isFile }.sumOf { it.length() } < 2048)
    }

    @Test fun interruptedTextSurvivesRepeatedRestarts() = withStore { first ->
        first.prepare(1000, "original-tree") { null }
        first.markRun(true, 1010)
        val second = CrashLogStore(first.root)
        second.prepare(2000, "new-tree") { tree ->
            assertEquals("original-tree", tree)
            "last gameplay records".byteInputStream()
        }
        val report = second.reports().single()
        val bytes = report.readBytes()
        assertTrue(report.readText().startsWith("last gameplay records"))
        assertTrue(report.readText().contains("exit_reason=unknown"))
        repeat(8) { CrashLogStore(first.root).prepare(2000, "tree") { null } }
        assertArrayEquals(bytes, report.readBytes())
        assertEquals(1, second.reports().size)
    }

    @Test fun failedCopyKeepsMarkerAndAllowsRetry() = withStore { first ->
        first.prepare(1000, "tree") { null }
        first.markRun(true, 1010)
        val marker = File(first.root, "pending.properties")
        val before = marker.readBytes()
        val next = CrashLogStore(first.root)
        assertThrows(IOException::class.java) {
            next.prepare(2000, "tree") {
                object : InputStream() {
                    override fun read(): Int = throw IOException("provider gone")
                }
            }
        }
        assertArrayEquals(before, marker.readBytes())
        assertTrue(next.reports().isEmpty())
        assertFalse(first.root.list()!!.any { it.endsWith(".tmp") })
        assertThrows(IOException::class.java) { next.markRun(true, 2001) }
        assertTrue(next.prepare(3000, "tree") { "recovered source".byteInputStream() })
        assertTrue(next.reports().single().readText().contains("recovered source"))
    }

    @Test fun committedReportIsNotRewrittenAfterMarkerFailure() = withStore { first ->
        first.prepare(1000, "tree") { null }
        first.markRun(true, 1010)
        val blocked = File(first.root, "pending.properties.tmp").apply { mkdir() }
        val next = CrashLogStore(first.root)
        assertThrows(IOException::class.java) {
            next.prepare(2000, "tree") { "original".byteInputStream() }
        }
        val report = next.reports().single()
        val bytes = report.readBytes()
        assertTrue(blocked.delete())
        next.prepare(3000, "tree") { error("Committed report must not be copied twice") }
        assertArrayEquals(bytes, report.readBytes())
    }

    @Test fun recordedErrorSurvivesNormalTeardown() = withStore { first ->
        first.prepare(1000, "tree") { null }
        first.markRun(true, 1001)
        first.noteFailure("uncaught_java known exception")
        first.markRun(false, 1002)
        CrashLogStore(first.root).prepare(2000, "tree") { "ordinary log".byteInputStream() }
        assertTrue(first.reports().single().readText().contains("known exception"))
    }

    @Test fun missingLogRetainsMarkerEvidence() = withStore { first ->
        first.prepare(1000, "tree") { null }
        first.markRun(true, 1001)
        CrashLogStore(first.root).prepare(2000, "tree") { null }
        assertTrue(first.reports().single().readText().contains("run_active=true"))
    }

    @Test fun clockRollbackDoesNotOverwriteIncident() = withStore { first ->
        first.prepare(1000, "tree") { null }
        repeat(3) {
            first.markRun(true, 1001)
            first.prepare(1000, "tree") { "event $it".byteInputStream() }
        }
        assertEquals(3, first.reports().size)
    }

    @Test fun damagedMarkerIsNotSilentlyDiscarded() = withStore { first ->
        File(first.root, "pending.properties").writeText("broken")
        assertThrows(IllegalArgumentException::class.java) { first.prepare(1000, "tree") { null } }
        assertEquals("broken", File(first.root, "pending.properties").readText())
    }

    @Test fun errorTextIsBoundedAndActiveMarkerCannotBeDeleted() = withStore { first ->
        first.prepare(1000, "tree") { null }
        repeat(3) { first.noteFailure("a".repeat(100000)) }
        val marker = File(first.root, "pending.properties")
        assertTrue(marker.length() < 70000)
        assertThrows(IllegalArgumentException::class.java) { first.delete(marker) }
        first.prepare(2000, "tree") { null }
        first.delete(first.reports().single())
        assertTrue(first.reports().isEmpty())
    }

    @Test fun legacyUpgradeRemovesDuplicatesAndPreservesEvidence() = withStore { first ->
        val legacy = File(first.root, "legacy").apply { mkdir() }
        fun folder(n: Int, state: String): File {
            val name = "20261004T120000.000Z_00000000-0000-0000-0000-00000000000$n"
            return File(legacy, name).apply {
                mkdir()
                File(this, "run.properties").writeText("state=$state")
                File(this, "session.properties").writeText("version=0.1.15")
            }
        }
        val ordinary = folder(0, "idle")
        File(ordinary, "log.txt").writeText("duplicate ordinary log")
        val crashed = folder(1, "active")
        File(crashed, "log.txt").writeText("crash context")
        File(crashed, "failure.txt").writeText("known error")
        val bytes = byteArrayOf(0, -1, 7, 42)
        File(crashed, "trace.bin").writeBytes(bytes)
        first.migrateLegacy(legacy)
        assertFalse(legacy.exists())
        assertEquals(2, first.reports().size)
        assertArrayEquals(bytes, first.reports().single { it.extension == "bin" }.readBytes())
        val text = first.reports().single { it.extension == "txt" }.readText()
        assertTrue(text.contains("crash context") && text.contains("known error"))
        assertFalse(text.contains("duplicate ordinary log"))
        first.migrateLegacy(legacy)
        assertEquals(2, first.reports().size)
    }
}

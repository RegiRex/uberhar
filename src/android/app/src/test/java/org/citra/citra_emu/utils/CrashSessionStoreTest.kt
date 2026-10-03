// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.File
import java.nio.file.Files
import java.util.Properties
import java.util.zip.ZipFile
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

// AstraEH: Test retention/identity against real files, not a second implementation of the policy.
class CrashSessionStoreTest {
    private fun withStore(test: (CrashSessionStore) -> Unit) {
        val root = Files.createTempDirectory("uberhar-session-test").toFile()
        try {
            test(CrashSessionStore(root))
        } finally {
            root.deleteRecursively()
        }
    }

    @Test fun interruptedSessionSurvivesRepeatedStartsAndHasUniqueIdentity() = withStore { store ->
        val first = store.create(1000, 1, "build-a", "0.1.14")
        first.log.writeText("last written crash context\n")
        store.markRun(first, true, 1001)
        repeat(12) { store.create(1000, it + 2, "build-b", "0.1.14") }
        assertEquals(13, CrashSessionStore(store.root).sessions().size)
        assertEquals("last written crash context\n", first.log.readText())
        assertEquals("interrupted_unknown", store.outcome(first))
        assertEquals(13, store.sessions().map { it.id }.toSet().size)
    }

    @Test fun exitMatchingRejectsReusedPidAndHonorsUuidAcrossClockChanges() = withStore { store ->
        val first = store.create(1000, 42, "a", "0.1.14")
        assertTrue(store.matchesExit(first, 42, 1200, null, 1500))
        assertFalse(store.matchesExit(first, 42, 999, null, 1500))
        assertFalse(store.matchesExit(first, 42, 1500, null, 1500))
        assertFalse(store.matchesExit(first, 42, 1200, "different-session", 1500))
        assertFalse(store.matchesExit(first, 43, 1200, null, 1500))
        assertTrue(store.matchesExit(first, 42, 500, first.id, 1500))
    }

    @Test fun handledFailureRemainsAfterOrderlyGameStop() = withStore { store ->
        val session = store.create(1000, 1, "a", "0.1.14")
        store.markRun(session, true, 1001)
        store.noteFailure(session, "shader unavailable")
        store.markRun(session, false, 1002)
        assertEquals("recorded_failure", store.outcome(session))
        assertTrue(File(session.directory, "failure.txt").readText().contains("shader unavailable"))
    }

    @Test fun bundleRetainsBytesAndDistinguishesKnownExitTime() = withStore { store ->
        val session = store.create(1000, 1, "a", "0.1.14")
        session.log.writeText("saved log\n")
        File(session.directory, "trace.bin").writeBytes(byteArrayOf(0, 1, 2, -1))
        CrashSessionStore.writeProperties(
            File(session.directory, "exit.properties"),
            Properties().apply {
                setProperty("kind", "native_crash")
                setProperty("timestamp_ms", "2000")
            }
        )
        val output = File(store.root, store.exportName(session))
        assertTrue(output.name.contains("native_crash_exit_${CrashSessionStore.stamp(2000)}"))
        store.snapshot(session, output, live = true, flushed = false)
        ZipFile(output).use { zip ->
            assertEquals(
                "saved log\n",
                zip.getInputStream(zip.getEntry("log.txt")).reader().readText()
            )
            assertArrayEquals(
                byteArrayOf(0, 1, 2, -1),
                zip.getInputStream(zip.getEntry("trace.bin")).readBytes()
            )
            assertTrue(
                zip.getInputStream(
                    zip.getEntry("export.properties")
                ).reader().readText().contains("flush_complete=false")
            )
        }
        assertTrue(session.log.exists())
    }

    @Test fun deleteCannotRemoveCurrentOrUnrelatedDirectory() = withStore { store ->
        val session = store.create(1000, 1, "a", "0.1.14")
        assertThrows(IllegalArgumentException::class.java) { store.delete(session, session.id) }
        val outside = Files.createTempDirectory("uberhar-unrelated").toFile()
        try {
            val forged = CrashSessionStore.Session(outside, session.properties)
            assertThrows(IllegalArgumentException::class.java) { store.delete(forged, null) }
            assertTrue(outside.exists())
        } finally {
            outside.deleteRecursively()
        }
        store.delete(session, null)
        assertFalse(session.directory.exists())
    }

    @Test fun missingTraceAndDamagedManifestDoNotEraseOtherSessions() = withStore { store ->
        val good = store.create(1000, 1, "a", "0.1.14")
        val bad = store.create(2000, 2, "a", "0.1.14")
        File(bad.directory, "session.properties").writeText("invalid")
        assertEquals(setOf(good.id, bad.id), store.sessions().map { it.id }.toSet())
        val recovered = store.sessions().first { it.id == bad.id }
        assertEquals(2000, recovered.startedMs)
        assertEquals("true", recovered.properties.getProperty("metadata_recovered"))
        assertTrue(bad.directory.exists())
        assertTrue(store.exportName(good).contains("_start_"))
        val output = File(store.root, "no-trace.zip")
        store.snapshot(good, output)
        ZipFile(output).use { assertNull(it.getEntry("trace.bin")) }
    }
}

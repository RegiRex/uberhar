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
    // AstraEH: Regression: empty/ordinary launches must not appear as crash reports.
    @Test fun crashViewFiltersOrdinarySessions() = withStore { store ->
        val idle = store.create(1000, 1, "a", "0.1.15")
        store.markRun(idle, false, 1001)
        assertFalse(store.isReportable(idle, null))
        idle.log.writeText("a normal game log")
        assertFalse(store.isReportable(idle, null))
        store.markRun(idle, true, 1002)
        assertTrue(store.isReportable(idle, null))
        assertFalse(store.isReportable(idle, idle.id))
        idle.log.writeText("")
        assertFalse(store.isReportable(idle, null))
        CrashSessionStore.writeProperties(
            File(idle.directory, "exit.properties"),
            Properties().apply { setProperty("kind", "native_crash") }
        )
        assertTrue(store.isReportable(idle, null))
        store.removeEmptyIdleSessions(null)
        assertTrue(idle.directory.exists())
    }

    @Test fun cleanupPreservesIncidentsAndUnknownExits() = withStore { store ->
        val current = store.create(20000, 99, "a", "0.1.15")
        store.markRun(current, false, 20001)
        val unknown = store.create(1000, 1, "a", "0.1.15")
        unknown.log.writeText("unknown exit")
        store.markRun(unknown, false, 1001)
        val active = store.create(2000, 2, "a", "0.1.15")
        store.markRun(active, true, 2001)
        val crash = store.create(3000, 3, "a", "0.1.15")
        store.markRun(crash, false, 3001)
        CrashSessionStore.writeProperties(
            File(crash.directory, "exit.properties"),
            Properties().apply { setProperty("kind", "native_crash") }
        )
        val empty = store.create(4000, 4, "a", "0.1.15")
        store.markRun(empty, false, 4001)
        repeat(5) { n ->
            val clean = store.create(5000L + n, 10 + n, "a", "0.1.15")
            clean.log.writeText("ordinary log")
            store.markRun(clean, false, 6000)
            CrashSessionStore.writeProperties(
                File(clean.directory, "exit.properties"),
                Properties().apply { setProperty("kind", "user_exit") }
            )
        }
        store.removeEmptyIdleSessions(current.id)
        store.pruneConfirmedCleanSessions(current.id)
        assertFalse(empty.directory.exists())
        assertTrue(listOf(current, unknown, active, crash).all { it.directory.exists() })
        assertEquals(6, store.sessions().size)
    }

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

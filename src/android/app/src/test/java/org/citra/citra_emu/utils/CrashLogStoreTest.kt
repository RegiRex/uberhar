// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.util.Properties
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

// AstraEH: Exercise restart/IO failures against production policy and real files without Android.
class CrashLogStoreTest {
    // CodexAstraUlt-2: Model provider rename with actual files; stage never opens log contents.
    private class Fixture(val root: File) {
        val store = CrashLogStore(File(root, "store"))
        val source = File(root, "source").apply { mkdir() }
        val marker get() = File(store.root, "pending.properties")
        var opens = 0

        fun current(tree: String = "tree") = File(File(source, tree).apply { mkdir() }, "azahar_log.txt")
        fun snapshot(tree: String, name: String) = File(File(source, tree), name)
        fun stage(tree: String, name: String): Boolean {
            val target = snapshot(tree, name)
            if (target.isFile) return true
            val log = current(tree)
            if (!log.exists()) return false
            if (!log.renameTo(target)) throw IOException("provider rename failed")
            return true
        }
        fun open(tree: String, name: String): InputStream? {
            opens++
            return snapshot(tree, name).takeIf { it.exists() }?.inputStream()
        }
        fun delete(tree: String, name: String) {
            val log = snapshot(tree, name)
            if (log.exists() && !log.delete()) throw IOException("provider delete failed")
        }
        fun prepare(store: CrashLogStore = this.store, nowMs: Long = 1000, tree: String = "tree") =
            store.prepare(nowMs, tree, ::stage)
        fun recover(store: CrashLogStore = this.store) = store.recoverStaged(::open, ::delete)
        fun incident() {
            prepare()
            current().writeText("last gameplay records")
            store.markRun(true, 1010)
        }
    }

    private fun withFixture(test: (Fixture) -> Unit) {
        val root = Files.createTempDirectory("uberhar-crash-logs").toFile()
        try {
            test(Fixture(root))
        } finally {
            root.deleteRecursively()
        }
    }

    private fun withStore(test: (CrashLogStore) -> Unit) = withFixture {
        it.store.root.mkdirs()
        test(it.store)
    }

    @Test fun ordinaryLaunchesKeepOnlyOneMarker() = withFixture { fixture ->
        repeat(40) { index ->
            val store = CrashLogStore(fixture.store.root)
            assertTrue(store.prepare(index.toLong(), "tree") { _, _ -> error("No ordinary log copy") })
            store.markRun(true, index.toLong())
            store.markRun(false, index.toLong())
        }
        assertEquals(listOf("pending.properties"), fixture.store.root.list()!!.toList())
        assertTrue(fixture.store.root.walkTopDown().filter { it.isFile }.sumOf { it.length() } < 2048)
    }

    // CodexAstraUlt-2: Startup stages without reading the file; recovery preserves the old tree.
    @Test fun interruptedTextSurvivesRepeatedRestarts() = withFixture { fixture ->
        fixture.prepare(tree = "original-tree")
        fixture.current("original-tree").writeText("last gameplay records")
        fixture.store.markRun(true, 1010)
        val second = CrashLogStore(fixture.store.root)
        fixture.prepare(second, 2000, "new-tree")
        assertEquals(0, fixture.opens)
        assertTrue(second.reports().isEmpty())
        assertFalse(fixture.current("original-tree").exists())
        fixture.current("new-tree").writeText("subsequent gameplay")
        fixture.recover(second)
        val report = second.reports().single()
        val bytes = report.readBytes()
        assertTrue(report.readText().startsWith("last gameplay records"))
        assertTrue(report.readText().contains("exit_reason=unknown"))
        assertFalse(report.readText().contains("subsequent gameplay"))
        repeat(8) { fixture.prepare(CrashLogStore(fixture.store.root), 2000) }
        assertArrayEquals(bytes, report.readBytes())
        assertEquals(1, second.reports().size)
    }

    // CodexAstraUlt-2: A failed rename leaves native append mode with the exact source/marker.
    @Test fun failedSnapshotKeepsMarkerAndAllowsRetry() = withFixture { fixture ->
        fixture.incident()
        val before = fixture.marker.readBytes()
        val next = CrashLogStore(fixture.store.root)
        assertThrows(IOException::class.java) {
            next.prepare(2000, "tree") { _, _ -> throw IOException("provider gone") }
        }
        assertArrayEquals(before, fixture.marker.readBytes())
        assertEquals("last gameplay records", fixture.current().readText())
        assertThrows(IOException::class.java) { next.markRun(true, 2001) }
        next.recoverStaged({ _, _ -> error("Uncommitted snapshots must not be opened") }) { _, _ -> }
        assertTrue(fixture.prepare(next, 3000))
        fixture.recover(next)
        assertTrue(next.reports().single().readText().contains("last gameplay records"))
    }

    // CodexAstraUlt-2: A bulk-copy failure is retriable without touching the new run's marker.
    @Test fun failedBackgroundCopyKeepsSnapshotAndCurrentRun() = withFixture { fixture ->
        fixture.incident()
        val next = CrashLogStore(fixture.store.root)
        fixture.prepare(next, 2000)
        next.markRun(true, 2001)
        val before = fixture.marker.readBytes()
        assertThrows(IOException::class.java) {
            next.recoverStaged({ _, _ ->
                object : InputStream() {
                    override fun read(): Int = throw IOException("provider gone")
                }
            }) { _, _ -> error("Cannot delete failed copy") }
        }
        assertArrayEquals(before, fixture.marker.readBytes())
        assertTrue(next.reports().isEmpty())
        assertFalse(next.root.list()!!.any { it.endsWith(".tmp") })
        fixture.recover(next)
        assertTrue(next.reports().single().readText().contains("last gameplay records"))
    }

    // CodexAstraUlt-2: Cover a process/storage failure immediately after the provider rename.
    @Test fun renamedSnapshotSurvivesCaptureCommitFailure() = withFixture { fixture ->
        fixture.incident()
        val before = fixture.marker.readBytes()
        val next = CrashLogStore(fixture.store.root)
        lateinit var blocked: File
        assertThrows(IOException::class.java) {
            next.prepare(2000, "tree") { tree, name ->
                val staged = fixture.stage(tree, name)
                val recovery = next.root.listFiles()!!.single { it.name.startsWith("recovery_") }
                blocked = File(next.root, recovery.name + ".tmp").apply { mkdir() }
                staged
            }
        }
        assertArrayEquals(before, fixture.marker.readBytes())
        assertTrue(blocked.delete())
        fixture.current().writeText("append fallback from next process")
        fixture.prepare(next, 3000)
        assertEquals("append fallback from next process", fixture.current().readText())
        fixture.recover(next)
        assertTrue(next.reports().single().readText().startsWith("last gameplay records"))
    }

    @Test fun stagedReportIsNotReplacedAfterMarkerFailure() = withFixture { fixture ->
        fixture.incident()
        val blocked = File(fixture.store.root, "pending.properties.tmp").apply { mkdir() }
        val next = CrashLogStore(fixture.store.root)
        assertThrows(IOException::class.java) { fixture.prepare(next, 2000) }
        next.recoverStaged({ _, _ -> error("Keep snapshot until lifecycle marker commits") }) { _, _ -> }
        assertTrue(next.reports().isEmpty())
        assertTrue(blocked.delete())
        fixture.current().writeText("newer source")
        fixture.prepare(next, 3000)
        fixture.recover(next)
        assertEquals("newer source", fixture.current().readText())
        assertTrue(next.reports().single().readText().startsWith("last gameplay records"))
    }

    // CodexAstraUlt-2: Existing 0.1.17 private reports must also survive marker-commit failure.
    @Test fun preexistingReportCannotPublishBeforeOldMarkerIsReplaced() = withFixture { fixture ->
        fixture.incident()
        val state = Properties().apply { fixture.marker.inputStream().use { load(it) } }
        val report = File(
            fixture.store.root,
            "uberhar_interrupted_start_${state.getProperty("id")}.txt"
        ).apply { writeText("previously committed evidence") }
        val blocked = File(fixture.store.root, "pending.properties.tmp").apply { mkdir() }
        val next = CrashLogStore(fixture.store.root)
        assertThrows(IOException::class.java) { fixture.prepare(next, 2000) }
        fixture.recover(next)
        assertTrue(next.reports().isEmpty())
        assertEquals("previously committed evidence", report.readText())
        assertTrue(blocked.delete())
        fixture.prepare(next, 3000)
        assertEquals(report, next.reports().single())
    }

    // CodexAstraUlt-2: Committed bytes survive a failed source cleanup and retries do not recopy.
    @Test fun committedReportSurvivesSnapshotCleanupFailure() = withFixture { fixture ->
        fixture.incident()
        val next = CrashLogStore(fixture.store.root)
        fixture.prepare(next, 2000)
        assertThrows(IOException::class.java) {
            next.recoverStaged(fixture::open) { _, _ -> throw IOException("delete failed") }
        }
        val report = next.reports().single()
        val before = report.readBytes()
        next.recoverStaged({ _, _ -> error("Committed report must not be copied twice") }, fixture::delete)
        assertArrayEquals(before, report.readBytes())
        assertFalse(next.root.list()!!.any { it.startsWith("recovery_") })
    }

    // CodexAstraUlt-2: Verified publication may retire the copy while cleanup awaits a retry.
    @Test fun publishedReportDoesNotRequireReopeningSnapshotDuringCleanupRetry() = withFixture { fixture ->
        fixture.incident()
        val next = CrashLogStore(fixture.store.root)
        fixture.prepare(next, 2000)
        assertThrows(IOException::class.java) {
            next.recoverStaged(fixture::open) { tree, name ->
                fixture.delete(tree, name)
                throw IOException("cleanup interrupted after source deletion")
            }
        }
        val report = next.reports().single()
        val published = report.readBytes()
        assertTrue(report.delete())
        next.recoverStaged({ _, _ -> error("Published report needs only cleanup") }, fixture::delete)
        assertFalse(next.root.list()!!.any { it.startsWith("recovery_") })
        assertTrue(String(published).startsWith("last gameplay records"))
    }

    // CodexAstraUlt-2: Slow storage recovery must not block gameplay lifecycle/failure markers.
    @Test fun blockedBackgroundCopyDoesNotLockLifecycleMarker() = withFixture { fixture ->
        fixture.incident()
        val next = CrashLogStore(fixture.store.root)
        fixture.prepare(next, 2000)
        val reading = CountDownLatch(1)
        val release = CountDownLatch(1)
        val executor = Executors.newFixedThreadPool(2)
        try {
            val recovery = executor.submit {
                next.recoverStaged({ tree, name ->
                    reading.countDown()
                    check(release.await(5, TimeUnit.SECONDS))
                    fixture.open(tree, name)
                }, fixture::delete)
            }
            assertTrue(reading.await(5, TimeUnit.SECONDS))
            executor.submit { next.markRun(true, 2001) }.get(2, TimeUnit.SECONDS)
            executor.submit { next.noteFailure("current run failure") }.get(2, TimeUnit.SECONDS)
            release.countDown()
            recovery.get(5, TimeUnit.SECONDS)
            assertTrue(fixture.marker.readText().contains("current run failure"))
        } finally {
            release.countDown()
            executor.shutdownNow()
        }
    }

    @Test fun recordedErrorSurvivesNormalTeardown() = withFixture { fixture ->
        fixture.incident()
        fixture.store.noteFailure("uncaught_java known exception")
        fixture.store.markRun(false, 1002)
        val next = CrashLogStore(fixture.store.root)
        fixture.prepare(next, 2000)
        fixture.recover(next)
        assertTrue(next.reports().single().readText().contains("known exception"))
    }

    @Test fun missingLogRetainsMarkerEvidence() = withFixture { fixture ->
        fixture.prepare()
        fixture.store.markRun(true, 1001)
        val next = CrashLogStore(fixture.store.root)
        fixture.prepare(next, 2000)
        fixture.recover(next)
        assertTrue(next.reports().single().readText().contains("run_active=true"))
        assertTrue(next.reports().single().readText().contains("log_available=false"))
    }

    // CodexAstraUlt-2: Several interrupted launches retain independent immutable snapshots.
    @Test fun clockRollbackDoesNotOverwriteIncident() = withFixture { fixture ->
        fixture.prepare()
        repeat(3) {
            fixture.current().writeText("event $it")
            fixture.store.markRun(true, 1001)
            fixture.prepare(nowMs = 1000)
        }
        fixture.recover()
        assertEquals(3, fixture.store.reports().size)
    }

    @Test fun damagedMarkerIsNotSilentlyDiscarded() = withFixture { fixture ->
        fixture.store.root.mkdirs()
        fixture.marker.writeText("broken")
        assertThrows(IllegalArgumentException::class.java) { fixture.prepare() }
        assertEquals("broken", fixture.marker.readText())
    }

    @Test fun errorTextIsBoundedAndActiveMarkerCannotBeDeleted() = withFixture { fixture ->
        fixture.prepare()
        repeat(3) { fixture.store.noteFailure("a".repeat(100000)) }
        assertTrue(fixture.marker.length() < 70000)
        assertThrows(IllegalArgumentException::class.java) { fixture.store.delete(fixture.marker) }
        fixture.prepare(nowMs = 2000)
        fixture.recover()
        fixture.store.delete(fixture.store.reports().single())
        assertTrue(fixture.store.reports().isEmpty())
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

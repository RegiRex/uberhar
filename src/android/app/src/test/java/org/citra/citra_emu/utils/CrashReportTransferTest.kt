// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

// AstraEH: Provider faults must never consume the only valid crash report.
class CrashReportTransferTest {
    private class Sink : CrashReportTransfer.Destination {
        var bytes: ByteArray? = null
        var pending = ByteArrayOutputStream()
        var fail = false
        override fun open(name: String) = bytes?.inputStream()
        override fun pending(name: String): ByteArrayOutputStream {
            if (fail) throw IOException("Permission revoked")
            pending = ByteArrayOutputStream()
            return pending
        }
        override fun commit(name: String) {
            bytes = pending.toByteArray()
        }
    }
    private fun withSource(test: (File, Sink) -> Unit) {
        val folder = Files.createTempDirectory("uberhar-crash-transfer").toFile()
        val file = File(folder, "crash.txt").apply { writeText("precious crash evidence") }
        try {
            test(file, Sink())
        } finally {
            folder.deleteRecursively()
        }
    }

    @Test fun verifiedDestinationRetiresPrivateCopy() = withSource { file, sink ->
        val bytes = file.readBytes()
        CrashReportTransfer.publish(file, sink)
        assertArrayEquals(bytes, sink.bytes)
        assertFalse(file.exists())
    }

    @Test fun failedProviderPreservesSource() = withSource { file, sink ->
        sink.fail = true
        assertThrows(IOException::class.java) { CrashReportTransfer.publish(file, sink) }
        assertTrue(file.exists())
        sink.fail = false
        CrashReportTransfer.publish(file, sink)
        assertFalse(file.exists())
    }

    @Test fun retryRecognizesAlreadyCommittedBytes() = withSource { file, sink ->
        sink.bytes = file.readBytes()
        sink.fail = true
        CrashReportTransfer.publish(file, sink)
        assertFalse(file.exists())
    }

    @Test fun existingDifferentReportIsNeverOverwritten() = withSource { file, sink ->
        sink.bytes = "other crash".toByteArray()
        assertThrows(IOException::class.java) { CrashReportTransfer.publish(file, sink) }
        assertTrue(file.exists())
        assertEquals("other crash", String(sink.bytes!!))
    }

    @Test fun damagedDestinationKeepsSource() = withSource { file, _ ->
        var reads = 0
        val sink = object : CrashReportTransfer.Destination {
            override fun open(name: String) = if (reads++ ==
                0
            ) {
                null
            } else {
                "truncated".byteInputStream()
            }
            override fun pending(name: String) = ByteArrayOutputStream()
            override fun commit(name: String) {}
        }
        assertThrows(IOException::class.java) { CrashReportTransfer.publish(file, sink) }
        assertTrue(file.exists())
    }
}

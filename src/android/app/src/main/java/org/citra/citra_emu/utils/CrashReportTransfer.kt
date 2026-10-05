// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
package org.citra.citra_emu.utils

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest

// AstraEH: Publish incident files only. Verify destination bytes before retiring the private
// staging copy, and never overwrite an existing report with different content.
object CrashReportTransfer {
    interface Destination {
        fun open(name: String): InputStream?
        fun pending(name: String): OutputStream
        fun commit(name: String)
    }

    fun publish(source: File, destination: Destination) {
        val expected = source.inputStream().use { digest(it) }
        val existing = destination.open(source.name)?.use { digest(it) }
        if (existing != null) {
            if (existing !=
                expected
            ) {
                throw IOException("Existing crash report differs; keeping both sources")
            }
        } else {
            source.inputStream().use { input ->
                destination.pending(source.name).use { input.copyTo(it) }
            }
            destination.commit(source.name)
            val actual = destination.open(source.name)?.use { digest(it) }
            if (actual != expected) throw IOException("Crash report verification failed")
        }
        if (!source.delete()) throw IOException("Cannot retire verified staging copy")
    }

    private fun digest(input: InputStream): String {
        val hash = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(8192)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            hash.update(buffer, 0, count)
        }
        return hash.digest().joinToString("") { "%02x".format(it) }
    }
}

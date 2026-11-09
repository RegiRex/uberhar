// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
package org.citra.citra_emu.utils

import java.io.File
import java.io.InputStream
import java.io.IOException
import java.io.RandomAccessFile

/** CodexAstraLocal: A bounded, newline-framed export snapshot. The source remains untouched;
 * bytes appended after the chosen endpoint are for the next export, not an endless copy. */
object LogTelemetrySnapshot {
    private val name = Regex("azahar_log\\.txt\\.frames-[0-9]+-[0-9]+\\.jsonl")
    fun accepts(filename: String) = name.matches(filename)
    data class Proof(val sourceBytes: Long, val completeBytes: Long, val partialTailBytes: Long)
    fun copy(input: InputStream, output: File, sourceBytes: Long): Proof {
        require(sourceBytes >= 0)
        var copied = 0L
        var complete = 0L
        val buffer = ByteArray(64 * 1024)
        output.outputStream().use { out ->
            while (copied < sourceBytes) {
                val count = input.read(buffer, 0, minOf(buffer.size.toLong(), sourceBytes - copied).toInt())
                if (count <= 0) throw IOException("Telemetry source ended before its snapshot endpoint")
                for (i in 0 until count) if (buffer[i] == 10.toByte()) complete = copied + i + 1
                out.write(buffer, 0, count)
                copied += count
            }
        }
        RandomAccessFile(output, "rw").use { it.setLength(complete) }
        return Proof(sourceBytes, complete, sourceBytes - complete)
    }
}

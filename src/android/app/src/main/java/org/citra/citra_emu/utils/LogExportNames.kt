// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Pure parsing/naming logic shared by log exports and JVM regression tests.
package org.citra.citra_emu.utils

import java.net.URLDecoder
import java.text.Normalizer
import java.time.Instant
import java.time.OffsetDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

object LogExportNames {
    enum class Style { INITIALS, FIRST_THREE }

    data class Metadata(val date: OffsetDateTime?, val games: List<String>, val build: String?)

    // AstraEH: Read metadata from this log, never the active game's settings/library list.
    // The previous file's mtime is used only when pre-0.0.4 logs lack a session marker.
    fun parse(lines: Sequence<String>, modifiedMillis: Long, zone: ZoneId): Metadata {
        var date: OffsetDateTime? = null
        var build: String? = null
        var explicitTitlePending = false
        val games = linkedMapOf<String, String>()
        fun add(title: String) {
            val clean = title.replace(Regex("\\s+"), " ").trim()
            if (clean.isNotEmpty()) games.putIfAbsent(clean.lowercase(Locale.ROOT), clean)
        }
        for (line in lines) {
            when {
                line.contains("Uberhar log session: ") && date == null -> {
                    date = runCatching {
                        OffsetDateTime.parse(line.substringAfter("Uberhar log session: ").trim())
                    }.getOrNull()
                }

                line.contains("Azahar Version: ") && build == null -> {
                    build = line.substringAfter("Azahar Version: ").trim()
                }

                line.contains("Uberhar game title: ") -> {
                    val title = line.substringAfter("Uberhar game title: ").trim()
                    add(title)
                    explicitTitlePending = title.isNotEmpty()
                }

                line.contains("[EmulationFragment] Starting application ") -> {
                    if (!explicitTitlePending) {
                        add(
                            legacyTitle(
                                line.substringAfter("[EmulationFragment] Starting application ")
                            )
                        )
                    }
                    explicitTitlePending = false
                }
            }
        }
        return Metadata(
            date
                ?: modifiedMillis.takeIf {
                    it > 0
                }?.let { Instant.ofEpochMilli(it).atZone(zone).toOffsetDateTime() },
            games.values.toList(),
            build
        )
    }

    // AstraEH: Older logs only expose paths. Decode SAF escapes without turning literal
    // plus signs into spaces, then remove the extension and trailing region/dump tags.
    internal fun legacyTitle(path: String): String {
        val decoded = runCatching {
            URLDecoder.decode(path.replace("+", "%2B"), "UTF-8")
        }.getOrDefault(path)
        val name = decoded.substringAfterLast('/').substringAfterLast('\\')
        val stem = name.replace(
            Regex("\\.(cci|cxi|3ds|3dsx|zcci|zcxi|z3dsx|app|elf|axf)$", RegexOption.IGNORE_CASE),
            ""
        )
        return stem.replace(Regex("(?:\\s*\\([^()]*\\)|\\s*\\[[^\\[\\]]*\\])+$"), "").trim()
    }

    // AstraEH: Keep numeric words intact (3D, 7); single-word titles use up to three
    // characters. ASCII output is portable across Android, Windows and cloud providers.
    fun abbreviation(title: String, style: Style): String {
        val ascii = Normalizer.normalize(title, Normalizer.Form.NFKD)
            .replace(Regex("\\p{M}+"), "").uppercase(Locale.ROOT)
        val words = Regex("[A-Z0-9]+").findAll(ascii).map { it.value }.toList()
        if (words.isEmpty()) return "GAME"
        if (style == Style.FIRST_THREE || words.size == 1) return words.joinToString("").take(3)
        return words.joinToString("") { if (it.first().isDigit()) it else it.take(1) }.take(32)
    }

    // AstraEH: Deduplicate replayed games while preserving play order. Keep filenames
    // below common filesystem limits; an overflow count preserves notice of extra games.
    fun filename(metadata: Metadata, style: Style): String {
        val stamp =
            metadata.date?.format(DateTimeFormatter.ofPattern("M_d_HHmm", Locale.ROOT))
                ?: "UNKNOWN_DATE"
        val prefix = "uberhar_log_$stamp"
        val suffixes = metadata.games.map { abbreviation(it, style) }
        if (suffixes.isEmpty()) return "${prefix}_NO_GAME.txt"
        val name = StringBuilder(prefix)
        for ((index, suffix) in suffixes.withIndex()) {
            if (name.length + suffix.length + 1 > 175) {
                name.append("_PLUS").append(suffixes.size - index)
                break
            }
            name.append('_').append(suffix)
        }
        return "$name.txt"
    }
}

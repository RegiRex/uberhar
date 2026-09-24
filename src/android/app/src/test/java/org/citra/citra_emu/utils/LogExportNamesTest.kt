// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Behavioral regression tests for real exported names and session attribution.
package org.citra.citra_emu.utils

import java.time.OffsetDateTime
import java.time.ZoneId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LogExportNamesTest {
    private val date = OffsetDateTime.parse("2026-09-24T01:00:00-04:00")
    private val zone = ZoneId.of("America/New_York")
    private fun parse(vararg lines: String, modified: Long = date.toInstant().toEpochMilli()) =
        LogExportNames.parse(lines.asSequence(), modified, zone)

    @Test fun ownerExamplesPreserveNumbersAndOrder() {
        val metadata = LogExportNames.Metadata(
            date,
            listOf(
                "Fire Emblem Awakening",
                "The Legend of Zelda: Ocarina of Time 3D",
                "Mario Kart 7"
            ),
            null
        )
        assertEquals(
            "uberhar_log_9_24_0100_FEA_TLOZOOT3D_MK7.txt",
            LogExportNames.filename(metadata, LogExportNames.Style.INITIALS)
        )
        assertEquals(
            "uberhar_log_9_24_0100_FIR_THE_MAR.txt",
            LogExportNames.filename(metadata, LogExportNames.Style.FIRST_THREE)
        )
    }

    @Test fun sessionDateWinsOverExportTimeAndDeviceTimeZone() {
        val result = parse("[0] Uberhar log session: 2025-12-31T23:59:00+09:00")
        assertEquals(
            "uberhar_log_12_31_2359_NO_GAME.txt",
            LogExportNames.filename(result, LogExportNames.Style.INITIALS)
        )
    }

    @Test fun oldLogsUseRecordedFileDateAndPathTitles() {
        val result =
            parse(
                "[1] [EmulationFragment] Starting application !/Roms/Fire Emblem - Awakening (USA).cci"
            )
        assertEquals(listOf("Fire Emblem - Awakening"), result.games)
        assertEquals(
            "uberhar_log_9_24_0100_FEA.txt",
            LogExportNames.filename(result, LogExportNames.Style.INITIALS)
        )
    }

    @Test fun explicitTitleReplacesRomFilenameAndReplaysAreDeduplicated() {
        val result = parse(
            "[0] Azahar Version: example | test-build",
            "[1] Uberhar game title: Mario Kart 7",
            "[1] [EmulationFragment] Starting application !/sdmc/00000000.app",
            "[2] Uberhar game title: MARIO KART 7",
            "[2] [EmulationFragment] Starting application !/sdmc/00000000.app",
            "[3] Uberhar game title: Fire Emblem Awakening",
            "[3] [EmulationFragment] Starting application !/Roms/a.cci"
        )
        assertEquals(listOf("Mario Kart 7", "Fire Emblem Awakening"), result.games)
        assertEquals("example | test-build", result.build)
    }

    @Test fun emptyTitleFallsBackToPath() {
        val result =
            parse(
                "[1] Uberhar game title: ",
                "[1] [EmulationFragment] Starting application /Roms/Mario Kart 7.cci"
            )
        assertEquals(listOf("Mario Kart 7"), result.games)
    }

    @Test fun encodedPathsAndRegionTagsAreHandled() {
        assertEquals(
            "Mario Kart 7",
            LogExportNames.legacyTitle(
                "content://storage/primary%3ARoms%2FMario%20Kart%207%20(USA)%20(Rev%201).cci"
            )
        )
        assertEquals("A+B", LogExportNames.legacyTitle("!/Roms/A+B [USA].3ds"))
    }

    @Test fun shortTitlesAreNotPadded() {
        for (style in LogExportNames.Style.entries) {
            assertEquals("IT", LogExportNames.abbreviation("It", style))
            assertEquals("X", LogExportNames.abbreviation("X", style))
            assertEquals("KIR", LogExportNames.abbreviation("Kirby", style))
        }
    }

    @Test fun missingOrMalformedDateDoesNotInventASessionTime() {
        val result = parse("[1] Uberhar log session: broken", modified = 0)
        assertNull(result.date)
        assertEquals(
            "uberhar_log_UNKNOWN_DATE_NO_GAME.txt",
            LogExportNames.filename(result, LogExportNames.Style.INITIALS)
        )
    }

    @Test fun filenamesArePortableAndBounded() {
        val titles = (1..100).map { "Pokémon / Title: $it?" }
        val name = LogExportNames.filename(
            LogExportNames.Metadata(date, titles, null),
            LogExportNames.Style.INITIALS
        )
        assertTrue(name.matches(Regex("[A-Za-z0-9_.]+")))
        assertTrue(name.length < 200)
        assertTrue(name.contains("_PLUS"))
        assertEquals(
            "POK",
            LogExportNames.abbreviation("Pokémon", LogExportNames.Style.FIRST_THREE)
        )
        assertEquals("GAME", LogExportNames.abbreviation("日本語", LogExportNames.Style.INITIALS))
    }

    @Test fun choosingAnotherLogDoesNotMixSessionTitles() {
        val current = parse("[1] Uberhar game title: Mario Kart 7")
        val previous = parse("[1] Uberhar game title: Fire Emblem Awakening")
        assertTrue(
            LogExportNames.filename(current, LogExportNames.Style.INITIALS).endsWith("_MK7.txt")
        )
        assertTrue(
            LogExportNames.filename(previous, LogExportNames.Style.INITIALS).endsWith("_FEA.txt")
        )
    }
}

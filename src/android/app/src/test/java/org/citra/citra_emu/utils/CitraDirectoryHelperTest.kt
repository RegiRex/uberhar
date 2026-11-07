// CodexAstraUlt-2: Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// CodexAstraUlt-2: Licensed under GPLv2 or any later version.
// CodexAstraUlt-2: Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

// CodexAstraUlt-2: Execute the real selection transaction against controlled
// storage endpoints; only DocumentFile lookups, enumeration and creation are modeled.
class CitraDirectoryHelperTest {
    private class Folder(initialEntries: Set<String>, private val canCreate: Boolean = true) {
        val entries = initialEntries.toMutableSet()
        val createAttempts = mutableListOf<String>()
        var enumerations = 0

        fun prepare(): Boolean = CitraDirectoryHelper.prepareUberharDirectory(
            markerExists = { it in entries },
            isEmpty = {
                enumerations++
                entries.isEmpty()
            },
            createMarker = {
                createAttempts.add(it)
                canCreate && entries.add(it)
            }
        )
    }

    @Test
    fun mainUberharFolderIsRejectedWithoutChanges() {
        val contents = setOf("uberhar-data.txt", "config", "sdmc", "shaders")
        val folder = Folder(contents)
        assertFalse(folder.prepare())
        assertEquals(contents, folder.entries)
        assertTrue(folder.createAttempts.isEmpty())
        assertEquals(0, folder.enumerations)
    }

    @Test
    fun copiedCandidateMarkerDoesNotAuthorizeMainFolder() {
        val contents = setOf("uberhar-data.txt", "uberhar-precompiled-data.txt", "sdmc")
        val folder = Folder(contents)
        assertFalse(folder.prepare())
        assertEquals(contents, folder.entries)
        assertTrue(folder.createAttempts.isEmpty())
        assertEquals(0, folder.enumerations)
    }

    @Test
    fun candidateFolderCanBeReusedWithoutRewritingMarker() {
        val contents = setOf("uberhar-precompiled-data.txt", "config", "sdmc")
        val folder = Folder(contents)
        assertTrue(folder.prepare())
        assertEquals(contents, folder.entries)
        assertTrue(folder.createAttempts.isEmpty())
        assertEquals(0, folder.enumerations)
    }

    @Test
    fun emptyFolderGetsOnlyCandidateMarker() {
        val folder = Folder(emptySet())
        assertTrue(folder.prepare())
        assertEquals(setOf("uberhar-precompiled-data.txt"), folder.entries)
        assertEquals(listOf("uberhar-precompiled-data.txt"), folder.createAttempts)
        assertEquals(1, folder.enumerations)
    }

    @Test
    fun failedMarkerCreationRejectsEmptyFolder() {
        val folder = Folder(emptySet(), canCreate = false)
        assertFalse(folder.prepare())
        assertTrue(folder.entries.isEmpty())
        assertEquals(listOf("uberhar-precompiled-data.txt"), folder.createAttempts)
    }

    @Test
    fun populatedUnmarkedFolderIsRejectedWithoutChanges() {
        val contents = setOf("config", "sdmc")
        val folder = Folder(contents)
        assertFalse(folder.prepare())
        assertEquals(contents, folder.entries)
        assertTrue(folder.createAttempts.isEmpty())
        assertEquals(1, folder.enumerations)
    }

    @Test
    fun similarMarkerNameDoesNotAuthorizePopulatedFolder() {
        val contents = setOf("uberhar-precompiled-data.txt.backup", "sdmc")
        val folder = Folder(contents)
        assertFalse(folder.prepare())
        assertEquals(contents, folder.entries)
        assertTrue(folder.createAttempts.isEmpty())
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-413 has no representable password or password action. */
class SavedSignInsReducerTest {

    private val ready = SavedSignInsUiState(
        availability = YouSurfaceAvailability.READY,
        records = listOf(YouFixtures.signInRecord),
    )

    @Test
    fun `the state and intent vocabularies contain no password material`() {
        val stateFields = SavedSignInsUiState::class.java.declaredFields.map { it.name }
        val intentNames = SavedSignInsIntent::class.java.declaredClasses.map { it.simpleName }
        assertTrue(stateFields.none { it.contains("password", ignoreCase = true) })
        assertTrue(intentNames.none { it.contains("password", ignoreCase = true) })
        assertTrue(intentNames.none { it.contains("reveal", ignoreCase = true) })
        assertEquals("<redacted>", YouFixtures.signInRecord.toString()
            .substringAfter("username=").substringBefore(','))
    }

    @Test
    fun `unavailable drops records rather than guessing`() {
        val projected = projectSavedSignIns(
            snapshot = SavedSignInsRepository.Snapshot(
                availability = YouSurfaceAvailability.UNAVAILABLE,
                records = listOf(YouFixtures.signInRecord),
            ),
            query = "",
            openedId = "s1",
            confirmDelete = false,
        )
        assertTrue(projected.records.isEmpty())
        assertNull(projected.opened)
    }

    @Test
    fun `delete requires confirmation and removes only the opened row`() {
        val opened = reduceSavedSignIns(ready, SavedSignInsIntent.Open("s1"))
        val asking = reduceSavedSignIns(opened, SavedSignInsIntent.Delete)
        assertTrue(asking.confirmDelete)
        val deleted = reduceSavedSignIns(asking, SavedSignInsIntent.ConfirmDelete)
        assertTrue(deleted.records.isEmpty())
        assertNull(deleted.opened)
    }

    @Test
    fun `search matches site or username`() {
        assertEquals(listOf(YouFixtures.signInRecord), ready.copy(query = "CROMA").matching)
        assertTrue(ready.copy(query = "missing").matching.isEmpty())
    }
}

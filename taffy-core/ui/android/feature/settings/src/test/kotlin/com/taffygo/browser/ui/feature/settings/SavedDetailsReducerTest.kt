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

/** Screen SCR-414's editor, with no national-id field. */
class SavedDetailsReducerTest {

    private val ready = projectSavedDetails(
        YouFixtures.detailsEmpty.copy(people = listOf(YouFixtures.person)),
        editor = null,
        confirmDelete = false,
    )

    @Test
    fun `unavailable disables add`() {
        val unavailable = SavedDetailsUiState(availability = YouSurfaceAvailability.UNAVAILABLE)
        assertEquals(unavailable, reduceSavedDetails(unavailable, SavedDetailsIntent.Add))
    }

    @Test
    fun `add opens a blank editor without a national id`() {
        val after = reduceSavedDetails(ready, SavedDetailsIntent.Add)
        val editor = requireNotNull(after.editor)
        assertNull(editor.id)
        assertEquals("", editor.givenName)
        assertEquals("", editor.country)
    }

    @Test
    fun `edit copies the person and delete needs confirm`() {
        val editing = reduceSavedDetails(ready, SavedDetailsIntent.Edit("p1"))
        assertEquals("Priya", editing.editor?.givenName)
        val asking = reduceSavedDetails(editing, SavedDetailsIntent.Delete)
        assertTrue(asking.confirmDelete)
        val kept = reduceSavedDetails(asking, SavedDetailsIntent.CancelDelete)
        assertEquals("p1", kept.editor?.id)
        assertTrue(!kept.confirmDelete)
    }

    @Test
    fun `the editor has no extra identity field`() {
        val fields = SavedDetailsUiState.Editor::class.java.declaredFields.map { it.name }
        assertTrue(fields.none { it.contains("national", ignoreCase = true) })
        assertTrue(fields.none { it.contains("aadhaar", ignoreCase = true) })
        assertTrue(fields.none { it.contains("pan", ignoreCase = true) })
    }
}

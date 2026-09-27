// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-503's projection. Conflict claims appear only when the port named
 * them; a missing item is not filled in with a made-up fact.
 */
class LibraryItemReducerTest {

    @Test
    fun `a known item keeps its sources and body`() {
        val item = libraryItem(
            id = "item_0",
            collectionId = "col_0",
            body = "The listing says the warranty is two years.",
            related = listOf(
                LibraryRepository.RelatedItem("col_0", "item_1", "Return window"),
            ),
        )
        val state = projectLibraryItem(
            snapshot = readyLibrary(libraryCollection(id = "col_0", items = listOf(item))),
            collectionId = "col_0",
            itemId = "item_0",
            canMutate = false,
        )

        assertEquals("item_0", state.itemId)
        assertEquals("The listing says the warranty is two years.", state.body)
        assertEquals(listOf("docs.example.test"), state.sources.map { it.host })
        assertEquals("item_1", state.related.single().itemId)
        assertFalse(state.missing)
        assertFalse(state.hasConflict)
        assertNull(state.conflictSummary)
        assertFalse(state.canMutate)
    }

    @Test
    fun `an unknown item is missing, not a made-up fact`() {
        val state = projectLibraryItem(
            snapshot = readyLibrary(libraryCollection("col_0")),
            collectionId = "col_0",
            itemId = "gone",
            canMutate = false,
        )

        assertTrue(state.missing)
        assertTrue(state.body.isEmpty())
        assertTrue(state.sources.isEmpty())
    }

    @Test
    fun `a conflict without a summary still says the sources disagree`() {
        val item = libraryItem(
            id = "item_0",
            hasConflict = true,
            conflictSummary = null,
        )
        val state = projectLibraryItem(
            snapshot = readyLibrary(libraryCollection(id = "col_0", items = listOf(item))),
            collectionId = "col_0",
            itemId = "item_0",
            canMutate = false,
        )

        assertTrue(state.hasConflict)
        assertNull(state.conflictSummary)
    }

    @Test
    fun `a port-supplied conflict summary is kept`() {
        val item = libraryItem(
            id = "item_0",
            hasConflict = true,
            conflictSummary = "Two years on one page, one year on the other.",
        )
        val state = projectLibraryItem(
            snapshot = readyLibrary(libraryCollection(id = "col_0", items = listOf(item))),
            collectionId = "col_0",
            itemId = "item_0",
            canMutate = false,
        )

        assertEquals("Two years on one page, one year on the other.", state.conflictSummary)
    }

    @Test
    fun `unavailable is not dressed up as missing`() {
        val state = projectLibraryItem(
            snapshot = LibraryRepository.Snapshot.Unavailable,
            collectionId = "col_0",
            itemId = "item_0",
            canMutate = false,
        )

        assertTrue(state.unavailable)
        assertFalse(state.missing)
        assertTrue(state.body.isEmpty())
    }
}

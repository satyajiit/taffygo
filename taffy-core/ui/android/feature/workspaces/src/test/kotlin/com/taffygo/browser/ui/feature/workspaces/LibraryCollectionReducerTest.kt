// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-502's projection. A missing collection is not an empty one, and
 * Empty/Unavailable never look as if refresh or remove ran.
 */
class LibraryCollectionReducerTest {

    @Test
    fun `a known collection lists its items`() {
        val item = libraryItem(id = "item_0", collectionId = "col_0")
        val state = projectLibraryCollection(
            snapshot = readyLibrary(libraryCollection(id = "col_0", items = listOf(item))),
            collectionId = "col_0",
            query = "",
            canMutate = false,
        )

        assertEquals("col_0", state.collectionId)
        assertEquals(listOf("item_0"), state.items.map { it.id })
        assertEquals(1, state.totalCount)
        assertFalse(state.missing)
        assertFalse(state.canMutate)
        assertFalse(state.isEmpty)
    }

    @Test
    fun `the query matches an item title`() {
        val state = projectLibraryCollection(
            snapshot = readyLibrarySearch(
                "train",
                setOf("item_1"),
                libraryCollection(
                    id = "col_0",
                    items = listOf(
                        libraryItem(id = "item_0", title = "Warranty is two years"),
                        libraryItem(
                            id = "item_1",
                            title = "Train times",
                            body = "The 09:12 leaves from platform 4.",
                        ),
                    ),
                ),
            ),
            collectionId = "col_0",
            query = "train",
            canMutate = false,
        )

        assertEquals(listOf("item_1"), state.items.map { it.id })
        assertEquals(2, state.totalCount)
        assertFalse(state.hasNoMatches)
        assertFalse(state.isEmpty)
    }

    @Test
    fun `an unknown collection is missing, not empty`() {
        val state = projectLibraryCollection(
            snapshot = readyLibrary(libraryCollection("col_0")),
            collectionId = "gone",
            query = "",
            canMutate = false,
        )

        assertTrue(state.missing)
        assertFalse(state.isEmpty)
        assertTrue(state.items.isEmpty())
    }

    @Test
    fun `a collection with no items is empty, not missing`() {
        val state = projectLibraryCollection(
            snapshot = readyLibrary(libraryCollection(id = "col_0", items = emptyList())),
            collectionId = "col_0",
            query = "",
            canMutate = false,
        )

        assertTrue(state.isEmpty)
        assertFalse(state.missing)
        assertEquals("Home project", state.name)
    }

    @Test
    fun `unavailable is not dressed up as missing`() {
        val state = projectLibraryCollection(
            snapshot = LibraryRepository.Snapshot.Unavailable,
            collectionId = "col_0",
            query = "",
            canMutate = false,
        )

        assertTrue(state.unavailable)
        assertFalse(state.missing)
        assertFalse(state.canMutate)
    }

    @Test
    fun `canMutate stays false when the port cannot change anything`() {
        val state = projectLibraryCollection(
            snapshot = readyLibrary(libraryCollection("col_0")),
            collectionId = "col_0",
            query = "",
            canMutate = false,
        )

        assertFalse(state.canMutate)
    }

    @Test
    fun `management requires both a live port and a backing workspace`() {
        val orphan = projectLibraryCollection(
            snapshot = readyLibrary(libraryCollection("orphan")),
            collectionId = "orphan",
            query = "",
            canMutate = true,
            canManage = true,
        )
        val backed = projectLibraryCollection(
            snapshot = readyLibrary(libraryCollection("backed", canManage = true)),
            collectionId = "backed",
            query = "",
            canMutate = true,
            canManage = true,
        )

        assertFalse(orphan.canManage)
        assertTrue(backed.canManage)
    }

    @Test
    fun `refresh preview and result stay with their exact collection`() {
        val preview = LibraryRepository.RefreshPreview(
            previewId = "a".repeat(64),
            collectionId = "backed",
            libraryRevision = 7uL,
            workspaceRevision = 4uL,
            navigationCount = 1u,
            observationCount = 1u,
            sources = listOf(LibraryRepository.RefreshSource("source-1", "Page", "example.test")),
        )
        val result = LibraryRepository.RefreshResult(
            previewId = preview.previewId,
            collectionId = preview.collectionId,
            items = listOf(
                LibraryRepository.RefreshResultItem(
                    "source-1",
                    LibraryRepository.RefreshDisposition.CHANGED,
                ),
            ),
        )
        val state = projectLibraryCollection(
            snapshot = readyLibrary(
                libraryCollection("backed").copy(
                    refreshPreview = preview,
                    refreshResult = result,
                ),
            ),
            collectionId = "backed",
            query = "",
            canMutate = true,
        )

        assertEquals(preview, state.refreshPreview)
        assertEquals(result, state.refreshResult)
        assertEquals(1, state.refreshResult?.count(LibraryRepository.RefreshDisposition.CHANGED))
    }
}

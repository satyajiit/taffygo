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
 * Screen SCR-501's projection. Nothing at all is a different state from
 * nothing matching, and a conflict count of zero does not mint a badge.
 */
class LibraryHomeReducerTest {

    @Test
    fun `an empty query keeps every collection`() {
        val state = projectLibraryHome(
            readyLibrary(libraryCollection("col_0"), libraryCollection("col_1", name = "Travel")),
            "  ",
        )

        assertEquals(2, state.collections.size)
        assertEquals(2, state.totalCount)
        assertFalse(state.isEmpty)
        assertFalse(state.hasNoMatches)
        assertFalse(state.loading)
        assertFalse(state.unavailable)
    }

    @Test
    fun `the query matches a collection name`() {
        val state = projectLibraryHome(
            readyLibrarySearch(
                "travel",
                setOf("item_travel"),
                libraryCollection("col_0", name = "Home project"),
                libraryCollection(
                    "col_1",
                    name = "Travel",
                    items = listOf(libraryItem("item_travel", "col_1")),
                ),
            ),
            "travel",
        )

        assertEquals(listOf("col_1"), state.collections.map { it.id })
    }

    @Test
    fun `the query matches a kept item title`() {
        val state = projectLibraryHome(
            readyLibrarySearch(
                "WARRANTY",
                setOf("item_0"),
                libraryCollection(
                    "col_0",
                    items = listOf(libraryItem(title = "Warranty is two years")),
                ),
                libraryCollection(
                    "col_1",
                    name = "Travel",
                    items = listOf(
                        libraryItem(
                            id = "item_1",
                            title = "Train times",
                            body = "The 09:12 leaves from platform 4.",
                        ),
                    ),
                ),
            ),
            "WARRANTY",
        )

        assertEquals(listOf("col_0"), state.collections.map { it.id })
    }

    @Test
    fun `nothing at all is not the same state as nothing matching`() {
        val nothingAtAll = projectLibraryHome(readyLibrary(), "")
        val nothingMatching = projectLibraryHome(
            readyLibrary(libraryCollection("col_0")),
            "no such thing",
        )

        assertTrue(nothingAtAll.isEmpty)
        assertFalse(nothingAtAll.hasNoMatches)
        assertFalse(nothingMatching.isEmpty)
        assertTrue(nothingMatching.hasNoMatches)
    }

    @Test
    fun `unavailable is not dressed up as empty`() {
        val state = projectLibraryHome(LibraryRepository.Snapshot.Unavailable, "")

        assertTrue(state.unavailable)
        assertFalse(state.isEmpty)
        assertFalse(state.loading)
        assertTrue(state.collections.isEmpty())
    }

    @Test
    fun `loading is not dressed up as empty`() {
        val state = projectLibraryHome(LibraryRepository.Snapshot.Loading, "query")

        assertTrue(state.loading)
        assertFalse(state.isEmpty)
        assertEquals("query", state.query)
    }

    @Test
    fun `a zero conflict count stays zero`() {
        val state = projectLibraryHome(readyLibrary(libraryCollection(conflictCount = 0)), "")

        assertEquals(0, state.collections.single().conflictCount)
    }

    @Test
    fun `a port-supplied conflict count is kept`() {
        val state = projectLibraryHome(readyLibrary(libraryCollection(conflictCount = 2)), "")

        assertEquals(2, state.collections.single().conflictCount)
    }

    @Test
    fun `the empty port is ready with nothing kept`() {
        val port = EmptyLibraryRepository()
        val state = projectLibraryHome(port.snapshot.value, "")

        assertTrue(state.isEmpty)
        assertFalse(port.canKeep)
        assertFalse(port.canMutate)
    }

    @Test
    fun `the unavailable port does not invent collections`() {
        val port = UnavailableLibraryRepository()
        val state = projectLibraryHome(port.snapshot.value, "")

        assertTrue(state.unavailable)
        assertTrue(state.collections.isEmpty())
        assertFalse(port.canKeep)
    }

    @Test
    fun `related Library rows are bounded and keep entry order`() {
        val sources = List(1_024) { listOf("shared") }

        val related = relatedLibraryEntryIndices(sources, limit = 8)

        assertEquals((1..8).toList(), related.first())
        assertEquals((0..7).toList(), related.last())
        assertTrue(related.all { it.size == 8 })
    }

    @Test
    fun `overlapping and repeated source ids do not duplicate a related row`() {
        val related = relatedLibraryEntryIndices(
            listOf(
                listOf("a", "a", "b"),
                listOf("b"),
                listOf("a"),
                listOf("none"),
            ),
            limit = 8,
        )

        assertEquals(listOf(1, 2), related[0])
        assertEquals(listOf(0), related[1])
        assertEquals(listOf(0), related[2])
        assertTrue(related[3].isEmpty())
    }
}

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
 * Screen SCR-504's projection. Keep never flips a success flag, and a
 * collection that left the snapshot cannot stay selected.
 */
class KeepThisReducerTest {

    @Test
    fun `empty ready lists no collections and cannot keep`() {
        val state = projectKeepThis(
            snapshot = readyLibrary(),
            selectedCollectionId = null,
            selectedKind = null,
            canKeep = false,
        )

        assertTrue(state.collections.isEmpty())
        assertFalse(state.canKeep)
        assertFalse(state.keepEnabled)
        assertFalse(state.loading)
        assertEquals(KeepThisKind.entries, state.kinds)
    }

    @Test
    fun `keep stays off without a collection and a kind`() {
        val ready = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = "col_0",
            selectedKind = null,
            canKeep = true,
        )
        val kindOnly = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = null,
            selectedKind = KeepThisKind.FACT,
            canKeep = true,
        )

        assertFalse(ready.keepEnabled)
        assertFalse(kindOnly.keepEnabled)
    }

    @Test
    fun `keep is enabled only when the port can keep and both choices exist`() {
        val state = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = "col_0",
            selectedKind = KeepThisKind.FACT,
            canKeep = true,
        )

        assertTrue(state.keepEnabled)
    }

    @Test
    fun `a vanished collection cannot stay selected`() {
        val state = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = "gone",
            selectedKind = KeepThisKind.FILE,
            canKeep = true,
        )

        assertNull(state.selectedCollectionId)
        assertEquals(KeepThisKind.FILE, state.selectedKind)
        assertFalse(state.keepEnabled)
    }

    @Test
    fun `selecting a collection records it`() {
        val start = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = null,
            selectedKind = null,
            canKeep = false,
        )

        val next = reduceKeepThis(start, KeepThisIntent.SelectCollection("col_0"))

        assertEquals("col_0", next.selectedCollectionId)
    }

    @Test
    fun `selecting a collection that is not listed is ignored`() {
        val start = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = null,
            selectedKind = null,
            canKeep = false,
        )

        val next = reduceKeepThis(start, KeepThisIntent.SelectCollection("gone"))

        assertNull(next.selectedCollectionId)
    }

    @Test
    fun `selecting a kind records it`() {
        val start = projectKeepThis(
            snapshot = readyLibrary(),
            selectedCollectionId = null,
            selectedKind = null,
            canKeep = false,
        )

        val next = reduceKeepThis(start, KeepThisIntent.SelectKind(KeepThisKind.WORKSPACE))

        assertEquals(KeepThisKind.WORKSPACE, next.selectedKind)
    }

    @Test
    fun `keep does not pretend something was added`() {
        val start = projectKeepThis(
            snapshot = readyLibrary(libraryCollection("col_0")),
            selectedCollectionId = "col_0",
            selectedKind = KeepThisKind.FACT,
            canKeep = false,
        )

        val next = reduceKeepThis(start, KeepThisIntent.Keep)

        assertEquals(start, next)
        assertFalse(next.keepEnabled)
    }

    @Test
    fun `unavailable lists no collections`() {
        val state = projectKeepThis(
            snapshot = LibraryRepository.Snapshot.Unavailable,
            selectedCollectionId = "col_0",
            selectedKind = KeepThisKind.FACT,
            canKeep = false,
        )

        assertTrue(state.unavailable)
        assertTrue(state.collections.isEmpty())
        assertNull(state.selectedCollectionId)
        assertFalse(state.keepEnabled)
    }
}

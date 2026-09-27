// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-304's projection. Two sentences are pinned here: nothing at all is
 * a different state from nothing matching, and a partly done workspace stays
 * partly done in the list.
 */
class WorkspaceListReducerTest {

    @Test
    fun `an empty query keeps every workspace`() {
        val state = projectWorkspaceList(listOf(workspace("ws_0"), workspace("ws_1")), "  ")

        assertEquals(2, state.workspaces.size)
        assertEquals(2, state.totalCount)
        assertFalse(state.isEmpty)
        assertFalse(state.hasNoMatches)
    }

    @Test
    fun `the query matches the goal`() {
        val state = projectWorkspaceList(
            listOf(
                workspace("ws_0", goal = "Compare retention policies"),
                workspace("ws_1", goal = "Find a replacement kettle"),
            ),
            "kettle",
        )

        assertEquals(listOf("ws_1"), state.workspaces.map { it.id.value })
    }

    @Test
    fun `the query matches a source host`() {
        val state = projectWorkspaceList(
            listOf(
                workspace("ws_0", sources = listOf(source("src_0", "docs.example.test"))),
                workspace("ws_1", sources = listOf(source("src_1", "shop.example.test"))),
            ),
            "SHOP.",
        )

        assertEquals(listOf("ws_1"), state.workspaces.map { it.id.value })
    }

    @Test
    fun `the order is most recently updated first`() {
        val state = projectWorkspaceList(
            listOf(
                workspace("ws_old", lastUpdated = 10),
                workspace("ws_new", lastUpdated = 300),
                workspace("ws_middle", lastUpdated = 200),
            ),
            "",
        )

        assertEquals(listOf("ws_new", "ws_middle", "ws_old"), state.workspaces.map { it.id.value })
    }

    @Test
    fun `nothing at all is not the same state as nothing matching`() {
        val nothingAtAll = projectWorkspaceList(emptyList(), "")
        val nothingMatching = projectWorkspaceList(listOf(workspace("ws_0")), "no such thing")

        assertTrue(nothingAtAll.isEmpty)
        assertFalse(nothingAtAll.hasNoMatches)
        assertFalse(nothingMatching.isEmpty)
        assertTrue(nothingMatching.hasNoMatches)
    }

    @Test
    fun `a partly done workspace is listed as partly done`() {
        val state = projectWorkspaceList(
            listOf(workspace("ws_0", state = TaskDisplayState.PARTLY_DONE)),
            "",
        )

        assertEquals(TaskDisplayState.PARTLY_DONE, state.workspaces.single().state)
    }

    @Test
    fun `an unpublished list is loading, not empty`() {
        val state = projectWorkspaceList(emptyList(), "", loading = true)

        assertTrue(state.loading)
        assertFalse(state.isEmpty)
        assertFalse(state.hasNoMatches)
    }

    @Test
    fun `the profile is named only when there is more than one to tell apart`() {
        val personal = BrowserProfilesRepository.Profile("p", "Personal", active = true)
        val work = BrowserProfilesRepository.Profile("w", "Work", active = false)
        val ready = BrowserProfilesRepository.Availability.READY

        assertNull(workspaceProfileName(BrowserProfilesRepository.Snapshot(ready, listOf(personal))))
        assertEquals(
            "Personal",
            workspaceProfileName(BrowserProfilesRepository.Snapshot(ready, listOf(personal, work))),
        )
        assertNull(
            workspaceProfileName(
                BrowserProfilesRepository.Snapshot(
                    BrowserProfilesRepository.Availability.UNAVAILABLE,
                    listOf(personal, work),
                ),
            ),
        )
    }

    @Test
    fun `the profile line rides through the projection`() {
        val state = projectWorkspaceList(listOf(workspace("ws_0")), "", profileName = "Work")

        assertEquals("Work", state.profileName)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-305's projection: grouped output, honest state, counted conflicts. */
class WorkspaceDetailReducerTest {

    @Test
    fun `an identifier that resolves to nothing is missing, not empty`() {
        val state = projectWorkspaceDetail(null)

        assertTrue(state.missing)
        assertFalse(state.loading)
        assertTrue(state.facts.isEmpty())
        assertEquals(null, state.id)
    }

    @Test
    fun `an unpublished workspace is loading, not missing`() {
        val state = projectWorkspaceDetail(null, loading = true)

        assertTrue(state.loading)
        assertFalse(state.missing)
    }

    @Test
    fun `facts are grouped by field and stable within a field`() {
        val state = projectWorkspaceDetail(
            workspace(
                facts = listOf(
                    fact("f_2", field = "price", value = "20", sources = listOf("src_0")),
                    fact("f_1", field = "price", value = "18", sources = listOf("src_1")),
                    fact("f_0", field = "name", value = "Kettle", sources = listOf("src_0")),
                ),
            ),
        )

        assertEquals(listOf("f_0", "f_1", "f_2"), state.facts.map { it.id.value })
        assertEquals(listOf("name", "price", "price"), state.facts.map { it.field })
    }

    @Test
    fun `sources are ordered by host`() {
        val state = projectWorkspaceDetail(
            workspace(
                sources = listOf(
                    source("src_1", "shop.example.test"),
                    source("src_0", "docs.example.test"),
                ),
            ),
        )

        assertEquals(
            listOf("docs.example.test", "shop.example.test"),
            state.sources.map { it.host },
        )
    }

    @Test
    fun `conflicts and cells that lost their source are counted apart`() {
        val state = projectWorkspaceDetail(
            workspace(
                facts = listOf(
                    fact("f_0", "price", "18", listOf("src_0"), hasConflict = true),
                    fact("f_1", "stock", "none", listOf("src_1"), needsANewSource = true),
                    fact("f_2", "name", "Kettle", listOf("src_0")),
                ),
            ),
        )

        assertEquals(1, state.conflictCount)
        assertEquals(1, state.needsANewSourceCount)
    }

    @Test
    fun `a stopped workspace is not shown as done`() {
        val state = projectWorkspaceDetail(workspace(state = TaskDisplayState.STOPPED))

        assertEquals(TaskDisplayState.STOPPED, state.state)
        assertFalse(state.missing)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.SourceId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-306's projection: when and how a page was read, or nothing at all. */
class SourceViewerReducerTest {

    @Test
    fun `a source that is not in this workspace is missing`() {
        val state = projectSourceViewer(workspace(), SourceId("src_absent"))

        assertTrue(state.missing)
        assertEquals("", state.host)
    }

    @Test
    fun `a workspace that did not resolve is missing too`() {
        assertTrue(projectSourceViewer(null, SourceId("src_0")).missing)
    }

    @Test
    fun `an unpublished source is loading, not missing`() {
        val state = projectSourceViewer(null, SourceId("src_0"), loading = true)

        assertTrue(state.loading)
        assertFalse(state.missing)
    }

    @Test
    fun `the capture time and the fact count are carried through`() {
        val state = projectSourceViewer(
            workspace(sources = listOf(source("src_0", "docs.example.test", readAt = 4_200, factCount = 3))),
            SourceId("src_0"),
        )

        assertEquals("docs.example.test", state.host)
        assertEquals(4_200L, state.readAtEpochMillis)
        assertEquals(3, state.factCount)
        assertFalse(state.excluded)
        assertFalse(state.missing)
    }

    @Test
    fun `an excluded source is still shown, and still says it is excluded`() {
        val state = projectSourceViewer(
            workspace(sources = listOf(source("src_0", "gone.example.test", excluded = true))),
            SourceId("src_0"),
        )

        assertTrue(state.excluded)
        assertFalse(state.missing)
    }
}

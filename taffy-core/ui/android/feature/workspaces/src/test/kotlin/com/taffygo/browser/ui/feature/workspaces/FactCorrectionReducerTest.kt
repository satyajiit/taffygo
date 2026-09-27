// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.FactId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-307's reducer. The sheet's whole point is pinned here: what the
 * page said is kept, and what the user typed is recorded beside it.
 */
class FactCorrectionReducerTest {

    @Test
    fun `a fact that is not in this workspace is missing`() {
        val state = projectFactCorrection(workspace(), FactId("f_absent"))

        assertTrue(state.missing)
        assertFalse(state.loading)
        assertFalse(state.canSave)
    }

    @Test
    fun `an unpublished fact is loading, not missing`() {
        val state = projectFactCorrection(null, FactId("f_0"), loading = true)

        assertTrue(state.loading)
        assertFalse(state.missing)
        assertFalse(state.canSave)
    }

    @Test
    fun `the page value is shown beside the entered value, never replaced by it`() {
        val state = projectFactCorrection(
            workspace(
                facts = listOf(
                    fact("f_0", "price", "18.00", listOf("src_0"), correction = "17.50"),
                ),
            ),
            FactId("f_0"),
        )

        assertEquals("18.00", state.pageValue)
        assertEquals("17.50", state.enteredValue)
        assertTrue(state.canSave)
    }

    @Test
    fun `the sheet counts the other cells that share a source with this one`() {
        val state = projectFactCorrection(
            workspace(
                facts = listOf(
                    fact("f_0", "price", "18.00", listOf("src_0", "src_1")),
                    fact("f_1", "stock", "in stock", listOf("src_1")),
                    fact("f_2", "name", "Kettle", listOf("src_0")),
                    fact("f_3", "rating", "4.5", listOf("src_2")),
                ),
            ),
            FactId("f_0"),
        )

        assertEquals(2, state.downstreamCount)
    }

    @Test
    fun `typing changes the draft and nothing else`() {
        val before = FactCorrectionUiState(field = "price", pageValue = "18.00", downstreamCount = 2)

        val after = reduceFactCorrection(before, FactCorrectionIntent.ValueChanged("17.50"))

        assertEquals("17.50", after.enteredValue)
        assertEquals(before.pageValue, after.pageValue)
        assertEquals(before.downstreamCount, after.downstreamCount)
    }

    @Test
    fun `there is nothing to save until the value differs and is not blank`() {
        val base = FactCorrectionUiState(field = "price", pageValue = "18.00")

        assertFalse(base.canSave)
        assertFalse(base.copy(enteredValue = "   ").canSave)
        assertFalse(base.copy(enteredValue = "18.00").canSave)
        assertTrue(base.copy(enteredValue = "17.50").canSave)
    }

    @Test
    fun `saving and cancelling leave the state to the layer that owns it`() {
        val state = FactCorrectionUiState(pageValue = "18.00", enteredValue = "17.50")

        assertEquals(state, reduceFactCorrection(state, FactCorrectionIntent.Save))
        assertEquals(state, reduceFactCorrection(state, FactCorrectionIntent.Cancel))
    }
}

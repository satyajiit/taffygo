// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * One box, four content readings and one safe browser command.
 *
 * Only one of them can start work across pages, and that one always opens a
 * preview first. This test pins which, so another interpretation cannot be
 * added without deciding whether it needs consent.
 */
class AddressBarInterpretationTest {

    @Test
    fun `going somewhere and searching run immediately`() {
        assertFalse(
            AddressBarInterpretation.GoTo("docs.example.test", "docs.example.test").needsPreview,
        )
        assertFalse(AddressBarInterpretation.Search("retention policy").needsPreview)
    }

    @Test
    fun `a browser command opens its screen without starting work`() {
        val reading = AddressBarInterpretation.BrowserCommand(
            "clear browsing data",
            AddressBarCommand.OPEN_CLEAR_BROWSING_DATA,
        )

        assertFalse(reading.needsPreview)
    }

    @Test
    fun `asking about the page changes nothing and needs no preview`() {
        assertFalse(AddressBarInterpretation.AskTaffy("what does this say?").needsPreview)
    }

    @Test
    fun `work across pages always opens a preview first`() {
        val reading = AddressBarInterpretation.TaskForTaffy(
            "compare these two policies",
            TaskTemplate.COMPARE_PRODUCTS,
        )

        assertTrue(reading.needsPreview)
    }

    @Test
    fun `every reading keeps the text it was resolved from`() {
        val typed = "compare these two policies"
        val readings = listOf(
            AddressBarInterpretation.GoTo(typed, "docs.example.test"),
            AddressBarInterpretation.Search(typed),
            AddressBarInterpretation.AskTaffy(typed),
            AddressBarInterpretation.TaskForTaffy(typed, TaskTemplate.SUMMARIZE_EVIDENCE),
            AddressBarInterpretation.BrowserCommand(typed, AddressBarCommand.OPEN_HISTORY),
        )

        assertTrue(readings.all { it.input == typed })
        assertEquals(1, readings.count { it.needsPreview })
    }
}

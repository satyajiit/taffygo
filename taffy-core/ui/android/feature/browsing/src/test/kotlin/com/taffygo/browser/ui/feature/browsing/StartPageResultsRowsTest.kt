// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.TaskTemplate
import org.junit.Assert.assertEquals
import org.junit.Test

/** Which suggestion rows the start page draws under the reading. */
class StartPageResultsRowsTest {

    private val words = "download my aadhaar"
    private val errand = AddressBarInterpretation.TaskForTaffy(words, TaskTemplate.WEB_ERRAND)
    private val search = AddressBarInterpretation.Search(words)

    @Test
    fun `a suggestion that says what the reading says is not drawn`() {
        val state = AddressBarUiState(
            input = words,
            interpretation = errand,
            suggestions = listOf(
                Suggestion("suggestion_0", words, errand),
                Suggestion("suggestion_1", words, search),
            ),
        )

        assertEquals(listOf("suggestion_1"), state.visibleSuggestions().map(Suggestion::id))
    }

    @Test
    fun `a stated shape makes the resolver's own reading a suggestion again`() {
        val state = AddressBarUiState(
            input = words,
            interpretation = search,
            shape = TaskTemplate.COMPARE_PRODUCTS,
            suggestions = listOf(
                Suggestion("suggestion_0", words, search),
                Suggestion("suggestion_1", words, errand),
            ),
        )

        // The reading is now the shaped task, so the plain search the words
        // would have meant is a real alternative and keeps its row.
        assertEquals(
            listOf("suggestion_0", "suggestion_1"),
            state.visibleSuggestions().map(Suggestion::id),
        )
    }

    @Test
    fun `each reading row says where its words go`() {
        val labels = listOf(
            AddressBarInterpretation.GoTo("example.com", "example.com"),
            search,
            AddressBarInterpretation.AskTaffy(words),
            errand,
        ).map { suggestionSourceLabel(Suggestion("s", words, it)) }

        assertEquals(
            listOf(
                R.string.taffy_address_bar_source_go_to,
                R.string.taffy_address_bar_source_search,
                R.string.taffy_address_bar_source_ask,
                R.string.taffy_address_bar_source_task,
            ),
            labels,
        )
        // The Search and Ask Taffy rows once shared one label; four readings,
        // four labels.
        assertEquals(labels.size, labels.toSet().size)
    }

    @Test
    fun `a page row still names the list it came from`() {
        val page = AddressBarInterpretation.GoTo("example.com", "example.com")

        assertEquals(
            R.string.taffy_address_bar_source_history,
            suggestionSourceLabel(Suggestion("p", "Example", page, Suggestion.Source.HISTORY)),
        )
    }
}

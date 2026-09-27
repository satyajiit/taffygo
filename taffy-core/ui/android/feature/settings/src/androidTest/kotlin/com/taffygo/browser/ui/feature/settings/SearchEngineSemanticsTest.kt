// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.browser.SearchEngineId
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test

class SearchEngineSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<SearchEngineIntent>()

    @Before
    fun clearIntents() {
        intents.clear()
    }

    @Test
    fun listShowsVendorRowsAndSelectingBingRecordsTheIntent() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "US")
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SearchEngineContent(state = state, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(SEARCH_ENGINE_LIST_TEST_TAG).assertExists()
        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}google").assertExists()
        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}duckduckgo").assertExists()
        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}brave-search").assertExists()
        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}bing").performClick()
        assertEquals(listOf(SearchEngineIntent.Select(SearchEngineId.BING)), intents)
    }

    @Test
    fun germanyListShowsRegionalDuckDuckGoAndSelectingItRecordsTheIntent() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "DE")
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SearchEngineContent(state = state, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}duckduckgo-de").assertExists()
        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}duckduckgo").assertDoesNotExist()
        compose.onNodeWithTag("${SEARCH_ENGINE_ROW_TEST_TAG_PREFIX}duckduckgo-de").performClick()
        assertEquals(
            listOf(SearchEngineIntent.Select(SearchEngineId.DUCKDUCKGO_DE)),
            intents,
        )
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.SearchEngineId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SearchEngineReducerTest {

    @Test
    fun `the default list is google first and includes brave search`() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "US")
        assertEquals(SearchEngineId.GOOGLE, state.engines.first().id)
        assertTrue(state.engines.first().selected)
        assertTrue(state.engines.any { it.id == SearchEngineId.BRAVE_SEARCH })
        assertTrue(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO })
        assertFalse(state.engines.any { it.id == SearchEngineId.YAHOO_JP })
        assertFalse(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO_DE })
        assertFalse(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO_AU_NZ_IE })
    }

    @Test
    fun `germany offers regional duckduckgo instead of the generic row`() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "DE")
        assertTrue(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO_DE })
        assertFalse(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO })
        assertEquals(
            "duckduckgo.ico",
            state.engines.first { it.id == SearchEngineId.DUCKDUCKGO_DE }.markFile,
        )
    }

    @Test
    fun `australia offers the au-nz-ie duckduckgo row`() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "AU")
        assertTrue(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO_AU_NZ_IE })
        assertFalse(state.engines.any { it.id == SearchEngineId.DUCKDUCKGO })
    }

    @Test
    fun `japan adds yahoo japan`() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "JP")
        assertTrue(state.engines.any { it.id == SearchEngineId.YAHOO_JP })
        assertTrue(state.engines.any { it.id == SearchEngineId.YAHOO })
    }

    @Test
    fun `selecting bing marks only bing`() {
        val before = projectSearchEngine(SearchEngineId.GOOGLE, "US")
        val after = reduceSearchEngine(before, SearchEngineIntent.Select(SearchEngineId.BING))
        assertEquals(SearchEngineId.BING, after.selectedId)
        assertTrue(after.engines.single { it.id == SearchEngineId.BING }.selected)
        assertFalse(after.engines.single { it.id == SearchEngineId.GOOGLE }.selected)
    }

    @Test
    fun `selecting regional duckduckgo marks only that row`() {
        val before = projectSearchEngine(SearchEngineId.GOOGLE, "DE")
        val after = reduceSearchEngine(
            before,
            SearchEngineIntent.Select(SearchEngineId.DUCKDUCKGO_DE),
        )
        assertEquals(SearchEngineId.DUCKDUCKGO_DE, after.selectedId)
        assertTrue(after.engines.single { it.id == SearchEngineId.DUCKDUCKGO_DE }.selected)
        assertFalse(after.engines.single { it.id == SearchEngineId.GOOGLE }.selected)
    }

    @Test
    fun `dismiss leaves the selection`() {
        val state = projectSearchEngine(SearchEngineId.ECOSIA, "IN")
        assertEquals(state, reduceSearchEngine(state, SearchEngineIntent.Dismiss))
    }

    @Test
    fun `each row draws the vendor mark file`() {
        val state = projectSearchEngine(SearchEngineId.GOOGLE, "US")
        assertEquals("google.ico", state.engines.first { it.id == SearchEngineId.GOOGLE }.markFile)
        assertEquals(
            "brave-search.ico",
            state.engines.first { it.id == SearchEngineId.BRAVE_SEARCH }.markFile,
        )
        assertEquals(
            "duckduckgo.ico",
            state.engines.first { it.id == SearchEngineId.DUCKDUCKGO }.markFile,
        )
    }
}

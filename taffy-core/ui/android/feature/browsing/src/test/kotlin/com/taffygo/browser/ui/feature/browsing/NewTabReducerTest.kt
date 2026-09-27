// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The local, bounded start-page projection behind SCR-102. */
class NewTabReducerTest {
    @Test
    fun `the start page shows the person's sites in the store's own order`() {
        val state = projectNewTab(
            tabs = emptyList(),
            sites = listOf(
                site("docs.example.test", "Retention policy", visitCount = 9),
                site("shop.example.test", "Product listing", visitCount = 4),
            ),
        )

        assertEquals(
            listOf("docs.example.test", "shop.example.test"),
            state.frequent.map { it.host },
        )
        assertEquals("Retention policy", state.frequent.first().title)
        assertFalse(state.isEmpty)
    }

    @Test
    fun `a tile with no title is spoken by its host rather than by nothing`() {
        val state = projectNewTab(
            tabs = emptyList(),
            sites = listOf(site("docs.example.test", title = "", visitCount = 1)),
        )

        assertEquals("docs.example.test", state.frequent.single().title)
    }

    @Test
    fun `the grid draws one row and the ranking past it stays in the store`() {
        val ranking = (1..12).map { index ->
            site("site$index.example.test", "Site", 100L - index)
        }

        val state = projectNewTab(tabs = emptyList(), sites = ranking)

        assertEquals(MAX_START_PAGE_TILES, state.frequent.size)
        assertEquals("site1.example.test", state.frequent.first().host)
    }

    @Test
    fun `a profile that has been nowhere has an empty grid, not an invented one`() {
        val state = projectNewTab(
            tabs = listOf(Tab(TabId("tab_1"), "Product listing", "shop.example.test")),
            sites = emptyList(),
        )

        assertTrue(state.isEmpty)
    }

    @Test
    fun `the tab badge counts every open tab including ones the grid omits`() {
        val state = projectNewTab(
            listOf(
                Tab(TabId("tab_1"), "Product listing", "shop.example.test"),
                Tab(TabId("tab_2"), "", "", isPrivate = true),
                Tab(TabId("tab_3"), "", ""),
                Tab(
                    TabId("tab_4"),
                    "Independent review",
                    "reviews.example.test",
                    isTaffyTab = true,
                ),
            ),
        )

        assertTrue(state.frequent.isEmpty())
        assertEquals(3, state.userTabCount)
        assertEquals(1, state.taffyTabCount)
    }

    private fun site(host: String, title: String, visitCount: Long) = FrequentSite(
        host = host,
        title = title,
        visitCount = visitCount,
        lastVisitEpochMillis = 1,
    )
}

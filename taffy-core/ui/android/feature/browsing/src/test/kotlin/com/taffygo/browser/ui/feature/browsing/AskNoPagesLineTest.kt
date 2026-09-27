// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Why an ask in place has no page on it (decision 0168).
 *
 * The overlay had one sentence for this and it was about private tabs, so a
 * person who opened Ask on one of Taffy's own tabs was told "This kind of task
 * reads pages you name. Tap to choose them." — an instruction whose only
 * answer is a sheet that has already decided this tab is not on offer.
 */
class AskNoPagesLineTest {
    @Test
    fun `an ask opened on one of Taffy's own tabs says whose tab it is`() {
        val state = askingInPlace(taffyTab(selected = true))
        assertTrue(state.pages.currentTabIsTaffys)
        assertEquals(R.string.taffy_attach_pages_taffy_tabs, noPagesLine(state))
    }

    @Test
    fun `a Taffy tab open somewhere else is not why this ask has no page`() {
        // The person is reading their own page; the ask has a page and nothing
        // is owed. `taffyTabsPresent` is true and must not be the trigger.
        val state = askingInPlace(
            userTab(selected = true),
            taffyTab(id = "tab_taffy_2"),
        )
        assertTrue(state.pages.taffyTabsPresent)
        assertFalse(state.pages.currentTabIsTaffys)
        assertNull(noPagesLine(state))
    }

    @Test
    fun `a private tab keeps its own sentence`() {
        val state = askingInPlace(privateTab(selected = true))
        assertEquals(R.string.taffy_ask_will_do_none_private, noPagesLine(state))
    }

    @Test
    fun `a tab that is both Taffy's and private is Taffy's`() {
        // `privateTabsWithheld` leaves Taffy's tabs out on purpose: a tab Taffy
        // opened was never the person's to offer, so privacy is not what
        // withheld it, and the private sentence would send them to open the
        // page in one of their tabs for no reason.
        val state = askingInPlace(taffyTab(selected = true, isPrivate = true))
        assertFalse(state.pages.privateTabsWithheld)
        assertEquals(R.string.taffy_attach_pages_taffy_tabs, noPagesLine(state))
    }

    @Test
    fun `a box that is not asking in place owes no such sentence`() {
        val state = askingInPlace(taffyTab(selected = true))
            .copy(conditions = StartConditions(asksInPlace = false))
        assertNull(noPagesLine(state))
    }

    @Test
    fun `an ask that has a page says nothing about the pages it has not`() {
        val tabs = listOf(userTab(selected = true), privateTab(id = "tab_private_2"))
        val state = AddressBarUiState(
            conditions = StartConditions(asksInPlace = true),
            pages = askPagesSnapshot(tabs),
            attachedPages = defaultAttachedPages(tabs),
        )
        assertTrue(state.pages.privateTabsWithheld)
        assertNull(noPagesLine(state))
    }

    private fun askingInPlace(vararg tabs: Tab) = AddressBarUiState(
        conditions = StartConditions(asksInPlace = true),
        pages = askPagesSnapshot(tabs.toList()),
        attachedPages = defaultAttachedPages(tabs.toList()),
    )
}

private fun userTab(id: String = "tab_user", selected: Boolean = false) = Tab(
    id = TabId(id),
    title = "Docs",
    host = "docs.example.test",
    isSelected = selected,
)

private fun taffyTab(
    id: String = "tab_taffy",
    selected: Boolean = false,
    isPrivate: Boolean = false,
) = Tab(
    id = TabId(id),
    title = "Aadhaar",
    host = "myaadhaar.uidai.test",
    isTaffyTab = true,
    isPrivate = isPrivate,
    isSelected = selected,
)

private fun privateTab(id: String = "tab_private", selected: Boolean = false) = Tab(
    id = TabId(id),
    title = "Shop",
    host = "shop.example.test",
    isPrivate = true,
    isSelected = selected,
)

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
import org.junit.Assert.assertTrue
import org.junit.Test

/** Add pages sheet: search, tick, caution over eight, never block. */
class AttachPagesReducerTest {

    private val snapshot = askPagesSnapshot(
        listOf(
            Tab(TabId("tab_1"), "Sony listing", "croma.com"),
            Tab(TabId("tab_2"), "Samsung listing", "samsung.com"),
            Tab(TabId("tab_p"), "Secret", "secret.example.test", isPrivate = true),
            Tab(TabId("tab_t"), "Work", "work.example.test", isTaffyTab = true),
            Tab(TabId("tab_n"), "", "", hasBeenNowhere = true),
        ),
    )

    @Test
    fun `private taffy and nowhere tabs are not rows`() {
        val sheet = attachPagesFromSnapshot(snapshot, alreadyAttached = listOf(TabId("tab_1")))

        assertEquals(2, sheet.rows.size)
        assertEquals(setOf(TabId("tab_1"), TabId("tab_2")), sheet.rows.map { it.tabId }.toSet())
        assertTrue(sheet.rows.single { it.tabId == TabId("tab_1") }.ticked)
        assertFalse(sheet.rows.single { it.tabId == TabId("tab_2") }.ticked)
        assertTrue(sheet.taffyTabsPresent)
    }

    @Test
    fun `search matches title or host`() {
        val sheet = attachPagesFromSnapshot(snapshot, alreadyAttached = emptyList())
        val titled = reduceAttachPages(sheet, AttachPagesIntent.SearchChanged("Sony"))
        val hosted = reduceAttachPages(sheet, AttachPagesIntent.SearchChanged("samsung"))

        assertEquals(listOf(TabId("tab_1")), titled.visibleRows.map { it.tabId })
        assertEquals(listOf(TabId("tab_2")), hosted.visibleRows.map { it.tabId })
    }

    @Test
    fun `toggling ticks and unticks without dropping the row`() {
        val sheet = attachPagesFromSnapshot(snapshot, alreadyAttached = emptyList())
        val ticked = reduceAttachPages(sheet, AttachPagesIntent.Toggle(TabId("tab_2")))
        val unticked = reduceAttachPages(ticked, AttachPagesIntent.Toggle(TabId("tab_2")))

        assertTrue(ticked.tickedIds.contains(TabId("tab_2")))
        assertTrue(ticked.rows.single { it.tabId == TabId("tab_2") }.ticked)
        assertFalse(unticked.tickedIds.contains(TabId("tab_2")))
    }

    @Test
    fun `more than eight ticked is a caution not a block`() {
        val many = (1..9).map { index ->
            Tab(TabId("tab_$index"), "Page $index", "site$index.example.test")
        }
        val sheet = attachPagesFromSnapshot(
            askPagesSnapshot(many),
            alreadyAttached = many.map { it.id },
        )

        assertTrue(sheet.cautionOverHandful)
        assertEquals(9, sheet.tickedIds.size)
        assertEquals(sheet, reduceAttachPages(sheet, AttachPagesIntent.Confirm))
    }

    @Test
    fun `confirm and dismiss change no sheet state`() {
        val sheet = attachPagesFromSnapshot(snapshot, alreadyAttached = listOf(TabId("tab_1")))

        assertEquals(sheet, reduceAttachPages(sheet, AttachPagesIntent.Confirm))
        assertEquals(sheet, reduceAttachPages(sheet, AttachPagesIntent.Dismiss))
    }
}

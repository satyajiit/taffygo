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

/**
 * The Ask sheet's scope rule (decisions 0034 and 0035).
 *
 * The sheet's consent row is a consent gate, so what it names is what the
 * person is being asked to allow. A tab that has been nowhere has no origin to allow, and a
 * private tab has an origin the person did not offer for reading or for
 * keeping. These tests are the statement of both in the one place a host can
 * run.
 */
class TaskScopeTest {

    private fun tab(
        id: String,
        host: String,
        hasBeenNowhere: Boolean = false,
        isTaffyTab: Boolean = false,
        isPrivate: Boolean = false,
        isSelected: Boolean = false,
    ) = Tab(
        id = TabId(id),
        title = host,
        host = host,
        hasBeenNowhere = hasBeenNowhere,
        isTaffyTab = isTaffyTab,
        isPrivate = isPrivate,
        isSelected = isSelected,
    )

    @Test
    fun `a selected tab that has been nowhere contributes no source`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab(
                    "tab_1",
                    "docs.example.test",
                    hasBeenNowhere = true,
                    isSelected = true,
                ),
                tab("tab_2", "shop.example.test"),
            ),
        )

        assertEquals(emptyList<String>(), scope)
    }

    @Test
    fun `a blank tab is the only tab, so nothing is in scope`() {
        assertEquals(
            emptyList<String>(),
            TaskScope.fromTabs(listOf(tab("tab_1", "", isSelected = true))),
        )
    }

    @Test
    fun `a host of nothing but spaces names no origin either`() {
        assertEquals(emptyList<String>(), TaskScope.fromHosts(listOf("   ")))
    }

    @Test
    fun `Taffy's own tabs are not sources the person chose`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test"),
                tab("tab_2", "shop.example.test", isTaffyTab = true, isSelected = true),
            ),
        )

        assertEquals(emptyList<String>(), scope)
    }

    @Test
    fun `only the selected tab contributes when another tab shares its host`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test", isSelected = true),
                tab("tab_2", "docs.example.test"),
            ),
        )

        assertEquals(listOf("docs.example.test"), scope)
    }

    @Test
    fun `another open user tab is not implicit consent`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test"),
                tab("tab_2", "shop.example.test", isSelected = true),
            ),
        )

        assertEquals(listOf("shop.example.test"), scope)
    }

    @Test
    fun `no selected tab contributes no source`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test"),
                tab("tab_2", "shop.example.test"),
            ),
        )

        assertEquals(emptyList<String>(), scope)
    }

    @Test
    fun `more than one selected tab fails closed`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test", isSelected = true),
                tab("tab_2", "shop.example.test", isSelected = true),
            ),
        )

        assertEquals(emptyList<String>(), scope)
    }

    @Test
    fun `named tab ids stay two chips when they share a host`() {
        val pages = TaskScope.pagesFromTabs(
            listOf(
                tab("tab_1", "docs.example.test"),
                tab("tab_2", "docs.example.test"),
            ),
            selectedIds = listOf(TabId("tab_1"), TabId("tab_2")),
        )

        assertEquals(2, pages.size)
        assertEquals(listOf("docs.example.test"), TaskScope.fromHosts(pages.map { it.host }))
    }

    @Test
    fun `named tab ids contribute every eligible host`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test", isSelected = true),
                tab("tab_2", "shop.example.test"),
                tab("tab_3", "reviews.example.test", isPrivate = true),
            ),
            selectedIds = listOf(TabId("tab_1"), TabId("tab_2"), TabId("tab_3")),
        )

        assertEquals(listOf("docs.example.test", "shop.example.test"), scope)
    }

    @Test
    fun `an explicit empty id list is not mixed with the selected tab`() {
        val scope = TaskScope.fromTabs(
            listOf(tab("tab_1", "docs.example.test", isSelected = true)),
            selectedIds = emptyList(),
        )

        assertEquals(emptyList<String>(), scope)
    }

    @Test
    fun `a private tab is not a source the person offered`() {
        val scope = TaskScope.fromTabs(
            listOf(
                tab("tab_1", "docs.example.test"),
                tab("tab_2", "shop.example.test", isPrivate = true, isSelected = true),
            ),
        )

        assertEquals(emptyList<String>(), scope)
    }

    @Test
    fun `every open tab is private, so nothing is in scope`() {
        val tabs = listOf(
            tab("tab_1", "docs.example.test", isPrivate = true, isSelected = true),
            tab("tab_2", "shop.example.test", isPrivate = true),
        )

        assertEquals(emptyList<String>(), TaskScope.fromTabs(tabs))
        assertTrue(TaskScope.privateTabsWithheld(tabs))
    }

    @Test
    fun `a host one of the person's own tabs also names stays in scope`() {
        val tabs = listOf(
            tab("tab_1", "docs.example.test", isSelected = true),
            tab("tab_2", "docs.example.test", isPrivate = true),
        )

        // The origin is in scope because a tab the person offered names it.
        // What the private tab is reading there is still not a source, and the
        // screen still owes them the reason a private tab is never offered.
        assertEquals(listOf("docs.example.test"), TaskScope.fromTabs(tabs))
        assertTrue(TaskScope.privateTabsWithheld(tabs))
    }

    @Test
    fun `a private tab that has been nowhere withholds nothing`() {
        val tabs = listOf(tab("tab_1", "docs.example.test"), tab("tab_2", "", isPrivate = true))

        assertFalse(TaskScope.privateTabsWithheld(tabs))
    }

    @Test
    fun `one of Taffy's own tabs is not the person's private tab`() {
        val tabs = listOf(
            tab(
                "tab_1",
                "shop.example.test",
                isTaffyTab = true,
                isPrivate = true,
                isSelected = true,
            ),
        )

        assertEquals(emptyList<String>(), TaskScope.fromTabs(tabs))
        assertFalse(TaskScope.privateTabsWithheld(tabs))
    }

    @Test
    fun `no private tab open, so nothing was withheld`() {
        val tabs = listOf(tab("tab_1", "docs.example.test"), tab("tab_2", "shop.example.test"))

        assertFalse(TaskScope.privateTabsWithheld(tabs))
    }

    @Test
    fun `the rule only ever removes hosts`() {
        val reported = listOf("docs.example.test", "", "shop.example.test")

        assertTrue(TaskScope.fromHosts(reported).all { it in reported })
        assertTrue(TaskScope.fromHosts(reported).size <= reported.size)
    }

}

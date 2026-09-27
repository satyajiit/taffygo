// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-104's projection and its two local intents. */
class TabSwitcherReducerTest {

    private val tabs = listOf(
        Tab(TabId("tab_1"), "Retention policy", "docs.example.test", isSelected = true),
        Tab(TabId("tab_2"), "Product listing", "shop.example.test"),
        Tab(TabId("tab_5"), "Price history", "prices.example.test", isPrivate = true),
        Tab(TabId("tab_9"), "Independent review", "reviews.example.test", isTaffyTab = true),
    )

    private val sources = listOf(
        source("src_1", "reviews.example.test", factCount = 2),
        source("src_2", "reviews.example.test", factCount = 1),
    )

    private fun source(id: String, host: String, factCount: Int, excluded: Boolean = false) =
        SourceRecord(
            id = SourceId(id),
            title = "Independent review",
            host = host,
            readAtEpochMillis = 0,
            factCount = factCount,
            excluded = excluded,
        )

    private fun project(
        tabs: List<Tab> = this.tabs,
        sources: List<SourceRecord> = this.sources,
        taskIsRunning: Boolean = false,
        group: TabSwitcherGroup = TabSwitcherGroup.YOURS,
        expanded: Boolean = false,
    ) = projectTabSwitcher(tabs, sources, taskIsRunning, group, expanded)

    @Test
    fun `yours, private and Taffy's are three groups`() {
        val state = project()

        assertEquals(listOf("tab_1", "tab_2"), state.yourTabs.map { it.id.value })
        assertEquals(listOf("tab_5"), state.privateTabs.map { it.id.value })
        assertEquals(listOf("tab_9"), state.taffyTabs.map { it.id.value })
        assertTrue(state.hasTaffyTabs)
        assertEquals(4, state.totalTabCount)
        assertTrue(state.canStartWorkspace)
    }

    @Test
    fun `a private card knows it is private and a Taffy card never is`() {
        val state = project()

        assertTrue(state.privateTabs.single().isPrivate)
        assertFalse(state.yourTabs.first().isPrivate)
        assertFalse(state.taffyTabs.single().isPrivate)
    }

    @Test
    fun `the segment decides which cards the grid draws`() {
        assertEquals(listOf("tab_1", "tab_2"), project().visibleTabs.map { it.id.value })
        assertEquals(
            listOf("tab_5"),
            project(group = TabSwitcherGroup.PRIVATE).visibleTabs.map { it.id.value },
        )
    }

    @Test
    fun `Taffy's group never appears over the private segment`() {
        assertTrue(project().showsTaffyGroup)
        assertFalse(project(group = TabSwitcherGroup.PRIVATE).showsTaffyGroup)
    }

    @Test
    fun `a Taffy card counts every fact its host supports`() {
        val card = project().taffyTabs.single()

        assertTrue(card.openedByTaffy)
        assertEquals(3, card.factCount)
        assertTrue(card.hasBadge)
        assertFalse(card.isBeingRead)
    }

    @Test
    fun `fact counts saturate instead of wrapping the badge negative`() {
        val state = project(
            sources = listOf(
                source("large", "reviews.example.test", Int.MAX_VALUE),
                source("more", "reviews.example.test", 1),
                source("invalid", "reviews.example.test", -10),
            ),
        )

        assertEquals(Int.MAX_VALUE, state.taffyTabs.single().factCount)
        assertTrue(state.taffyTabs.single().hasBadge)
    }

    @Test
    fun `a source the user excluded supports nothing`() {
        val state = project(
            sources = listOf(source("src_1", "reviews.example.test", 2, excluded = true)),
        )

        assertEquals(0, state.taffyTabs.single().factCount)
    }

    @Test
    fun `only a Taffy tab carries a count`() {
        val state = project(
            tabs = listOf(Tab(TabId("tab_3"), "Independent review", "reviews.example.test")),
        )

        assertEquals(0, state.yourTabs.single().factCount)
        assertFalse(state.yourTabs.single().hasBadge)
    }

    @Test
    fun `a Taffy tab with nothing yet is being read only while the task runs`() {
        val opened = listOf(
            Tab(TabId("tab_9"), "Specifications", "specs.example.test", isTaffyTab = true),
        )

        val running = project(tabs = opened, sources = emptyList(), taskIsRunning = true)
        assertTrue(running.taffyTabs.single().isBeingRead)
        assertTrue(running.taffyTabs.single().hasBadge)

        val stopped = project(tabs = opened, sources = emptyList(), taskIsRunning = false)
        assertFalse(stopped.taffyTabs.single().isBeingRead)
        assertFalse(stopped.taffyTabs.single().hasBadge)
    }

    @Test
    fun `a tab that produced facts is not reported as still being read`() {
        val state = project(taskIsRunning = true)

        assertFalse(state.taffyTabs.single().isBeingRead)
    }

    /**
     * The phone case behind decision 0232, as arithmetic.
     *
     * Fifteen tabs restored after a restart, every one named by the profile's
     * register of tabs Taffy created. The browser restores the mark and not the
     * task that made it, so each arrives as Taffy's with no creating task. The
     * count line has to say fifteen, which is what the start refusal counts as
     * `not_user_owned`; it used to say "0 opened by Taffy" and offer all fifteen
     * to Ask, where each one was then refused.
     */
    @Test
    fun `a restored Taffy tab with no creating task is still counted as Taffy's`() {
        val restored = (1..15).map { index ->
            Tab(
                TabId("tab_$index"),
                "Result $index",
                "site$index.example.test",
                isTaffyTab = true,
                taskId = null,
            )
        }

        val state = project(tabs = restored, sources = emptyList())

        assertEquals(15, state.totalTabCount)
        assertEquals(15, state.taffyTabs.size)
        assertTrue(state.yourTabs.isEmpty())
        assertTrue(state.taffyTabs.all { it.openedByTaffy && !it.canAskAbout })
    }

    @Test
    fun `no Taffy tabs means no group at all`() {
        assertFalse(project(tabs = tabs.filter { !it.isTaffyTab }).hasTaffyTabs)
    }

    @Test
    fun `nothing open means nothing to make a workspace from`() {
        val state = project(tabs = emptyList(), sources = emptyList())

        assertEquals(0, state.totalTabCount)
        assertEquals(0, state.workspaceTabCount)
        assertFalse(state.canStartWorkspace)
    }

    /**
     * The defect from the phone, as arithmetic.
     *
     * Three of the person's own tabs and one private tab: the workspace action
     * read "New workspace from 4 tabs…" and the consent it led to scoped 3. The
     * label is a promise about scope, and a private tab is never in one
     * (decision 0035), so the number it names is not the number of tabs open.
     */
    @Test
    fun `the workspace action does not count a private tab`() {
        val state = project(
            tabs = listOf(
                Tab(TabId("tab_1"), "Retention policy", "docs.example.test"),
                Tab(TabId("tab_2"), "Product listing", "shop.example.test"),
                Tab(TabId("tab_3"), "Independent review", "reviews.example.test"),
                Tab(TabId("tab_4"), "Price history", "prices.example.test", isPrivate = true),
            ),
            sources = emptyList(),
        )

        assertEquals("every tab is still open, and the count line says so", 4, state.totalTabCount)
        assertEquals(3, state.workspaceTabCount)
        assertTrue(state.canStartWorkspace)
    }

    /** Nor a tab Taffy opened, which is not the person's tab to offer. */
    @Test
    fun `the workspace action does not count one of Taffy's own tabs`() {
        assertEquals(2, project().workspaceTabCount)
    }

    /**
     * Nor a tab that has been nowhere, which names no page to read
     * (decision 0034).
     */
    @Test
    fun `the workspace action does not count a tab that has been nowhere`() {
        val state = project(
            tabs = listOf(
                Tab(TabId("tab_1"), "Retention policy", "docs.example.test"),
                Tab(TabId("tab_2"), "", "", hasBeenNowhere = true),
            ),
            sources = emptyList(),
        )

        assertEquals(2, state.totalTabCount)
        assertEquals(1, state.workspaceTabCount)
    }

    /**
     * Decision 0035's first consequence, on this screen.
     *
     * A person whose open tabs are all private is not offered a button that
     * leads to a preview telling them there is nothing to read.
     */
    @Test
    fun `all-private tabs offer no workspace to start`() {
        val state = project(
            tabs = listOf(
                Tab(TabId("tab_1"), "Price history", "prices.example.test", isPrivate = true),
                Tab(TabId("tab_2"), "Retention policy", "docs.example.test", isPrivate = true),
            ),
            sources = emptyList(),
        )

        assertEquals(2, state.totalTabCount)
        assertEquals(0, state.workspaceTabCount)
        assertFalse(state.canStartWorkspace)
    }

    @Test
    fun `the group collapses and expands`() {
        val collapsed = project()
        val expanded = reduceTabSwitcher(collapsed, TabSwitcherIntent.ToggleTaffyGroup)

        assertTrue(expanded.taffyGroupExpanded)
        assertFalse(
            reduceTabSwitcher(expanded, TabSwitcherIntent.ToggleTaffyGroup).taffyGroupExpanded,
        )
    }

    @Test
    fun `choosing a segment is a local change`() {
        val state = project()
        val private = reduceTabSwitcher(
            state,
            TabSwitcherIntent.SelectGroup(TabSwitcherGroup.PRIVATE),
        )

        assertEquals(TabSwitcherGroup.PRIVATE, private.group)
        assertEquals(listOf("tab_5"), private.visibleTabs.map { it.id.value })
    }

    @Test
    fun `selecting, closing, opening and starting a workspace change the browser, not this state`() {
        val state = project()

        assertEquals(state, reduceTabSwitcher(state, TabSwitcherIntent.Select(TabId("tab_1"))))
        assertEquals(state, reduceTabSwitcher(state, TabSwitcherIntent.Close(TabId("tab_1"))))
        assertEquals(state, reduceTabSwitcher(state, TabSwitcherIntent.NewTab))
        assertEquals(state, reduceTabSwitcher(state, TabSwitcherIntent.StartWorkspace))
        assertEquals(state, reduceTabSwitcher(state, TabSwitcherIntent.AskTaffy))
    }

    @Test
    fun `long-pressing an eligible card starts Ask Taffy selection`() {
        val state = reduceTabSwitcher(project(), TabSwitcherIntent.LongPress(TabId("tab_2")))

        assertTrue(state.isSelecting)
        assertEquals(setOf(TabId("tab_2")), state.selectedForAsk)
        assertTrue(state.yourTabs.single { it.id.value == "tab_2" }.checkedForAsk)
        assertEquals(listOf(TabId("tab_2")), state.askTaffyTabIds)
        assertTrue(state.canAskTaffy)
    }

    @Test
    fun `a private or empty card cannot enter the Ask Taffy selection`() {
        val state = project()
        assertEquals(state, reduceTabSwitcher(state, TabSwitcherIntent.LongPress(TabId("tab_5"))))

        val withBlank = project(
            tabs = tabs + Tab(TabId("tab_blank"), "", "", hasBeenNowhere = true),
        )
        assertEquals(
            withBlank,
            reduceTabSwitcher(withBlank, TabSwitcherIntent.LongPress(TabId("tab_blank"))),
        )
    }

    @Test
    fun `a restored selection drops ids that are no longer eligible`() {
        val state = projectTabSwitcher(
            tabs = tabs,
            sources = sources,
            taskIsRunning = false,
            group = TabSwitcherGroup.YOURS,
            taffyGroupExpanded = false,
            selectedForAsk = setOf(TabId("tab_5"), TabId("tab_9")),
            isSelecting = true,
        )

        assertTrue(state.selectedForAsk.isEmpty())
        assertFalse(state.isSelecting)
    }

    @Test
    fun `with nothing chosen Ask Taffy uses the active eligible tab`() {
        val state = project()

        assertEquals(listOf(TabId("tab_1")), state.askTaffyTabIds)
        assertTrue(state.canAskTaffy)
    }

    @Test
    fun `projecting cards reads each tab only once`() {
        val measuredTabs = CountingList(
            (0 until 32).map { index ->
                Tab(
                    id = TabId("tab_$index"),
                    title = "Page $index",
                    host = "site$index.example.test",
                    isPrivate = index % 3 == 0,
                    isTaffyTab = index % 5 == 0,
                )
            },
        )

        project(tabs = measuredTabs, sources = emptyList())

        assertEquals(measuredTabs.size, measuredTabs.elementReads)
    }

    @Test
    fun `updating a large Ask Taffy selection stays linear in the card count`() {
        val cards = (0 until 64).map { index ->
            TabCard(
                id = TabId("tab_$index"),
                title = "Page $index",
                host = "site$index.example.test",
                checkedForAsk = true,
            )
        }
        val measuredCards = CountingList(cards)
        val state = TabSwitcherUiState(
            yourTabs = measuredCards,
            selectedForAsk = cards.mapTo(linkedSetOf()) { it.id },
            isSelecting = true,
        )

        reduceTabSwitcher(state, TabSwitcherIntent.ToggleSelected(cards.last().id))

        assertTrue(
            "selection update read ${measuredCards.elementReads} elements for ${cards.size} cards",
            measuredCards.elementReads <= cards.size * 4,
        )
    }

    private class CountingList<T>(
        private val values: List<T>,
    ) : AbstractList<T>() {
        var elementReads: Int = 0
            private set

        override val size: Int
            get() = values.size

        override fun get(index: Int): T {
            elementReads += 1
            return values[index]
        }
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * What screen SCR-104's close-all closes, in both directions (decision 0236).
 *
 * The sheet says tabs Taffy opened for a running task stay open. It kept every
 * Taffy tab instead, so on a phone restarted over earlier errands fourteen tabs
 * no task held survived "Close them" and had to be closed one at a time.
 */
class TabSwitcherCloseAllTest {

    private val yours = Tab(TabId("tab_1"), "Retention policy", "docs.example.test")
    private val privateTab = Tab(TabId("tab_2"), "Price history", "prices.example.test", isPrivate = true)

    private fun taffyTab(id: String, taskId: String?) = Tab(
        TabId(id),
        "Result $id",
        "$id.example.test",
        isTaffyTab = true,
        taskId = taskId,
    )

    private fun project(
        tabs: List<Tab>,
        holding: Set<String>?,
        group: TabSwitcherGroup = TabSwitcherGroup.YOURS,
    ) = projectTabSwitcher(
        tabs = tabs,
        sources = emptyList(),
        taskIsRunning = false,
        group = group,
        taffyGroupExpanded = false,
        tabHoldingTaskIds = holding,
    )

    private fun TabSwitcherUiState.closedIds() = closeVisibleTabs.map { it.id.value }

    @Test
    fun `a tab its running task still holds stays open`() {
        val state = project(listOf(yours, taffyTab("tab_9", "task-live")), setOf("task-live"))

        assertTrue(state.taffyTabs.single().heldByTask)
        assertEquals(listOf("tab_1"), state.closedIds())
    }

    @Test
    fun `a restored Taffy tab with no creating task closes with the rest`() {
        val state = project(listOf(yours, taffyTab("tab_9", null)), setOf("task-live"))

        assertFalse(state.taffyTabs.single().heldByTask)
        assertEquals(listOf("tab_1", "tab_9"), state.closedIds())
    }

    @Test
    fun `a tab whose task has ended or is gone closes with the rest`() {
        val state = project(
            listOf(yours, taffyTab("tab_8", "task-done"), taffyTab("tab_9", "task-live")),
            setOf("task-live"),
        )

        assertEquals(listOf("tab_1", "tab_8"), state.closedIds())
    }

    /**
     * The phone case: fourteen of the person's tabs and fourteen restored tabs
     * of Taffy's, with the core's store set aside so no task exists at all.
     */
    @Test
    fun `with no task running every Taffy tab closes`() {
        val own = (1..14).map { Tab(TabId("own_$it"), "Page $it", "own$it.example.test") }
        val restored = (1..14).map { taffyTab("taffy_$it", null) }

        val state = project(own + restored, emptySet())

        assertEquals(28, state.totalTabCount)
        assertEquals(28, state.closeVisibleTabs.size)
    }

    /** A core that is not ready has not shown any task ended. */
    @Test
    fun `a core that cannot say which tasks exist keeps every tab with a creating task`() {
        val state = project(
            listOf(yours, taffyTab("tab_8", "task-unknown"), taffyTab("tab_9", null)),
            holding = null,
        )

        assertEquals(listOf("tab_1", "tab_9"), state.closedIds())
    }

    @Test
    fun `the close control is offered over Taffy tabs no task holds even with none of yours`() {
        val state = project(listOf(taffyTab("tab_9", "task-done")), emptySet())

        assertTrue(state.visibleTabs.isEmpty())
        assertEquals(listOf("tab_9"), state.closedIds())
    }

    @Test
    fun `the private segment closes only its own tabs`() {
        val state = project(
            listOf(yours, privateTab, taffyTab("tab_9", null)),
            emptySet(),
            group = TabSwitcherGroup.PRIVATE,
        )

        assertEquals(listOf("tab_2"), state.closedIds())
    }

    /**
     * A search narrows what the grid draws, never what the destructive button
     * closes.
     */
    @Test
    fun `a search typed and forgotten does not change what closes`() {
        val state = projectTabSwitcher(
            tabs = listOf(yours, taffyTab("tab_9", null)),
            sources = emptyList(),
            taskIsRunning = false,
            group = TabSwitcherGroup.YOURS,
            taffyGroupExpanded = false,
            searchQuery = "nothing matches this",
            tabHoldingTaskIds = emptySet(),
        )

        assertTrue(state.matchingTabs.isEmpty())
        assertEquals(listOf("tab_1", "tab_9"), state.closedIds())
    }

    @Test
    fun `a task holds its tabs until it has ended`() {
        val phases = mapOf(
            "planning" to TaskPhase.PLANNING,
            "running" to TaskPhase.RUNNING,
            "waiting" to TaskPhase.WAITING_FOR_USER,
            "paused" to TaskPhase.PAUSED,
            "idle" to TaskPhase.IDLE,
            "done" to TaskPhase.COMPLETED,
            "partly" to TaskPhase.PARTIAL,
            "stopped" to TaskPhase.CANCELLED,
            "failed" to TaskPhase.FAILED,
            "unknown" to TaskPhase.OUTCOME_UNKNOWN,
        ).map { (id, phase) -> startedTask(id, "goal", phase = phase) }
        val state = TaskRepositoryState(
            CoreUiAvailability.READY,
            1u,
            task = phases.first(),
            others = phases.drop(1),
        )

        assertEquals(
            setOf("planning", "running", "waiting", "paused", "idle"),
            state.tabHoldingTaskIds(),
        )
    }

    @Test
    fun `only a ready core can say which tasks hold tabs`() {
        val running = startedTask("running", "goal", phase = TaskPhase.RUNNING)
        for (availability in CoreUiAvailability.entries - CoreUiAvailability.READY) {
            assertNull(
                availability.name,
                TaskRepositoryState(availability, 1u, task = running).tabHoldingTaskIds(),
            )
        }
        assertEquals(
            emptySet<String>(),
            TaskRepositoryState(CoreUiAvailability.READY, 1u, task = null).tabHoldingTaskIds(),
        )
    }
}

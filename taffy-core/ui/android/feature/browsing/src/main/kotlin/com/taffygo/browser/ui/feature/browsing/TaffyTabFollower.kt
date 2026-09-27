// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first

/**
 * Moves the screen to the first tab Taffy opens for a task the box started.
 *
 * A task started from the start page works out of sight until it has a page:
 * the core opens Taffy's tab, the engine may foreground it, and the screen
 * the person is looking at is still the box they typed into. This is the one
 * move from that box to Taffy's page (decision 0132 section 2): the tab is
 * selected, the blank tab the box opened on first focus is closed if it is
 * still blank, and the browsing surface replaces the start page. It moves
 * once. Later tabs are the task's own to activate, and a person who then
 * chooses a tab of their own is not moved off it.
 *
 * There is no task-to-tab correlation in the Core API, so "Taffy's tab for
 * this task" is a Taffy tab that was not there when the start was sent —
 * which is why the caller takes the snapshot before sending.
 */
internal class TaffyTabFollower(
    private val browser: BrowserRepository,
    private val tasks: TaskRepository,
) {
    /**
     * Wait for the task's first Taffy tab and go to it, or return when the
     * task ends without one.
     *
     * @param before the tabs as they stood before the start was sent.
     */
    suspend fun follow(taskId: String, before: List<Tab>, navigator: TaffyNavigator) {
        val known = before.filter(Tab::isTaffyTab).map(Tab::id).toSet()
        val blank = before.firstOrNull { it.isSelected && it.hasBeenNowhere && !it.isTaffyTab }?.id
        val sighting = combine(browser.tabs, tasks.state) { tabs, repository ->
            Sighting(
                tab = newTaffyTab(known, tabs),
                underWay = repository.tasks.any { it.id == taskId && it.isUnderWay },
            )
        }.first { it.tab != null || !it.underWay }
        val arrived = sighting.tab ?: return
        browser.selectTab(arrived)
        // Closed after the select, so closing never chooses a tab of its own,
        // and only while it is still blank: a page the person opened there
        // meanwhile is theirs to keep.
        if (blank != null && blank != arrived &&
            browser.tabs.value.any { it.id == blank && it.hasBeenNowhere }
        ) {
            browser.closeTab(blank)
        }
        navigator.replaceCurrent(TaffyDestination.BrowserMain)
    }

    private data class Sighting(val tab: TabId?, val underWay: Boolean)
}

/** The newest Taffy tab that was not among [known], or null. */
internal fun newTaffyTab(known: Set<TabId>, tabs: List<Tab>): TabId? =
    tabs.lastOrNull { it.isTaffyTab && it.id !in known }?.id

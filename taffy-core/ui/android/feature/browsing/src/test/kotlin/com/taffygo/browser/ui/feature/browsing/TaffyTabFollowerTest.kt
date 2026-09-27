// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskPhase

/** The one move from the box to Taffy's page. */
@OptIn(ExperimentalCoroutinesApi::class)
class TaffyTabFollowerTest {

    private val blank = Tab(id = TabId("blank"), title = "", host = "", hasBeenNowhere = true, isSelected = true)
    private val theirs = Tab(id = TabId("theirs"), title = "News", host = "news.example")
    private val earlier = Tab(id = TabId("taffy-old"), title = "Earlier", host = "old.example", isTaffyTab = true)
    private val taffys = Tab(id = TabId("taffy-1"), title = "UIDAI", host = "uidai.gov.in", isTaffyTab = true)

    @Test
    fun `the first Taffy tab is selected, the blank tab closed, and the screen moved`() = runTest {
        val browser = PagesTestBrowser()
        val tasks = RecordingTaskRepository()
        val navigator = Recorder()
        val before = listOf(theirs, blank)
        browser.showTabs(before)
        tasks.publish(task(TaskPhase.RUNNING))
        val follow = launch { TaffyTabFollower(browser, tasks).follow("task-1", before, navigator) }
        runCurrent()
        assertTrue(browser.selected.isEmpty())
        assertTrue(navigator.replaced.isEmpty())

        browser.showTabs(listOf(theirs, blank, taffys))
        runCurrent()

        assertEquals(listOf(TabId("taffy-1")), browser.selected)
        assertEquals(listOf(TabId("blank")), browser.closed)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
        assertTrue(follow.isCompleted)
    }

    @Test
    fun `a Taffy tab that was already there is not the task's`() = runTest {
        val browser = PagesTestBrowser()
        val tasks = RecordingTaskRepository()
        val navigator = Recorder()
        val before = listOf(earlier, blank)
        browser.showTabs(before)
        tasks.publish(task(TaskPhase.RUNNING))
        val follow = launch { TaffyTabFollower(browser, tasks).follow("task-1", before, navigator) }
        runCurrent()

        // The list changes, but no new Taffy tab is in it.
        browser.showTabs(listOf(blank, earlier))
        runCurrent()
        assertTrue(browser.selected.isEmpty())

        tasks.publish(task(TaskPhase.COMPLETED))
        runCurrent()

        assertTrue(follow.isCompleted)
        assertTrue(browser.selected.isEmpty())
        assertTrue(browser.closed.isEmpty())
        assertTrue(navigator.replaced.isEmpty())
    }

    @Test
    fun `a blank tab the person has since used is kept`() = runTest {
        val browser = PagesTestBrowser()
        val tasks = RecordingTaskRepository()
        val navigator = Recorder()
        val before = listOf(blank)
        browser.showTabs(before)
        tasks.publish(task(TaskPhase.PLANNING))
        launch { TaffyTabFollower(browser, tasks).follow("task-1", before, navigator) }
        runCurrent()

        val used = blank.copy(host = "news.example", hasBeenNowhere = false)
        browser.showTabs(listOf(used, taffys))
        runCurrent()

        assertEquals(listOf(TabId("taffy-1")), browser.selected)
        assertTrue(browser.closed.isEmpty())
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
    }

    @Test
    fun `the newest Taffy tab not known before is the one`() {
        val known = setOf(TabId("taffy-old"))
        assertEquals(null, newTaffyTab(known, listOf(theirs, earlier)))
        assertEquals(TabId("taffy-1"), newTaffyTab(known, listOf(earlier, taffys, theirs)))
        assertEquals(TabId("taffy-2"), newTaffyTab(known, listOf(taffys, taffys.copy(id = TabId("taffy-2")))))
    }

    private fun task(phase: TaskPhase) = TaskProjection(
        id = "task-1",
        revision = 1u,
        phase = phase,
        goal = "download my aadhaar",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 0u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
    )

    private class Recorder : TaffyNavigator {
        val replaced = mutableListOf<TaffyDestination>()
        override fun goTo(destination: TaffyDestination) = Unit
        override fun replaceCurrent(destination: TaffyDestination) {
            replaced += destination
        }
        override fun goBack() = false
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }
}

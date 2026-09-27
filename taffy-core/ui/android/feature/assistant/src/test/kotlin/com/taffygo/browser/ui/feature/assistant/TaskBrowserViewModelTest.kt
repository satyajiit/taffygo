// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

@OptIn(ExperimentalCoroutinesApi::class)
class TaskBrowserViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val browser = TaskBrowserTestRepository()
    private val tasks = TestTasks()
    private val source = Tab(TabId("existing-source"), "Shop", "shop.example.test")

    @Before fun setUp() {
        Dispatchers.setMain(dispatcher)
        browser.tabs.value = listOf(source)
        tasks.status.value = tasks.status.value.copy(task = task("comparison", 1u))
    }
    @After fun tearDown() = Dispatchers.resetMain()

    @Test fun `membership is requested on demand and refreshed for a new task revision`() = runTest(dispatcher) {
        browser.members = { setOf(source.id) }
        val model = TaskBrowserViewModel(browser, tasks)
        runCurrent()
        assertTrue(browser.queries.isEmpty())
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        assertEquals(listOf(source), model.state.value.tabs)
        assertEquals(listOf("comparison"), browser.queries)
        browser.tabArtwork.value = mapOf(source.id to TabArtwork())
        runCurrent()
        assertEquals(1, browser.queries.size)
        browser.members = { emptySet() }
        tasks.status.value = tasks.status.value.copy(task = task("comparison", 2u))
        runCurrent()
        assertEquals(2, browser.queries.size)
        assertTrue(model.state.value.tabs.isEmpty())
        assertTrue(model.state.value.artwork.isEmpty())
    }

    @Test fun `a delayed previous-task membership cannot populate the new task`() = runTest(dispatcher) {
        val oldAnswer = CompletableDeferred<Set<TabId>>()
        browser.members = { id -> if (id == "comparison") oldAnswer.await() else emptySet() }
        val model = TaskBrowserViewModel(browser, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        tasks.status.value = tasks.status.value.copy(task = task("next", 1u))
        runCurrent()
        oldAnswer.complete(setOf(source.id))
        runCurrent()
        assertEquals("next", model.state.value.taskId)
        assertTrue(model.state.value.tabs.isEmpty())
        assertEquals(listOf("comparison", "next"), browser.queries)
    }

    @Test fun `opening an existing source waits for native admission and ignores a stale task card`() = runTest(dispatcher) {
        browser.members = { setOf(source.id) }
        val navigator = RecordingNavigator()
        val model = TaskBrowserViewModel(browser, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        model.openTab("comparison", source.id, navigator)
        runCurrent()
        assertTrue(navigator.destinations.isEmpty())
        browser.selectionAccepted = true
        model.openTab("comparison", source.id, navigator)
        runCurrent()
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.destinations)
        tasks.status.value = tasks.status.value.copy(task = task("next", 1u))
        model.openTab("comparison", source.id, navigator)
        runCurrent()
        assertEquals(listOf(source.id to "comparison", source.id to "comparison"), browser.selections)
    }

    private fun task(id: String, revision: ULong) = TaskProjection(
        id = id, revision = revision, phase = TaskPhase.RUNNING, goal = "Compare this phone",
        template = TaskTemplate.COMPARE_PRODUCTS, progressBasisPoints = 0u,
        statusMessageKey = null, failure = null, pendingAction = null,
    )
}

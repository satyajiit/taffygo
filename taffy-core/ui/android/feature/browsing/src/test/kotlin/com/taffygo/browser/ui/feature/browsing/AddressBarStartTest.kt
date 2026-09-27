// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.MAX_ERRAND_NEW_SOURCES
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * The box starting a task where it stands.
 *
 * There is no preview screen between the words and the task any more: the
 * rows under the box say the consent, sending starts the errand, and the box
 * waits for the core to name the task and then follows it. These tests hold
 * the seam between the box and the task repository — what is sent, what is
 * waited for, and what the box says when the core says no.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class AddressBarStartTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `an errand reading starts in place and the box follows the task`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val browser = StartTestBrowser(resolve = ::errand)
            val box = AddressBarViewModel(browser, NoEvents, SavedStateHandle(), tasks, FixedReadiness())
            runCurrent()

            box.onIntent(AddressBarIntent.InputChanged("download my aadhaar"), navigator)
            val reading = checkNotNull(box.state.value.reading)
            assertTrue(box.state.value.start is TaskStartDecision.Start)

            box.onIntent(AddressBarIntent.Choose(reading), navigator)
            assertTrue(box.state.value.starting)
            assertEquals("download my aadhaar", box.state.value.input)
            runCurrent()

            val start = tasks.starts.single()
            assertEquals("download my aadhaar", start.goal)
            assertEquals(TaskTemplate.WEB_ERRAND, start.template)
            assertTrue(start.consent.sourceHosts.isEmpty())
            assertTrue(start.consent.sourceDiscoveryEnabled)
            assertEquals(MAX_ERRAND_NEW_SOURCES, start.consent.newSourceCap)
            assertEquals(ProviderRoute.DIRECT_WITH_YOUR_KEY, start.consent.providerRoute)
            // Admitted is not started: the task has not been named yet.
            assertTrue(box.state.value.starting)
            assertNull(box.state.value.started)

            tasks.publish(task("task-1", "download my aadhaar"))
            runCurrent()

            assertEquals(StartedTask("task-1", "download my aadhaar"), box.state.value.started)
            assertFalse(box.state.value.starting)
            assertEquals("task-1", tasks.followed)
            // Nothing left the box: no navigation, and nothing committed to
            // the browser as if the words were an address.
            assertTrue(navigator.replaced.isEmpty())
            assertTrue(navigator.visited.isEmpty())
            assertTrue(browser.committed.isEmpty())
        }

    /**
     * The panel of a task that ended can send the same request again, and
     * the box follows the new task; or it can go home, which empties the box.
     */
    @Test
    fun `an ended task is tried again from its panel or left for the start page`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val box = AddressBarViewModel(
                StartTestBrowser(resolve = ::errand),
                NoEvents,
                SavedStateHandle(),
                tasks,
                FixedReadiness(),
            )
            runCurrent()
            box.onIntent(AddressBarIntent.InputChanged("download my aadhaar"), navigator)
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()
            tasks.publish(task("task-1", "download my aadhaar"))
            runCurrent()
            assertEquals("task-1", box.state.value.started?.id)

            // Nothing to try again before anything has ended: a running task
            // still counts as one, and the box stays on it.
            box.onIntent(AddressBarIntent.TryAgain, navigator)
            runCurrent()
            assertEquals("task-1", box.state.value.started?.id)
            assertEquals(1, tasks.starts.size)

            tasks.publish(task("task-1", "download my aadhaar").copy(phase = TaskPhase.FAILED))
            runCurrent()
            box.onIntent(AddressBarIntent.TryAgain, navigator)
            assertTrue(box.state.value.starting)
            assertNull(box.state.value.started)
            runCurrent()
            assertEquals(2, tasks.starts.size)
            assertEquals("download my aadhaar", tasks.starts.last().goal)

            tasks.publish(task("task-2", "download my aadhaar"))
            runCurrent()
            assertEquals(StartedTask("task-2", "download my aadhaar"), box.state.value.started)
            assertEquals("task-2", tasks.followed)

            box.onIntent(AddressBarIntent.LeaveTask, navigator)
            assertNull(box.state.value.started)
            assertEquals("", box.state.value.input)
            assertTrue(navigator.visited.isEmpty())
            assertTrue(navigator.replaced.isEmpty())
        }

    @Test
    fun `a refused start says why under the box and keeps the words`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository().apply {
                startResult = TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
            }
            val box = AddressBarViewModel(
                StartTestBrowser(resolve = ::errand),
                NoEvents,
                SavedStateHandle(),
                tasks,
                FixedReadiness(),
            )
            runCurrent()
            box.onIntent(AddressBarIntent.InputChanged("download my aadhaar"), RecordingNavigation())

            box.onIntent(
                AddressBarIntent.Choose(checkNotNull(box.state.value.reading)),
                RecordingNavigation(),
            )
            runCurrent()

            assertEquals(TaskStartFailure.CORE_UNAVAILABLE, box.state.value.startFailure)
            assertFalse(box.state.value.starting)
            assertEquals("download my aadhaar", box.state.value.input)

            // The next keystroke takes the failure with it.
            box.onIntent(AddressBarIntent.InputChanged("download my aadhaar card"), RecordingNavigation())
            assertNull(box.state.value.startFailure)
        }

    @Test
    fun `a start the core admits and never names times out under the box`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val box = AddressBarViewModel(
                StartTestBrowser(resolve = ::errand),
                NoEvents,
                SavedStateHandle(),
                tasks,
                FixedReadiness(),
            )
            runCurrent()
            box.onIntent(AddressBarIntent.InputChanged("download my aadhaar"), RecordingNavigation())
            box.onIntent(
                AddressBarIntent.Choose(checkNotNull(box.state.value.reading)),
                RecordingNavigation(),
            )
            runCurrent()
            assertEquals(1, tasks.starts.size)

            advanceTimeBy(5_001)
            runCurrent()

            assertEquals(TaskStartFailure.DEADLINE_EXCEEDED, box.state.value.startFailure)
            assertFalse(box.state.value.starting)
            assertNull(box.state.value.started)
        }

    @Test
    fun `a question on a blank tab starts an errand and over a page opens the overlay`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val blank = StartTestBrowser(
                tabs = listOf(Tab(TabId("t1"), "", "", isSelected = true)),
                resolve = { AddressBarInterpretation.AskTaffy(it) },
            )
            val onBlank = AddressBarViewModel(blank, NoEvents, SavedStateHandle(), tasks, FixedReadiness())
            runCurrent()
            onBlank.onIntent(AddressBarIntent.InputChanged("how do I renew a passport?"), navigator)
            onBlank.onIntent(AddressBarIntent.Choose(checkNotNull(onBlank.state.value.reading)), navigator)
            runCurrent()

            assertEquals(TaskTemplate.WEB_ERRAND, tasks.starts.single().template)
            assertTrue(navigator.replaced.isEmpty())

            val page = StartTestBrowser(
                tabs = listOf(Tab(TabId("t2"), "Docs", "docs.example.test", isSelected = true)),
                resolve = { AddressBarInterpretation.AskTaffy(it) },
            )
            val overPage = AddressBarViewModel(page, NoEvents, SavedStateHandle(), tasks, FixedReadiness())
            runCurrent()
            overPage.onIntent(AddressBarIntent.InputChanged("is this fee refundable?"), navigator)
            assertNull(overPage.state.value.start)
            overPage.onIntent(AddressBarIntent.Choose(checkNotNull(overPage.state.value.reading)), navigator)
            runCurrent()

            val overlay = navigator.replaced.single() as TaffyDestination.AssistantBar
            assertEquals("is this fee refundable?", overlay.question)
            assertNull(overlay.shape)
            assertEquals(1, tasks.starts.size)
            assertEquals("", overPage.state.value.input)
        }

    @Test
    fun `nothing set up is said under the box and the row opens set-up over it`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val readiness = FixedReadiness(TaffyReadiness.NotSetUp)
            val box = AddressBarViewModel(
                StartTestBrowser(resolve = ::errand),
                NoEvents,
                SavedStateHandle(),
                tasks,
                readiness,
            )
            runCurrent()
            box.onIntent(AddressBarIntent.InputChanged("download my aadhaar"), navigator)

            assertEquals(
                TaskStartDecision.Refused(TaskStartRefusal.SETUP_NEEDED),
                box.state.value.start,
            )
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()

            assertEquals(listOf(TaffyDestination.AiAndProviders), navigator.visited)
            assertTrue(navigator.replaced.isEmpty())
            assertTrue(tasks.starts.isEmpty())
            assertEquals("download my aadhaar", box.state.value.input)

            // Back from set-up with a provider connected, the same words are a
            // start the row now offers — nothing retyped.
            readiness.become(TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY))
            runCurrent()
            val start = box.state.value.start as TaskStartDecision.Start
            assertEquals(ProviderRoute.DIRECT_WITH_YOUR_KEY, start.request.consent.providerRoute)
        }

    private fun errand(input: String): AddressBarInterpretation =
        AddressBarInterpretation.TaskForTaffy(input, TaskTemplate.WEB_ERRAND)

    private fun task(id: String, goal: String): TaskProjection = startedTask(id, goal)
}

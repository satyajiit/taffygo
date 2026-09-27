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
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.resetMain
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
 * The box on the Ask overlay, from the question it opened with to the next
 * one (decisions 0135, 0137): what it restores, what it sends, when a send is
 * the next turn of a conversation and when it is a fresh start.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class AskComposerStartTest {

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
    fun `the overlay opens on its question with the page it stands over attached`() =
        runTest(dispatcher) {
            val box = AddressBarViewModel(
                StartTestBrowser(tabs = listOf(docs), resolve = ::asks),
                NoEvents,
                opened(question = "what is this page for?"),
                RecordingTaskRepository(),
                FixedReadiness(),
            )
            runCurrent()

            val state = box.state.value
            assertEquals("what is this page for?", state.input)
            assertTrue(state.conditions.asksInPlace)
            assertEquals(listOf(docs.id), state.attachedPages.map { it.tabId })
            assertEquals(listOf(docs), state.pages.eligibleTabs)
            val start = state.start as TaskStartDecision.Start
            assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, start.request.template)
        }

    @Test
    fun `the switcher's picked tabs arrive attached and a saved empty list means none`() =
        runTest(dispatcher) {
            val picked = AddressBarViewModel(
                StartTestBrowser(tabs = listOf(docs, shop), resolve = ::asks),
                NoEvents,
                opened(question = "", attached = listOf(shop.id)),
                RecordingTaskRepository(),
                FixedReadiness(),
            )
            runCurrent()
            assertEquals(listOf(shop.id), picked.state.value.attachedPages.map { it.tabId })

            val cleared = AddressBarViewModel(
                StartTestBrowser(tabs = listOf(docs), resolve = ::asks),
                NoEvents,
                opened(question = "q").apply { set("address_bar_attached", "") },
                RecordingTaskRepository(),
                FixedReadiness(),
            )
            runCurrent()
            assertTrue(cleared.state.value.attachedPages.isEmpty())
        }

    @Test
    fun `a question over a page starts in place empties the box and follows nothing`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val browser = StartTestBrowser(tabs = listOf(docs), resolve = ::asks)
            val box = AddressBarViewModel(browser, NoEvents, opened("what is this page for?"), tasks, FixedReadiness())
            runCurrent()

            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()
            val start = tasks.starts.single()
            assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, start.template)
            assertEquals(listOf("docs.example.test"), start.consent.sourceHosts)
            assertFalse(start.consent.sourceDiscoveryEnabled)

            tasks.publish(startedTask("task-1", "what is this page for?", TaskTemplate.SUMMARIZE_EVIDENCE))
            runCurrent()

            val state = box.state.value
            assertEquals(StartedTask("task-1", "what is this page for?"), state.started)
            assertEquals("", state.input)
            assertEquals(listOf(docs.id), state.attachedPages.map { it.tabId })
            assertEquals("task-1", tasks.followed)
            assertTrue(navigator.replaced.isEmpty())
            assertTrue(navigator.visited.isEmpty())
            assertTrue(browser.committed.isEmpty())
        }

    @Test
    fun `a published comparison opens its workspace with the Ask conversation kept beneath`() =
        runTest(dispatcher) {
            val goal = "compare these phones"
            val ask = TaffyDestination.AssistantBar(goal)
            var stack = BackStack.start(TaffyDestination.BrowserMain).push(ask)
            val tasks = RecordingTaskRepository().apply {
                publish(startedTask("previous", "old question", phase = TaskPhase.COMPLETED))
            }
            val handle = opened(goal, listOf(docs.id, shop.id))
            val box = comparisonBox(tasks, handle)
            val navigation = RecordingNavigation()
            val navigator = object : TaffyNavigator by navigation {
                override fun goTo(destination: TaffyDestination) {
                    assertEquals("comparison", tasks.followed)
                    assertEquals("comparison", box.state.value.started?.id)
                    navigation.goTo(destination)
                    stack = stack.push(destination)
                }
            }
            runCurrent()
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()
            val start = tasks.starts.single()
            assertEquals(TaskTemplate.COMPARE_PRODUCTS, start.template)
            assertEquals(listOf("docs.example.test", "shop.example.test"), start.consent.sourceHosts)
            assertFalse(start.consent.sourceDiscoveryEnabled)
            assertTrue(navigation.visited.isEmpty())
            assertTrue(box.state.value.starting)

            tasks.publish(startedTask("comparison", goal, TaskTemplate.COMPARE_PRODUCTS))
            runCurrent()
            assertEquals(listOf(TaffyDestination.TaskView), navigation.visited)
            assertTrue(navigation.replaced.isEmpty())
            assertEquals(listOf(TaffyDestination.BrowserMain, ask, TaffyDestination.TaskView), stack.entries)
            stack = stack.pop()
            assertEquals(ask, stack.current)
            assertEquals("", handle.get<String>("address_bar_input"))
            assertEquals(listOf(docs.id, shop.id), box.state.value.attachedPages.map { it.tabId })

            tasks.publish(startedTask("comparison", goal, TaskTemplate.COMPARE_PRODUCTS, TaskPhase.COMPLETED))
            runCurrent()
            box.onIntent(AddressBarIntent.InputChanged("which has more storage?"), navigator)
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()
            assertEquals(listOf(RecordingTaskRepository.FollowUp("comparison", "which has more storage?")), tasks.followUps)
            assertEquals(1, tasks.starts.size)
            assertEquals(listOf(TaffyDestination.TaskView), navigation.visited)
            assertEquals("comparison", box.state.value.started?.id)
        }

    @Test
    fun `a refused or unpublished comparison keeps the words and never opens a workspace`() =
        runTest(dispatcher) {
            for (failure in listOf(FailureReason.CORE_UNAVAILABLE, null)) {
                val tasks = RecordingTaskRepository().apply {
                    if (failure != null) startResult = TaffyResult.Failure(failure)
                }
                val navigator = RecordingNavigation()
                val box = comparisonBox(tasks, opened("compare these phones", listOf(docs.id, shop.id)))
                runCurrent()
                box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
                runCurrent()
                assertTrue(navigator.visited.isEmpty())
                if (failure == null) {
                    assertTrue(box.state.value.starting)
                    advanceTimeBy(5_001)
                    runCurrent()
                }
                assertEquals(
                    if (failure == null) TaskStartFailure.DEADLINE_EXCEEDED else TaskStartFailure.CORE_UNAVAILABLE,
                    box.state.value.startFailure,
                )
                assertFalse(box.state.value.starting)
                assertNull(box.state.value.started)
                assertEquals("compare these phones", box.state.value.input)
                assertTrue(navigator.visited.isEmpty())
                assertTrue(navigator.replaced.isEmpty())
                assertNull(tasks.followed)
            }
        }

    @Test
    fun `the next question on a finished task is a follow-up and not a start`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val box = AddressBarViewModel(
                StartTestBrowser(tabs = listOf(docs), resolve = ::asks),
                NoEvents,
                opened("what is this page for?"),
                tasks,
                FixedReadiness(),
            )
            runCurrent()
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()
            tasks.publish(startedTask("task-1", "what is this page for?", TaskTemplate.SUMMARIZE_EVIDENCE))
            runCurrent()

            // While the task works, the next question waits: the start rule
            // says a task is already running.
            box.onIntent(AddressBarIntent.InputChanged("and the fee?"), navigator)
            assertEquals(
                TaskStartDecision.Refused(TaskStartRefusal.ALREADY_RUNNING),
                box.state.value.start,
            )

            tasks.publish(
                startedTask("task-1", "what is this page for?", TaskTemplate.SUMMARIZE_EVIDENCE, TaskPhase.COMPLETED),
            )
            runCurrent()
            assertTrue(box.state.value.start is TaskStartDecision.Start)

            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()

            assertEquals(listOf(RecordingTaskRepository.FollowUp("task-1", "and the fee?")), tasks.followUps)
            assertEquals(1, tasks.starts.size)
            val state = box.state.value
            assertEquals("task-1", state.started?.id)
            assertEquals("", state.input)
            assertFalse(state.starting)
            assertNull(state.startFailure)
        }

    @Test
    fun `a conversation the core no longer holds is started afresh with the same words`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository().apply {
                followUpResult = TaffyResult.Failure(FailureReason.INVALID_REQUEST)
            }
            val box = finished(tasks)

            box.onIntent(AddressBarIntent.InputChanged("and the fee?"), RecordingNavigation())
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), RecordingNavigation())
            runCurrent()

            assertEquals(1, tasks.followUps.size)
            assertEquals(2, tasks.starts.size)
            assertEquals("and the fee?", tasks.starts.last().goal)
            tasks.publish(startedTask("task-2", "and the fee?", TaskTemplate.SUMMARIZE_EVIDENCE))
            runCurrent()
            assertEquals("task-2", box.state.value.started?.id)
        }

    @Test
    fun `any other refusal of a follow-up is said under the box`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository().apply {
                followUpResult = TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
            }
            val box = finished(tasks)

            box.onIntent(AddressBarIntent.InputChanged("and the fee?"), RecordingNavigation())
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), RecordingNavigation())
            runCurrent()

            assertEquals(1, tasks.starts.size)
            assertEquals(TaskStartFailure.CORE_UNAVAILABLE, box.state.value.startFailure)
            assertEquals("and the fee?", box.state.value.input)
            assertEquals("task-1", box.state.value.started?.id)
        }

    @Test
    fun `a failed task is tried again from its panel as a fresh start of the goal`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val box = finished(tasks, phase = TaskPhase.FAILED)

            box.onIntent(AddressBarIntent.TryAgain, RecordingNavigation())
            runCurrent()

            assertTrue(tasks.followUps.isEmpty())
            assertEquals(2, tasks.starts.size)
            assertEquals("what is this page for?", tasks.starts.last().goal)
        }

    @Test
    fun `an errand from the overlay follows Taffy to its tab`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val navigator = RecordingNavigation()
            val browser = StartTestBrowser(tabs = listOf(docs), resolve = ::asks)
            val box = AddressBarViewModel(browser, NoEvents, opened("download my aadhaar"), tasks, FixedReadiness())
            runCurrent()
            box.onIntent(AddressBarIntent.RemovePage(docs.id), navigator)
            box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), navigator)
            runCurrent()
            assertEquals(TaskTemplate.WEB_ERRAND, tasks.starts.single().template)

            tasks.publish(startedTask("task-1", "download my aadhaar"))
            runCurrent()
            browser.tabsBecome(listOf(docs, Tab(TabId("taffy"), "", "uidai.gov.in", isTaffyTab = true)))
            runCurrent()

            assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
            assertTrue(navigator.visited.isEmpty())
        }

    @Test
    fun `a page that closes stays on the question in caution and the row refuses`() =
        runTest(dispatcher) {
            val browser = StartTestBrowser(tabs = listOf(docs, shop), resolve = ::asks)
            val box = AddressBarViewModel(
                browser,
                NoEvents,
                opened("compare", attached = listOf(docs.id, shop.id)),
                RecordingTaskRepository(),
                FixedReadiness(),
            )
            runCurrent()

            browser.tabsBecome(listOf(docs))
            runCurrent()

            val pages = box.state.value.attachedPages
            assertEquals(listOf(false, true), pages.map { it.closed })
            assertEquals("Product listing", pages[1].title)
            assertEquals(TaskStartDecision.Refused(TaskStartRefusal.PAGE_CLOSED), box.state.value.start)
        }

    @Test
    fun `the not-ready row asks the core to start again`() =
        runTest(dispatcher) {
            val tasks = RecordingTaskRepository()
            val box = AddressBarViewModel(
                StartTestBrowser(tabs = listOf(docs), resolve = ::asks),
                NoEvents,
                opened("q"),
                tasks,
                FixedReadiness(),
            )
            runCurrent()

            box.onIntent(AddressBarIntent.RetryCore, RecordingNavigation())
            runCurrent()

            assertEquals(1, tasks.coreRetries)
        }

    private fun comparisonBox(tasks: RecordingTaskRepository, handle: SavedStateHandle) =
        AddressBarViewModel(
            StartTestBrowser(tabs = listOf(docs, shop), resolve = ::asks), NoEvents,
            handle.apply { set(TaffyDestination.SHAPE, TaskTemplate.COMPARE_PRODUCTS.label) },
            tasks, FixedReadiness(),
        )

    /** A box whose first question the core has finished answering. */
    private fun kotlinx.coroutines.test.TestScope.finished(
        tasks: RecordingTaskRepository,
        phase: TaskPhase = TaskPhase.COMPLETED,
    ): AddressBarViewModel {
        val box = AddressBarViewModel(
            StartTestBrowser(tabs = listOf(docs), resolve = ::asks),
            NoEvents,
            opened("what is this page for?"),
            tasks,
            FixedReadiness(),
        )
        runCurrent()
        box.onIntent(AddressBarIntent.Choose(checkNotNull(box.state.value.reading)), RecordingNavigation())
        runCurrent()
        tasks.publish(startedTask("task-1", "what is this page for?", TaskTemplate.SUMMARIZE_EVIDENCE, phase))
        runCurrent()
        return box
    }

    /** The handle as the overlay's destination fills it (`TaffyDestination.AssistantBar.arguments`). */
    private fun opened(question: String, attached: List<TabId>? = null): SavedStateHandle =
        SavedStateHandle(
            buildMap {
                put(TaffyDestination.QUESTION, question)
                if (attached != null) {
                    put(TaffyDestination.ATTACHED_TAB_IDS, attached.joinToString(",") { it.value })
                }
            },
        )

    private fun asks(input: String): AddressBarInterpretation = AddressBarInterpretation.AskTaffy(input)

    private val docs = Tab(TabId("tab_docs"), "Retention policy", "docs.example.test", isSelected = true)
    private val shop = Tab(TabId("tab_shop"), "Product listing", "shop.example.test")
}

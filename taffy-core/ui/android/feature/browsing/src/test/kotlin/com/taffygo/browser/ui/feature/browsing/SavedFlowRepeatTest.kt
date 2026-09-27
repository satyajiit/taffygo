// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.UnavailableVoiceInput
import java.lang.reflect.Proxy
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import kotlinx.coroutines.withContext
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import taffy.core_api.SavedFlowQueryAvailability
import taffy.core_api.SavedFlowQueryResult

@OptIn(ExperimentalCoroutinesApi::class)
class SavedFlowRepeatTest {
    private val dispatcher = StandardTestDispatcher()
    private val tab = Tab(TabId("manual"), "", "", hasBeenNowhere = true, isSelected = true)
    private val browser = StartTestBrowser(listOf(tab)) { AddressBarInterpretation.TaskForTaffy(it, TaskTemplate.WEB_ERRAND) }
    private val status = MutableStateFlow(pageFlowStatus())
    private val submitted = mutableListOf<String>()
    private val opens = mutableListOf<String>()
    private var answer: suspend (String, String) -> SavedFlowQueryResult = { request, _ -> result(request) }
    private var open: suspend () -> Unit = {}
    private val unused = Proxy.newProxyInstance(CoreApiClient::class.java.classLoader, arrayOf(CoreApiClient::class.java)) {
        _, method, _ -> error("Unexpected core call: ${method.name}")
    } as CoreApiClient
    private val core = object : CoreApiClient by unused {
        override val status = this@SavedFlowRepeatTest.status
        override suspend fun findSavedFlows(requestId: String, goal: String): SavedFlowQueryResult {
            submitted += goal
            return answer(requestId, goal)
        }
        override suspend fun openSavedFlowStart(requestId: String, skillId: String, expectedVersion: UInt) {
            opens += skillId
            open()
        }
    }

    @Before fun setUp() = Dispatchers.setMain(dispatcher)
    @After fun tearDown() = Dispatchers.resetMain()

    @Test fun `submission without a model shows complete review and open waits for commit before requesting fresh offers`() = runTest(dispatcher) {
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        val pages = SavedFlowPageRequests()
        val tasks = RecordingTaskRepository()
        val navigation = RecordingNavigation()
        val model = AddressBarViewModel(browser, NoEvents, SavedStateHandle(), tasks,
            FixedReadiness(TaffyReadiness.NotSetUp), UnavailableVoiceInput, repository, pages)
        runCurrent()
        model.onIntent(AddressBarIntent.InputChanged("download my document"), navigation)
        runCurrent()
        assertTrue(submitted.isEmpty())
        model.onIntent(AddressBarIntent.Choose(requireNotNull(model.state.value.reading)), navigation)
        runCurrent()
        val review = model.state.value.savedFlows.reviews.single()
        assertEquals(1, review.steps.size)
        assertTrue(tasks.starts.isEmpty())
        assertTrue(navigation.visited.isEmpty())
        val committed = CompletableDeferred<Unit>()
        open = { committed.await() }
        model.onIntent(AddressBarIntent.OpenSavedFlow(review), navigation)
        runCurrent()
        assertNull(pages.state.value)
        assertTrue(navigation.replaced.isEmpty())
        committed.complete(Unit)
        runCurrent()
        assertEquals(review, pages.state.value?.review)
        assertEquals("download my document", pages.state.value?.goal)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigation.replaced)
        assertTrue(tasks.starts.isEmpty())
    }

    @Test fun `available no match continues ordinary model setup and private tabs never query`() = runTest(dispatcher) {
        answer = { request, _ -> result(request).copy(flows = emptyList()) }
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        val navigation = RecordingNavigation()
        val model = AddressBarViewModel(browser, NoEvents, SavedStateHandle(), RecordingTaskRepository(),
            FixedReadiness(TaffyReadiness.NotSetUp), UnavailableVoiceInput, repository)
        runCurrent()
        model.onIntent(AddressBarIntent.InputChanged("new task"), navigation)
        model.onIntent(AddressBarIntent.Choose(requireNotNull(model.state.value.reading)), navigation)
        runCurrent()
        assertEquals(listOf(TaffyDestination.AiAndProviders), navigation.visited)
        browser.tabsBecome(listOf(tab.copy(isPrivate = true)))
        runCurrent()
        model.onIntent(AddressBarIntent.InputChanged("private task"), navigation)
        model.onIntent(AddressBarIntent.Choose(requireNotNull(model.state.value.reading)), navigation)
        runCurrent()
        assertEquals(listOf("new task"), submitted)
    }

    @Test fun `editing while an uncancellable old reply arrives never restores or opens that review`() = runTest(dispatcher) {
        val waiting = CompletableDeferred<Unit>()
        answer = { request, _ -> withContext(NonCancellable) { waiting.await(); result(request) } }
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        val controller = SavedFlowRepeatController(browser, repository, SavedFlowPageRequests(), backgroundScope)
        var misses = 0
        controller.find("old request") { misses++ }
        runCurrent()
        controller.clear()
        waiting.complete(Unit)
        runCurrent()
        assertFalse(controller.state.value.visible)
        controller.open(requireNotNull(pageFlowSkill().toSavedFlowReview()), RecordingNavigation())
        runCurrent()
        assertTrue(opens.isEmpty())
        assertEquals(0, misses)
    }

    private fun result(request: String) = SavedFlowQueryResult(
        request, 1uL, SavedFlowQueryAvailability.AVAILABLE, listOf(pageFlowSkill()),
    )
}

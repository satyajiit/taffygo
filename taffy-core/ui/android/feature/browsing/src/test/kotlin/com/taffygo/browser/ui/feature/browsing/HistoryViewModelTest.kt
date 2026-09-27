// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import java.time.ZoneOffset
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Screen SCR-201's commands: open, delete, Clear, and the honest empty store. */
class HistoryViewModelTest {

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
    fun `an empty store is empty, not a list of invented visits`() = runTest(dispatcher) {
        val viewModel = viewModel(history = EmptyHistoryRepository())

        assertTrue(viewModel.state.value.isEmpty)
        assertEquals(0, viewModel.state.value.totalCount)
        assertTrue(viewModel.state.value.days.isEmpty())
        assertTrue(viewModel.state.value.canOpen)
    }

    @Test
    fun `unavailable history does not open a tab`() = runTest(dispatcher) {
        val browser = PagesTestBrowser()
        val navigator = PagesTestNavigator()
        val viewModel = viewModel(
            history = UnavailableHistoryRepository(),
            browser = browser,
        )

        viewModel.onIntent(HistoryIntent.Open(HistoryVisit.Id("hv_1")), navigator)
        runCurrent()

        assertTrue(viewModel.state.value.isUnavailable)
        assertTrue(browser.opened.isEmpty())
        assertTrue(navigator.visited.isEmpty())
    }

    @Test
    fun `clear opens the clear-browsing-data screen`() = runTest(dispatcher) {
        val navigator = PagesTestNavigator()
        val viewModel = viewModel()

        viewModel.onIntent(HistoryIntent.Clear, navigator)

        assertEquals(listOf(TaffyDestination.ClearBrowsingData), navigator.visited)
        assertTrue(navigator.replaced.isEmpty())
    }

    @Test
    fun `dismiss leaves the screen`() = runTest(dispatcher) {
        val navigator = PagesTestNavigator()
        val viewModel = viewModel()

        viewModel.onIntent(HistoryIntent.Dismiss, navigator)

        assertEquals(1, navigator.backs)
    }

    @Test
    fun `open delegates the opaque visit and leaves History behind`() = runTest(dispatcher) {
        val browser = PagesTestBrowser()
        val navigator = PagesTestNavigator()
        val history = MutableHistoryRepository(
            HistoryVisit(
                HistoryVisit.Id("hv_1"),
                "Retention policy",
                "docs.example.test",
                visitedAtEpochMillis = 1L,
            ),
        )
        val viewModel = viewModel(history = history, browser = browser)

        viewModel.onIntent(HistoryIntent.Open(HistoryVisit.Id("hv_1")), navigator)
        runCurrent()

        assertEquals(listOf(HistoryVisit.Id("hv_1")), history.opened)
        assertTrue(browser.opened.isEmpty())
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
    }

    @Test
    fun `delete removes the visit the person confirmed`() = runTest(dispatcher) {
        val history = MutableHistoryRepository(
            HistoryVisit(
                HistoryVisit.Id("hv_1"),
                "Retention policy",
                "docs.example.test",
                visitedAtEpochMillis = 1L,
            ),
        )
        val viewModel = viewModel(history = history)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(HistoryIntent.Delete(HistoryVisit.Id("hv_1")), PagesTestNavigator())
        runCurrent()

        assertEquals(listOf(HistoryVisit.Id("hv_1")), history.deleted)
        assertTrue(viewModel.state.value.isEmpty)
    }

    @Test
    fun `private visits are never asked of the favicon store`() = runTest(dispatcher) {
        val browser = PagesTestBrowser()
        viewModel(
            history = MutableHistoryRepository(
                HistoryVisit(
                    HistoryVisit.Id("hv_private"),
                    "Price history",
                    "prices.example.test",
                    visitedAtEpochMillis = 1L,
                    isPrivate = true,
                ),
                HistoryVisit(
                    HistoryVisit.Id("hv_person"),
                    "Retention policy",
                    "docs.example.test",
                    visitedAtEpochMillis = 2L,
                ),
            ),
            browser = browser,
        )
        runCurrent()

        assertEquals(listOf(listOf("docs.example.test")), browser.markRequests)
    }

    @Test
    fun `onShown records the catalog identifier`() = runTest(dispatcher) {
        val analytics = PagesTestAnalytics()
        val viewModel = viewModel(analytics = analytics)

        viewModel.onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.History.screenId)),
            analytics.events,
        )
    }

    private fun viewModel(
        history: HistoryRepository = EmptyHistoryRepository(),
        browser: BrowserRepository = PagesTestBrowser(),
        analytics: AnalyticsClient = PagesTestAnalytics(),
    ) = HistoryViewModel(
        history = history,
        browser = browser,
        analytics = analytics,
        savedState = SavedStateHandle(),
        nowEpochMillis = { 1_718_452_800_000L },
        zone = ZoneOffset.UTC,
    )

    private class MutableHistoryRepository(vararg visits: HistoryVisit) : HistoryRepository {
        val opened = mutableListOf<HistoryVisit.Id>()
        val deleted = mutableListOf<HistoryVisit.Id>()
        private val state = MutableStateFlow<HistorySnapshot>(
            HistorySnapshot.Ready(visits.toList()),
        )
        override val snapshot: StateFlow<HistorySnapshot> = state

        override suspend fun open(id: HistoryVisit.Id): Boolean {
            val ready = state.value as? HistorySnapshot.Ready ?: return false
            if (ready.visits.none { it.id == id }) return false
            opened += id
            return true
        }

        override suspend fun delete(id: HistoryVisit.Id) {
            deleted += id
            val ready = state.value as? HistorySnapshot.Ready ?: return
            state.value = HistorySnapshot.Ready(ready.visits.filterNot { it.id == id })
        }
    }
}

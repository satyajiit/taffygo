// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Overflow destinations, find overlay, save sheet, and system back. */
class BrowserMainOverflowViewModelTest {

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
    fun `history bookmarks library and you are pushed and the menu closes`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = browserMainViewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenMore, navigator)
        viewModel.onIntent(BrowserMainIntent.OpenHistory, navigator)
        viewModel.onIntent(BrowserMainIntent.OpenBookmarks, navigator)
        viewModel.onIntent(BrowserMainIntent.OpenLibrary, navigator)
        viewModel.onIntent(BrowserMainIntent.OpenYou, navigator)
        runCurrent()

        assertEquals(
            listOf(
                TaffyDestination.History,
                TaffyDestination.Bookmarks,
                TaffyDestination.LibraryHome,
                TaffyDestination.You,
            ),
            navigator.visited,
        )
        assertFalse(viewModel.state.value.moreOpen)
    }

    @Test
    fun `find opens as chrome, not as a destination`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = browserMainViewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenMore, navigator)
        viewModel.onIntent(BrowserMainIntent.OpenFindInPage, navigator)
        runCurrent()

        assertTrue(viewModel.state.value.findInPage.open)
        assertEquals("", viewModel.state.value.findInPage.query)
        assertFalse(viewModel.state.value.findInPage.available)
        assertFalse(viewModel.state.value.moreOpen)
        assertEquals(emptyList<TaffyDestination>(), navigator.visited)
    }

    @Test
    fun `closing find forgets the query`() = runTest(dispatcher) {
        val viewModel = browserMainViewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenFindInPage, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.FindQueryChanged("policy"), NoNavigation())
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.DismissFindInPage, NoNavigation())
        runCurrent()

        assertFalse(viewModel.state.value.findInPage.open)
        assertEquals("", viewModel.state.value.findInPage.query)
        assertEquals(0, viewModel.state.value.findInPage.matchCount)
    }

    @Test
    fun `typing is immediate and an older answer cannot replace the new query`() =
        runTest(dispatcher) {
            val find = ControlledFindInPagePort()
            val viewModel = browserMainViewModel(find = find)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.OpenFindInPage, NoNavigation())
            viewModel.onIntent(BrowserMainIntent.FindQueryChanged("first"), NoNavigation())
            advanceTimeBy(100)
            runCurrent()
            viewModel.onIntent(BrowserMainIntent.FindQueryChanged("second"), NoNavigation())
            advanceTimeBy(100)
            runCurrent()

            assertEquals("second", viewModel.state.value.findInPage.query)
            assertEquals(0, viewModel.state.value.findInPage.matchCount)
            assertEquals(listOf("first"), find.cancelled)
            find.answer("first", FindInPagePort.MatchCount(1, 9), publish = false)
            runCurrent()
            assertEquals("second", viewModel.state.value.findInPage.query)
            assertEquals(0, viewModel.state.value.findInPage.matchCount)

            find.answer("second", FindInPagePort.MatchCount(2, 3), publish = true)
            runCurrent()
            assertEquals(2, viewModel.state.value.findInPage.activeIndex)
            assertEquals(3, viewModel.state.value.findInPage.matchCount)
        }

    @Test
    fun `rapid typing asks the renderer only for the settled query`() = runTest(dispatcher) {
        val find = ControlledFindInPagePort()
        val viewModel = browserMainViewModel(find = find)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenFindInPage, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.FindQueryChanged("p"), NoNavigation())
        viewModel.onIntent(BrowserMainIntent.FindQueryChanged("po"), NoNavigation())
        viewModel.onIntent(BrowserMainIntent.FindQueryChanged("policy"), NoNavigation())
        runCurrent()
        assertTrue(find.requested.isEmpty())

        advanceTimeBy(100)
        runCurrent()
        assertEquals(listOf("policy"), find.requested)
    }

    @Test
    fun `page invalidation clears a count and an empty query clears highlights`() =
        runTest(dispatcher) {
            val find = ControlledFindInPagePort()
            val viewModel = browserMainViewModel(find = find)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.OpenFindInPage, NoNavigation())
            viewModel.onIntent(BrowserMainIntent.FindQueryChanged("policy"), NoNavigation())
            advanceTimeBy(100)
            runCurrent()
            find.answer("policy", FindInPagePort.MatchCount(1, 4), publish = true)
            runCurrent()
            find.invalidate()
            runCurrent()
            assertEquals(0, viewModel.state.value.findInPage.matchCount)

            viewModel.onIntent(BrowserMainIntent.FindQueryChanged(""), NoNavigation())
            runCurrent()
            assertEquals(1, find.timesCleared)
            assertEquals("", viewModel.state.value.findInPage.query)
        }

    @Test
    fun `system back dismisses find before page history`() = runTest(dispatcher) {
        val browser = FakeBrowser(pageHistory = 2)
        val navigator = RecordingNavigator()
        val viewModel = browserMainViewModel(browser)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenFindInPage, navigator)
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.SystemBack, navigator)
        runCurrent()

        assertFalse(viewModel.state.value.findInPage.open)
        assertEquals(0, browser.timesWentBack)
        assertEquals(0, navigator.timesLeftToBackground)
    }

    @Test
    fun `save page is a sheet, not a destination`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = browserMainViewModel(
            FakeBrowser(
                tabs = listOf(
                    Tab(
                        TabId("tab_1"),
                        "Retention",
                        "docs.example.test",
                        isSelected = true,
                    ),
                ),
            ),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenSavePage, navigator)
        runCurrent()

        assertTrue(viewModel.state.value.savePageOpen)
        assertFalse(viewModel.state.value.savePage.primaryEnabled)
        assertEquals(emptyList<TaffyDestination>(), navigator.visited)

        viewModel.onIntent(BrowserMainIntent.DismissSavePage, navigator)
        runCurrent()

        assertFalse(viewModel.state.value.savePageOpen)
    }

    @Test
    fun `a failed bookmark keeps the exact page sheet open for retry`() = runTest(dispatcher) {
        val address = "https://docs.example.test/policies/retention?region=in#exceptions"
        val writer = ControlledBookmarksWriter()
        val viewModel = browserMainViewModel(
            browser = FakeBrowser(
                tabs = listOf(
                    Tab(
                        TabId("tab_1"),
                        "Retention policy",
                        "docs.example.test",
                        isSelected = true,
                    ),
                ),
                title = "Retention policy",
                canonicalUrl = address,
            ),
            writer = writer,
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenSavePage, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.ConfirmSavePage, NoNavigation())
        runCurrent()

        assertEquals(address, writer.requests.single().address)
        assertEquals(SavePageUiState.SaveStatus.SAVING, viewModel.state.value.savePage.saveStatus)
        assertTrue(viewModel.state.value.savePageOpen)

        writer.answer(0, saved = false)
        runCurrent()

        assertEquals(SavePageUiState.SaveStatus.FAILED, viewModel.state.value.savePage.saveStatus)
        assertTrue(viewModel.state.value.savePage.primaryEnabled)
        assertTrue(viewModel.state.value.savePageOpen)
    }

    @Test
    fun `an older bookmark completion cannot close a reopened sheet`() = runTest(dispatcher) {
        val writer = ControlledBookmarksWriter()
        val browser = FakeBrowser(
            tabs = listOf(
                Tab(
                    TabId("tab_1"),
                    "Retention policy",
                    "docs.example.test",
                    isSelected = true,
                ),
            ),
            canonicalUrl = "https://docs.example.test/policies/retention",
        )
        val viewModel = browserMainViewModel(browser = browser, writer = writer)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenSavePage, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.ConfirmSavePage, NoNavigation())
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.DismissSavePage, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.OpenSavePage, NoNavigation())
        runCurrent()

        writer.answer(0, saved = true)
        runCurrent()

        assertTrue(viewModel.state.value.savePageOpen)
        assertEquals(SavePageUiState.SaveStatus.IDLE, viewModel.state.value.savePage.saveStatus)
    }

    @Test
    fun `a successful bookmark closes only after the writer answers`() = runTest(dispatcher) {
        val writer = ControlledBookmarksWriter()
        val viewModel = browserMainViewModel(
            browser = FakeBrowser(
                tabs = listOf(
                    Tab(
                        TabId("tab_1"),
                        "Retention policy",
                        "docs.example.test",
                        isSelected = true,
                    ),
                ),
            ),
            writer = writer,
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenSavePage, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.ConfirmSavePage, NoNavigation())
        runCurrent()
        assertTrue(viewModel.state.value.savePageOpen)

        writer.answer(0, saved = true)
        runCurrent()

        assertFalse(viewModel.state.value.savePageOpen)
    }

    @Test
    fun `share with no host does not navigate`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = browserMainViewModel(FakeBrowser(host = "", title = ""))
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.SharePage, navigator)
        runCurrent()

        assertEquals(emptyList<TaffyDestination>(), navigator.visited)
        assertFalse(viewModel.state.value.moreOpen)
    }

    @Test
    fun `ads tile opens the site sheet and closes the menu`() = runTest(dispatcher) {
        val viewModel = browserMainViewModel()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenMore, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.OpenSiteFiltering, NoNavigation())
        runCurrent()

        assertTrue(viewModel.state.value.siteFilteringOpen)
        assertFalse(viewModel.state.value.moreOpen)
    }

    @Test
    fun `stop loading reaches the browser and closes the menu`() = runTest(dispatcher) {
        val browser = FakeBrowser()
        val viewModel = browserMainViewModel(browser)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.OpenMore, NoNavigation())
        viewModel.onIntent(BrowserMainIntent.StopLoading, NoNavigation())
        runCurrent()

        assertEquals(1, browser.timesStoppedLoading)
        assertFalse(viewModel.state.value.moreOpen)
    }
}

private class ControlledFindInPagePort : FindInPagePort {
    private val pending = mutableMapOf<String, CompletableDeferred<FindInPagePort.MatchCount>>()
    private val matchState = MutableStateFlow(FindInPagePort.MatchCount())
    override val isAvailable: Boolean = true
    override val matches: StateFlow<FindInPagePort.MatchCount> = matchState
    var timesCleared = 0
    val requested = mutableListOf<String>()
    val cancelled = mutableListOf<String>()

    override suspend fun find(query: String): FindInPagePort.MatchCount {
        requested += query
        val result = pending.getOrPut(query, ::CompletableDeferred)
        return try {
            result.await()
        } finally {
            if (!result.isCompleted) cancelled += query
        }
    }

    override suspend fun next(): FindInPagePort.MatchCount = matchState.value
    override suspend fun previous(): FindInPagePort.MatchCount = matchState.value

    override suspend fun clear() {
        timesCleared += 1
        invalidate()
    }

    fun answer(query: String, answer: FindInPagePort.MatchCount, publish: Boolean) {
        if (publish) matchState.value = answer
        pending.getValue(query).complete(answer)
    }

    fun invalidate() {
        matchState.value = FindInPagePort.MatchCount()
    }
}

private class ControlledBookmarksWriter : BookmarksWriter {
    override val isAvailable: Boolean = true
    val requests = mutableListOf<Request>()
    private val answers = mutableListOf<CompletableDeferred<Boolean>>()

    override suspend fun save(title: String, address: String, folderId: String): Boolean {
        val answer = CompletableDeferred<Boolean>()
        requests += Request(title, address, folderId)
        answers += answer
        return answer.await()
    }

    fun answer(index: Int, saved: Boolean) {
        answers[index].complete(saved)
    }

    data class Request(
        val title: String,
        val address: String,
        val folderId: String,
    )
}

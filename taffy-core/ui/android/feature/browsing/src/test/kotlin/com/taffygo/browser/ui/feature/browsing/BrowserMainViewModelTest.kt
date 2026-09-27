// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
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

/**
 * Screen SCR-101 and the refusal it has to carry.
 *
 * The point of this suite is the *seam*, not the projection: a search is
 * refused on screen SCR-103, which then closes, so the words about it have to
 * survive the screen that caused them and be waiting on this one. That crossing
 * is the part a reducer test cannot see, and it is exactly where the defect
 * lived — the refusal happened and nothing carried it anywhere.
 */
class BrowserMainViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        // `viewModelScope` and `stateIn` are both bound to the main dispatcher,
        // which a JVM test has to supply before a view model may exist.
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `a refusal raised while another screen was open is waiting on this one`() = runTest(
        dispatcher,
    ) {
        val browser = FakeBrowser()
        // What the address bar did before it closed: committed a search, which
        // sends nothing anywhere because no search engine has been chosen.
        browser.refuse(BrowserNotice.NO_SEARCH_ENGINE)

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertEquals(BrowserNotice.NO_SEARCH_ENGINE, viewModel.state.value.notice)
        // The page is untouched. A refusal is not a page failure.
        assertNull(viewModel.state.value.failure)
    }

    @Test
    fun `the screen shows nothing when nothing was refused`() = runTest(dispatcher) {
        val viewModel = testBrowserMainViewModel(
            FakeBrowser(),
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertNull(viewModel.state.value.notice)
    }

    @Test
    fun `reading the notice clears it, and clears it where it was raised`() = runTest(dispatcher) {
        val browser = FakeBrowser()
        browser.refuse(BrowserNotice.NO_SEARCH_ENGINE)

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.DismissNotice, NoNavigation())
        runCurrent()

        assertNull(viewModel.state.value.notice)
        // Cleared in the repository rather than in a copy held here: the notice
        // outlives this screen, so a screen-local flag would put it back the
        // next time somebody opened SCR-101.
        assertTrue(browser.dismissed)
        assertNull(browser.notice.value)
    }

    // -----------------------------------------------------------------------
    // What the system back button means here.
    //
    // Two stacks sit under screen SCR-101 — the tab's page history and the
    // shell's navigation stack — and a browser answers the first of them
    // first. It used to answer neither: nothing on this screen claimed the
    // gesture, so back popped whatever navigation entries the address bar had
    // left behind and a person who had followed one link found the address bar
    // instead of the page they came from.
    // -----------------------------------------------------------------------

    @Test
    fun `back takes the page before this one while the tab has history`() = runTest(dispatcher) {
        val browser = FakeBrowser(pageHistory = 2)
        val navigator = RecordingNavigator()

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        viewModel.onIntent(BrowserMainIntent.SystemBack, navigator)
        runCurrent()

        assertEquals(1, browser.timesWentBack)
        // The screen is still the screen. Nothing was left, because the page
        // had somewhere to go and that is where the press went.
        assertEquals(0, navigator.timesWentBack)
    }

    @Test
    fun `back leaves the app only once the page has no history left`() = runTest(dispatcher) {
        val browser = FakeBrowser(pageHistory = 1)
        val navigator = RecordingNavigator()
        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )

        viewModel.onIntent(BrowserMainIntent.SystemBack, navigator)
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.SystemBack, navigator)
        runCurrent()

        // The first press was the page's; the second found nothing there and
        // put the window behind the last app rather than walking through
        // chrome.
        assertEquals(2, browser.timesWentBack)
        assertEquals(0, navigator.timesWentBack)
        assertEquals(1, navigator.timesLeftToBackground)
    }

    /**
     * The fall-through is decided by the browser's answer and not by this
     * screen's copy of it.
     *
     * `BrowserMediator.goBack` returns false for exactly one reason — there
     * was no history entry to consume — and asking it is what keeps the
     * gesture correct in the frame between a tab moving and this screen's
     * projection catching up.
     */
    @Test
    fun `a page that says it went nowhere puts the window behind the last app`() =
        runTest(dispatcher) {
            val browser = FakeBrowser(pageHistory = 0)
            val navigator = RecordingNavigator()

            val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
            viewModel.onIntent(BrowserMainIntent.SystemBack, navigator)
            runCurrent()

            assertEquals(1, browser.timesWentBack)
            assertEquals(0, navigator.timesWentBack)
            assertEquals(1, navigator.timesLeftToBackground)
        }

    /**
     * The control in the action row is not the system gesture and must not
     * become it: it is drawn only while there is page history, so it has one
     * meaning, and a browser whose back *button* could close the screen would
     * be a browser that quit when a page finished loading at the wrong moment.
     */
    @Test
    fun `the action row's back control never leaves the screen`() = runTest(dispatcher) {
        val browser = FakeBrowser(pageHistory = 0)
        val navigator = RecordingNavigator()

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        viewModel.onIntent(BrowserMainIntent.GoBack, navigator)
        runCurrent()

        assertEquals(1, browser.timesWentBack)
        assertEquals(0, navigator.timesWentBack)
    }

    @Test
    fun `the action row's forward control asks only the browser`() = runTest(dispatcher) {
        val browser = FakeBrowser()
        val navigator = RecordingNavigator()

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        viewModel.onIntent(BrowserMainIntent.GoForward, navigator)
        runCurrent()

        assertEquals(1, browser.timesWentForward)
        assertEquals(0, navigator.timesWentBack)
    }

    // -----------------------------------------------------------------------
    // Which tab this is.
    //
    // A private tab looked exactly like every other one on this screen: the
    // state carried no answer, so SCR-101 could not have drawn one. A person
    // who opened a private tab, backgrounded the phone and came back had no way
    // to know which promise the address bar in front of them was keeping.
    // -----------------------------------------------------------------------

    @Test
    fun `the selected tab being private reaches the screen`() = runTest(dispatcher) {
        val browser = FakeBrowser(
            tabs = listOf(
                Tab(TabId("tab_1"), "Price history", "prices.example.test"),
                Tab(TabId("tab_5"), "Retention policy", "docs.example.test", isPrivate = true, isSelected = true),
            ),
        )

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertTrue(viewModel.state.value.isPrivate)
    }

    @Test
    fun `a private tab open elsewhere does not make this one private`() = runTest(dispatcher) {
        val browser = FakeBrowser(
            tabs = listOf(
                Tab(TabId("tab_1"), "Retention policy", "docs.example.test", isSelected = true),
                Tab(TabId("tab_5"), "Price history", "prices.example.test", isPrivate = true),
            ),
        )

        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertFalse(viewModel.state.value.isPrivate)
    }

    // -----------------------------------------------------------------------
    // The page-snapshot instrument.
    //
    // `DIAGNOSTIC-PAGE-INTELLIGENCE` is the one destination with no catalog
    // identifier, and its row shipped in the browser's own overflow menu with
    // a seam behind it that refuses every call. The row is the seam's answer
    // now, and so is the navigation.
    // -----------------------------------------------------------------------

    @Test
    fun `the workspace list is pushed over the tab that asked for it`() = runTest(dispatcher) {
        val browser = FakeBrowser()
        val navigator = RecordingNavigator()
        val viewModel = testBrowserMainViewModel(
            browser,
            NoAnalytics(),
            FakeTaffyParts(),
            NoFrequentSites(),
        )

        viewModel.onIntent(BrowserMainIntent.OpenWorkspaces, navigator)
        runCurrent()

        assertEquals(listOf(TaffyDestination.WorkspaceList), navigator.visited)
    }
}

/** Tests choose their closed ports explicitly; production construction cannot. */
private fun testBrowserMainViewModel(
    browser: FakeBrowser,
    analytics: NoAnalytics,
    parts: FakeTaffyParts,
    frequentSites: NoFrequentSites,
): BrowserMainViewModel = BrowserMainViewModel(
    browser,
    analytics,
    parts,
    frequentSites,
    EmptyFindInPagePort(),
    EmptyBookmarksWriter(),
    EmptySiteInfoRepository(),
    EmptyPageZoomRepository(),
)

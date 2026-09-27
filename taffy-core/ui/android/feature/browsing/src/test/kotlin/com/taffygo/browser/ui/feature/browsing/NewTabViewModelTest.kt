// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
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

/**
 * Screen SCR-102's three ways forward, and the one that used to lose the tab.
 *
 * The defect this fixes was invisible to every reducer test in this feature,
 * because nothing was wrong with any projection: the tab switcher offered "new
 * tab", SCR-102 offered its address entry, SCR-103 read the typed text
 * correctly and the browser navigated correctly — the current tab. Nothing in
 * between remembered that a *new* tab had been asked for, so a person could not
 * open a second tab through the interface at all.
 *
 * So these are view-model tests rather than reducer tests: the missing step was
 * a command, and a command is only visible where commands are recorded.
 */
class NewTabViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        // `viewModelScope` is bound to the main dispatcher, which a JVM test
        // has to supply before a view model may be constructed at all.
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `putting the caret in the box opens a tab and changes no screen`() = runTest(dispatcher) {
        val browser = RecordingBrowserRepository()
        val navigator = RecordingNavigator()
        val viewModel = NewTabViewModel(browser, NoAnalytics(), readyPythonParts(), NoFrequentSites())

        viewModel.onIntent(NewTabIntent.ComposerFocused, navigator)
        runCurrent()

        // One tab, opened on no address, because the person has not said one
        // yet. Without this the box commits into the tab that was already
        // showing and the count never moves.
        assertEquals(listOf("" to false), browser.opened)
        // And nowhere to go: the box the person is typing into is the one in
        // front of them (decision 0131), so the screen they are on is the
        // screen they stay on.
        assertEquals(emptyList<TaffyDestination>(), navigator.visited)
        assertEquals(emptyList<TaffyDestination>(), navigator.replaced)
    }

    @Test
    fun `the plus reaches the Library and opens no tab doing it`() = runTest(dispatcher) {
        val browser = RecordingBrowserRepository()
        val navigator = RecordingNavigator()
        val viewModel = NewTabViewModel(browser, NoAnalytics(), readyPythonParts(), NoFrequentSites())

        // History and Bookmarks are not here: the plus attaches them to the
        // request rather than opening them (decision 0133), which is the
        // box's own intent and never reaches this view model.
        viewModel.onIntent(NewTabIntent.OpenLibrary, navigator)
        runCurrent()

        // Pushed, so back returns to the chooser with the question still open,
        // and no tab opened on the way — the same rule the four dock slots
        // follow, for the same reason.
        assertEquals(listOf(TaffyDestination.LibraryHome), navigator.visited)
        assertEquals(emptyList<Pair<String, Boolean>>(), browser.opened)
    }

    @Test
    fun `a frequent site opens its own tab and goes to the page`() = runTest(dispatcher) {
        val browser = RecordingBrowserRepository()
        val navigator = RecordingNavigator()
        val viewModel = NewTabViewModel(browser, NoAnalytics(), readyPythonParts(), NoFrequentSites())

        viewModel.onIntent(NewTabIntent.OpenSite("en.wikipedia.org"), navigator)
        runCurrent()

        assertEquals(listOf("en.wikipedia.org" to false), browser.opened)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.visited)
        // And the chooser is left behind on the way, so back on the page that
        // arrives means the page and not the chooser.
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
    }

    /**
     * The chooser is a step towards a tab, never a place to come back to.
     *
     * The entries that end on a page used to push SCR-101 on top of SCR-102,
     * which left a pair on the stack per visit — the same arithmetic that made
     * the system back button walk through chrome instead of through pages.
     */
    @Test
    fun `every way out of the chooser onto a page leaves the chooser behind`() =
        runTest(dispatcher) {
            val onwards = listOf<NewTabIntent>(
                NewTabIntent.OpenSite("en.wikipedia.org"),
            )

            for (intent in onwards) {
                val navigator = RecordingNavigator()
                val viewModel = NewTabViewModel(
                    RecordingBrowserRepository(),
                    NoAnalytics(),
                    readyPythonParts(),
                    NoFrequentSites(),
                )

                viewModel.onIntent(intent, navigator)
                runCurrent()

                assertEquals(
                    intent.toString(),
                    listOf(TaffyDestination.BrowserMain),
                    navigator.replaced,
                )
            }
        }

    /**
     * The dock's four destinations are pushed, and no tab is opened: the
     * person has not answered "where does this tab go" yet, and back has to
     * return them to the chooser that is still asking.
     */
    @Test
    fun `each dock destination is pushed with the chooser left standing`() = runTest(dispatcher) {
        val cases = listOf(
            NewTabIntent.OpenTabSwitcher to TaffyDestination.TabSwitcher,
            NewTabIntent.OpenDownloads to TaffyDestination.Downloads,
            NewTabIntent.OpenWorkspaces to TaffyDestination.WorkspaceList,
            NewTabIntent.OpenSettings to TaffyDestination.SettingsHome,
        )

        for ((intent, destination) in cases) {
            val browser = RecordingBrowserRepository()
            val navigator = RecordingNavigator()
            val viewModel = NewTabViewModel(browser, NoAnalytics(), readyPythonParts(), NoFrequentSites())

            viewModel.onIntent(intent, navigator)
            runCurrent()

            assertEquals(intent.toString(), listOf(destination), navigator.visited)
            assertEquals(intent.toString(), emptyList<TaffyDestination>(), navigator.replaced)
            assertEquals(intent.toString(), emptyList<Pair<String, Boolean>>(), browser.opened)
        }
    }

    @Test
    fun `no entry point on this screen sends a device to a reserved test domain`() =
        runTest(dispatcher) {
            val browser = RecordingBrowserRepository()
            val navigator = RecordingNavigator()
            val viewModel = NewTabViewModel(browser, NoAnalytics(), readyPythonParts(), NoFrequentSites())

            for (intent in listOf(NewTabIntent.ComposerFocused, NewTabIntent.OpenTabSwitcher)) {
                viewModel.onIntent(intent, navigator)
            }
            runCurrent()

            // `.test` is reserved by RFC 6761 and resolves nowhere, so a fixture
            // host written into a screen becomes a name-resolution failure the
            // day a web engine is behind the seam. The only host this screen may
            // name is one that came from a tab the browser says is open, which
            // is what `OpenSite` carries and what these two do not.
            assertTrue(
                browser.opened.toString(),
                browser.opened.none { (host, _) -> host.endsWith(".test") },
            )
        }

    @Test
    fun `the box does nothing until the python library is installed`() = runTest(
        dispatcher,
    ) {
        val browser = RecordingBrowserRepository()
        val navigator = RecordingNavigator()
        val viewModel = NewTabViewModel(browser, NoAnalytics(), FakeTaffyParts(), NoFrequentSites())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(NewTabIntent.ComposerFocused, navigator)
        viewModel.onIntent(NewTabIntent.OpenSite("en.wikipedia.org"), navigator)
        runCurrent()

        assertEquals(emptyList<Pair<String, Boolean>>(), browser.opened)
        assertEquals(emptyList<TaffyDestination>(), navigator.visited)
    }

    // -----------------------------------------------------------------------
    // Doubles. Each records what it was asked to do and invents nothing.
    // -----------------------------------------------------------------------

    private class RecordingBrowserRepository : BrowserRepository {
        val opened = mutableListOf<Pair<String, Boolean>>()

        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
        override val navigation: StateFlow<NavigationState> =
            MutableStateFlow(NavigationState(host = "", title = ""))
        override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
        override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
        override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
        override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
        override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)

        /** Every host list this screen asked marks for, in call order. */
        val markRequests = mutableListOf<List<String>>()

        override suspend fun requestSiteMarks(hosts: Collection<String>) {
            markRequests += hosts.toList()
        }

        override fun resolve(input: String): AddressBarInterpretation =
            AddressBarInterpretation.GoTo(input, input)

        override fun suggestions(input: String): List<Suggestion> = emptyList()

        override suspend fun commit(interpretation: AddressBarInterpretation) = Unit

        override fun dismissNotice() = Unit

        override suspend fun selectTab(id: TabId) = Unit

        override suspend fun closeTab(id: TabId) = Unit

        override suspend fun openTab(host: String, isPrivate: Boolean): TabId {
            opened += host to isPrivate
            return TabId("tab_${opened.size}")
        }

        override suspend fun goBack(): Boolean = false

        override suspend fun goForward(): Boolean = false

        override suspend fun reload() = Unit

        override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false

        override val filtering: StateFlow<FilteringSettings> =
            MutableStateFlow(FilteringSettings())

        override suspend fun setFilteringEnabled(enabled: Boolean) = Unit

        override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = true

        override suspend fun flushFilteringCounts() = Unit
    }

    private class RecordingNavigator : TaffyNavigator {
        /** Everywhere this screen sent the person, however it sent them. */
        val visited = mutableListOf<TaffyDestination>()

        /**
         * The subset it sent them to *without* leaving SCR-102 behind to
         * return to. A chooser that has opened the tab it was asked for is
         * finished, and an entry for it on the stack is one more thing the
         * system back button has to walk through before it reaches the page.
         */
        val replaced = mutableListOf<TaffyDestination>()

        override fun goTo(destination: TaffyDestination) {
            visited += destination
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            visited += destination
            replaced += destination
        }

        override fun goBack(): Boolean = false

        override fun goHome() = Unit

        override fun restart(destination: TaffyDestination) = Unit

        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit

        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private class NoFrequentSites : FrequentSitesRepository {
        override val sites: StateFlow<List<FrequentSite>> = MutableStateFlow(emptyList())

        override suspend fun recordVisit(host: String, title: String) = Unit
    }

    private class SeededFrequentSites(vararg hosts: String) : FrequentSitesRepository {
        override val sites: StateFlow<List<FrequentSite>> = MutableStateFlow(
            hosts.map {
                FrequentSite(host = it, title = it, visitCount = 1, lastVisitEpochMillis = 0)
            },
        )

        override suspend fun recordVisit(host: String, title: String) = Unit
    }

    /**
     * The store keeps hosts and counts, never bitmaps, so every mark on the
     * grid has to be asked of the browser's local favicon store — and the ask
     * has to happen without the person doing anything, or a site visited last
     * week draws as a letter until its tab is somehow opened again.
     */
    @Test
    fun `the ranked hosts are asked of the favicon store unprompted`() = runTest(dispatcher) {
        val browser = RecordingBrowserRepository()
        val viewModel = NewTabViewModel(
            browser,
            NoAnalytics(),
            readyPythonParts(),
            SeededFrequentSites("en.wikipedia.org", "example.org"),
        )
        runCurrent()

        assertEquals(listOf(listOf("en.wikipedia.org", "example.org")), browser.markRequests)
        // Keep the double warm so the request above is attributable to
        // construction alone rather than to any collection of state.
        assertEquals(emptyList<Pair<String, Boolean>>(), browser.opened)
        viewModel.onShown()
    }
}

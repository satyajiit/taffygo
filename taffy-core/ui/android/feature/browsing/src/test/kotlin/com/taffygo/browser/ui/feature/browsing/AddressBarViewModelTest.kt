// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.FilteringSettings
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
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
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
 * Screen SCR-103 across two openings of the box, which is where the draft used
 * to leak.
 *
 * The reducer tests state the rule; these state the consequence, because the
 * part that made the fault so hard to see is not in any one state. The screen's
 * view model is cleared when the destination leaves the back stack, but its
 * saved-state handle outlives it — that is the whole point of a saved-state
 * handle — so the next box is built by a *new* view model reading what the last
 * one left behind. Each test here therefore uses two view models over one
 * handle, which is what a phone does between one tab and the next.
 */
class AddressBarViewModelTest {

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

    /**
     * The control for every other test in this file.
     *
     * A draft the person is still writing must survive the process dying under
     * them, and asserting that here is also what makes the tests below mean
     * anything: they claim a handle no longer carries an address between two
     * boxes, and that claim would be true of a handle that carried nothing at
     * all. This one proves the handle carries.
     */
    @Test
    fun `a draft interrupted part way through is still there when the box comes back`() {
        val handle = SavedStateHandle()
        val browser = FakeBrowser()

        val interrupted = addressBar(browser, handle)
        interrupted.onIntent(AddressBarIntent.InputChanged("en.wikiped"), NoNavigation())

        val restored = addressBar(browser, handle)

        assertEquals("en.wikiped", restored.state.value.input)
    }

    /**
     * The sequence from the phone, in the order a person did it.
     *
     * They typed an address on one tab and went to it. Later — after tabs had
     * been closed and another opened — they opened the box and asked a
     * question, and what came out was "wikipedia.orgwhat is a tapir". The field
     * was still holding the address they had already gone to, and a keyboard
     * types onto the end of whatever a field already holds.
     */
    @Test
    fun `words typed in a later box are not joined onto an address already gone to`() =
        runTest(dispatcher) {
            val handle = SavedStateHandle()
            val browser = FakeBrowser()

            val onOneTab = addressBar(browser, handle)
            onOneTab.onIntent(AddressBarIntent.InputChanged("wikipedia.org"), NoNavigation())
            onOneTab.onIntent(
                AddressBarIntent.Choose(browser.resolve("wikipedia.org")),
                NoNavigation(),
            )
            runCurrent()

            // The address really was committed, so what follows is about a box
            // whose work is done rather than one that never worked.
            assertEquals(listOf("wikipedia.org"), browser.committed)

            val onAnotherTab = addressBar(browser, handle)
            val field = onAnotherTab.state.value.input

            // A keyboard appends: what the field ends up holding is what was
            // already in it, followed by what they typed.
            onAnotherTab.onIntent(AddressBarIntent.InputChanged(field + "what is a tapir"), NoNavigation())

            assertEquals("what is a tapir", onAnotherTab.state.value.input)
        }

    /**
     * The second harm of the same leak, and the one that moved the page under
     * the person: the reopened box offered a reading of the old address, so the
     * top row was one tap away from leaving the page they were reading for
     * somewhere they had asked for minutes earlier on another tab.
     */
    @Test
    fun `the next box offers no reading of an address committed on an earlier tab`() =
        runTest(dispatcher) {
            val handle = SavedStateHandle()
            val browser = FakeBrowser()

            val onOneTab = addressBar(browser, handle)
            onOneTab.onIntent(AddressBarIntent.InputChanged("example.com"), NoNavigation())
            onOneTab.onIntent(
                AddressBarIntent.Choose(browser.resolve("example.com")),
                NoNavigation(),
            )
            runCurrent()

            val onAnotherTab = addressBar(browser, handle).state.value

            assertEquals("", onAnotherTab.input)
            assertNull(onAnotherTab.interpretation)
            assertEquals(emptyList<Suggestion>(), onAnotherTab.suggestions)
        }

    // -----------------------------------------------------------------------
    // What the box leaves on the back stack.
    //
    // The draft was one half of screen SCR-103 outliving itself; the entry on
    // the back stack was the other, and it is the half the system back button
    // showed. Committing an address used to *push* SCR-101 on top of the box,
    // so a stack of one became a stack of three, and back on a page the person
    // had followed a link to opened the address bar rather than the page
    // before it. These tests run the real reducer that owns the stack, so the
    // claim is about the arithmetic and not about a double that agreed with
    // itself.
    // -----------------------------------------------------------------------

    @Test
    fun `committing an address returns to the browsing surface rather than stacking it`() =
        runTest(dispatcher) {
            val browser = FakeBrowser()
            val navigator = StackNavigator(BackStack.start(TaffyDestination.BrowserMain))
            navigator.goTo(TaffyDestination.AddressBar)

            val box = addressBar(browser, SavedStateHandle())
            box.onIntent(AddressBarIntent.InputChanged("example.com"), navigator)
            box.onIntent(AddressBarIntent.Choose(browser.resolve("example.com")), navigator)
            runCurrent()

            assertEquals(listOf("example.com"), browser.committed)
            assertEquals(listOf(TaffyDestination.BrowserMain), navigator.stack.entries)
            // Which is the whole point: there is nothing left for back to walk
            // into, so the next press is the page's and then the browser's.
            assertFalse(navigator.stack.canGoBack)
        }

    /**
     * The sequence from the phone, counted.
     *
     * Load a page, open the box, go somewhere, and do it again. Every round
     * used to add two entries, which is why the fourth press of back was still
     * inside an application the person had asked to leave.
     */
    @Test
    fun `the stack is the size it started at however many addresses are committed`() =
        runTest(dispatcher) {
            val browser = FakeBrowser()
            val navigator = StackNavigator(BackStack.start(TaffyDestination.BrowserMain))
            val handle = SavedStateHandle()

            repeat(5) { visit ->
                navigator.goTo(TaffyDestination.AddressBar)
                val box = addressBar(browser, handle)
                box.onIntent(AddressBarIntent.InputChanged("example$visit.test"), navigator)
                box.onIntent(
                    AddressBarIntent.Choose(browser.resolve("example$visit.test")),
                    navigator,
                )
                runCurrent()
            }

            assertEquals(5, browser.committed.size)
            assertEquals(listOf(TaffyDestination.BrowserMain), navigator.stack.entries)
        }

    /**
     * The same rule where the box does not end on the browsing surface.
     *
     * Asking Taffy answers in the Assistant bar, which is genuinely a screen
     * forward — but the box is still finished with, and leaving it underneath
     * would put back into it from there instead of onto the page.
     */
    @Test
    fun `asking Taffy opens the Assistant bar with the page beneath it and not the box`() =
        runTest(dispatcher) {
            val browser = FakeBrowser()
            val navigator = StackNavigator(BackStack.start(TaffyDestination.BrowserMain))
            navigator.goTo(TaffyDestination.AddressBar)

            val box = addressBar(browser, SavedStateHandle())
            box.onIntent(
                AddressBarIntent.Choose(AddressBarInterpretation.AskTaffy("what is a tapir")),
                navigator,
            )
            runCurrent()

            assertEquals(
                listOf(
                    TaffyDestination.BrowserMain.screenId,
                    TaffyDestination.AssistantBar.SCREEN_ID,
                ),
                navigator.stack.entries.map { it.screenId },
            )
            // Compared by identifier rather than by value because an ask mints
            // an identity of its own, and asserted here because arriving at the
            // bar with nothing to ask is the fault this pairs with.
            val ask = navigator.stack.entries.last() as TaffyDestination.AssistantBar
            assertEquals("what is a tapir", ask.question)
        }

    /**
     * The box opened on top of the new-tab chooser, which is opened on top of
     * the tab switcher — the deepest the browsing chrome gets.
     *
     * Committing collapses all of it, because none of those screens is
     * somewhere back should return to once the person is looking at a page.
     */
    @Test
    fun `committing leaves the browsing chrome it was opened through behind`() =
        runTest(dispatcher) {
            val browser = FakeBrowser()
            val navigator = StackNavigator(BackStack.start(TaffyDestination.BrowserMain))
            navigator.goTo(TaffyDestination.TabSwitcher)
            navigator.goTo(TaffyDestination.NewTab)
            navigator.goTo(TaffyDestination.AddressBar)

            val box = addressBar(browser, SavedStateHandle())
            box.onIntent(AddressBarIntent.Choose(browser.resolve("example.com")), navigator)
            runCurrent()

            assertEquals(listOf(TaffyDestination.BrowserMain), navigator.stack.entries)
        }

    /**
     * A research shape, through the real back stack, twice.
     *
     * A task in a research shape reads pages the person names, and the Ask
     * sheet is where pages are named: choosing "Task for Taffy: compare two
     * phones" replaces the box with the sheet, carrying the words and the
     * shape as arguments. Each ask is an entry that names its own question,
     * and the two are different entries — which is what stops one sheet's
     * saved state reaching the other.
     */
    @Test
    fun `a research shape leaves the box for a sheet that carries its words and shape`() =
        runTest(dispatcher) {
            val browser = FakeBrowser()
            val navigator = StackNavigator(BackStack.start(TaffyDestination.BrowserMain))
            val handle = SavedStateHandle()

            navigator.goTo(TaffyDestination.AddressBar)
            val first = addressBar(browser, handle)
            first.onIntent(
                AddressBarIntent.Choose(
                    AddressBarInterpretation.TaskForTaffy(
                        "compare two phones",
                        TaskTemplate.COMPARE_PRODUCTS,
                    ),
                ),
                navigator,
            )
            runCurrent()

            assertEquals(
                listOf(TaffyDestination.BrowserMain.screenId, TaffyDestination.AssistantBar.SCREEN_ID),
                navigator.stack.entries.map { it.screenId },
            )
            assertEquals(
                TaffyDestination.AssistantBar("compare two phones", shape = TaskTemplate.COMPARE_PRODUCTS)
                    .arguments,
                navigator.stack.current.arguments,
            )
            // Nothing was committed to the browser: the sheet is where this
            // task is finished, and nothing starts on the way there.
            assertEquals(emptyList<String>(), browser.committed)

            navigator.goBack()
            navigator.goTo(TaffyDestination.AddressBar)
            val second = addressBar(browser, handle)
            second.onIntent(
                AddressBarIntent.Choose(
                    AddressBarInterpretation.TaskForTaffy(
                        "compare two laptops",
                        TaskTemplate.COMPARE_PRODUCTS,
                    ),
                ),
                navigator,
            )
            runCurrent()

            assertEquals(
                listOf(TaffyDestination.BrowserMain.screenId, TaffyDestination.AssistantBar.SCREEN_ID),
                navigator.stack.entries.map { it.screenId },
            )
            assertEquals(
                TaffyDestination.AssistantBar("compare two laptops", shape = TaskTemplate.COMPARE_PRODUCTS)
                    .arguments,
                navigator.stack.current.arguments,
            )
        }

    /** Giving up on the box is giving up on the draft, not storing it. */
    @Test
    fun `a box that was dismissed leaves nothing for the next one`() {
        val handle = SavedStateHandle()
        val browser = FakeBrowser()

        val abandoned = addressBar(browser, handle)
        abandoned.onIntent(AddressBarIntent.InputChanged("wikipedia.org"), NoNavigation())
        abandoned.onIntent(AddressBarIntent.Dismiss, NoNavigation())

        val next = addressBar(browser, handle)

        assertEquals("", next.state.value.input)
        assertNull(next.state.value.interpretation)
    }

    @Test
    fun `an index replacement refreshes rows without another keystroke`() =
        runTest(dispatcher) {
            val browser = FakeBrowser()
            browser.pageSuggestion = Suggestion(
                id = "bookmark-saved",
                title = "Saved page",
                interpretation = AddressBarInterpretation.GoTo(
                    "https://saved.example.test/page",
                    "saved.example.test",
                ),
                source = Suggestion.Source.BOOKMARK,
            )
            val box = addressBar(browser, SavedStateHandle())
            box.onIntent(AddressBarIntent.InputChanged("sav"), NoNavigation())
            assertTrue(box.state.value.suggestions.any { it.id == "bookmark-saved" })

            browser.pageSuggestion = null
            browser.suggestionRevision.value += 1L
            runCurrent()

            assertEquals("sav", box.state.value.input)
            assertFalse(box.state.value.suggestions.any { it.id == "bookmark-saved" })
        }

    /**
     * A chip is part of the draft, so it survives what the draft survives.
     *
     * It is saved beside the words rather than with them, because it is not the
     * person's words: it is a compiled-in label, and a build that dropped the
     * template restores as no shape rather than as a crash.
     */
    @Test
    fun `a chip set on a draft is still on it when the box comes back`() {
        val handle = SavedStateHandle()
        val browser = FakeBrowser()

        val interrupted = addressBar(browser, handle)
        interrupted.onIntent(AddressBarIntent.InputChanged("two laptops"), NoNavigation())
        interrupted.onIntent(
            AddressBarIntent.ChooseShape(TaskTemplate.COMPARE_PRODUCTS),
            NoNavigation(),
        )

        val restored = addressBar(browser, handle)

        assertEquals("two laptops", restored.state.value.input)
        assertEquals(TaskTemplate.COMPARE_PRODUCTS, restored.state.value.shape)
        val reading = restored.state.value.reading
        assertTrue(reading is AddressBarInterpretation.TaskForTaffy)
    }

    /** A label this build does not have is no shape, not a failure to start. */
    @Test
    fun `a chip naming a shape this build has no longer opens as no chip`() {
        val handle = SavedStateHandle()
        handle["address_bar_input"] = "two laptops"
        handle["address_bar_shape"] = "a-shape-from-another-build"

        val box = addressBar(FakeBrowser(), handle)

        assertEquals("two laptops", box.state.value.input)
        assertNull(box.state.value.shape)
    }

    /**
     * The start page's box is left without going anywhere.
     *
     * Screen SCR-101 is the bottom of the back stack, so its entry is never
     * released and its box's draft would otherwise wait in the next empty tab —
     * the same defect a dismissed SCR-103 used to have, on a screen that never
     * closes. [AddressBarIntent.Left] is the leaving, and it navigates nowhere
     * because the surface has already gone.
     */
    @Test
    fun `leaving the start page empties its box and moves no screen`() {
        val handle = SavedStateHandle()
        val browser = FakeBrowser()
        val started = BackStack.start(TaffyDestination.BrowserMain)
        val navigator = StackNavigator(started)

        val box = addressBar(browser, handle)
        box.onIntent(AddressBarIntent.InputChanged("wikipedia.org"), navigator)
        box.onIntent(AddressBarIntent.ChooseShape(TaskTemplate.WEB_ERRAND), navigator)
        box.onIntent(AddressBarIntent.Left, navigator)

        assertEquals("", box.state.value.input)
        assertNull(box.state.value.shape)
        assertEquals(started.entries, navigator.stack.entries)
        // And nothing waiting for the next one, either.
        val next = addressBar(browser, handle)
        assertEquals("", next.state.value.input)
        assertNull(next.state.value.shape)
    }

    // -----------------------------------------------------------------------
    // Doubles. Each records what it was asked to do and invents nothing.
    // -----------------------------------------------------------------------

    private fun addressBar(
        browser: BrowserRepository,
        handle: SavedStateHandle = SavedStateHandle(),
    ): AddressBarViewModel = AddressBarViewModel(
        browser,
        NoAnalytics(),
        handle,
        RecordingTaskRepository(),
        FixedReadiness(),
    )

    private class FakeBrowser : BrowserRepository {
        val committed = mutableListOf<String>()
        override val suggestionRevision = MutableStateFlow(0L)
        var pageSuggestion: Suggestion? = null

        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
        override val navigation: StateFlow<NavigationState> =
            MutableStateFlow(NavigationState(host = "", title = ""))
        override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
        override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())

        override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())

        override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())

        /** No refusal is provoked here: every reading these tests commit runs. */
        override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)

        override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit

        override fun resolve(input: String): AddressBarInterpretation =
            AddressBarInterpretation.GoTo(input, input)

        /** Empty for an empty box, exactly as the real repository answers. */
        override fun suggestions(input: String): List<Suggestion> =
            if (input.isBlank()) {
                emptyList()
            } else {
                listOf(Suggestion("suggestion_0", input, resolve(input))) +
                    listOfNotNull(pageSuggestion)
            }

        override suspend fun commit(interpretation: AddressBarInterpretation) {
            committed += interpretation.input
        }

        override fun dismissNotice() = Unit

        override suspend fun selectTab(id: TabId) = Unit

        override suspend fun closeTab(id: TabId) = Unit

        override suspend fun openTab(host: String, isPrivate: Boolean): TabId = TabId("tab")

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

    private class NoNavigation : TaffyNavigator {
        override fun goTo(destination: TaffyDestination) = Unit

        override fun replaceCurrent(destination: TaffyDestination) = Unit

        override fun goBack(): Boolean = false

        override fun goHome() = Unit

        override fun restart(destination: TaffyDestination) = Unit

        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    /**
     * The navigation contract over a real [BackStack], which is the only kind
     * of double that can be asked how big the stack got.
     *
     * It holds no rule of its own: every method is the back stack's own pure
     * operation, the same ones the application shell's `ShellNavigator` calls.
     * A double that decided for itself what "return to the browsing surface"
     * meant would agree with whatever this file expected and prove nothing.
     */
    private class StackNavigator(initial: BackStack) : TaffyNavigator {
        var stack: BackStack = initial
            private set

        override fun goTo(destination: TaffyDestination) {
            stack = stack.push(destination)
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            stack = stack.replaceCurrent(destination)
        }

        override fun goBack(): Boolean {
            if (!stack.canGoBack) return false
            stack = stack.pop()
            return true
        }

        override fun goHome() {
            stack = stack.home()
        }

        override fun restart(destination: TaffyDestination) {
            stack = stack.replaceAll(destination)
        }

        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) {
            stack = stack.popWhile(shouldPop)
        }
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit

        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

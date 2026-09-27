// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaffyPartId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-101's projection: one of loading, failed, start, or page — never two. */
class BrowserMainReducerTest {

    @Test
    fun `a loaded page shows content`() {
        val state = projectBrowserMain(NavigationState("docs.example.test", "Retention policy"), tabs())

        assertEquals(BrowserContent.PAGE, state.content)
        assertEquals("Retention policy", state.title)
    }

    @Test
    fun `the blocking facts ride the projection untouched`() {
        val state = projectBrowserMain(
            NavigationState(
                host = "docs.example.test",
                title = "Retention policy",
                filteringActive = true,
                blockedRequestCount = 9,
            ),
            tabs(),
            siteFilteringOpen = true,
        )

        assertTrue(state.filteringActive)
        assertEquals(9, state.blockedRequestCount)
        assertTrue(state.siteFilteringOpen)
        // The sheet's own projection sees the same page.
        assertEquals("docs.example.test", state.siteFiltering.host)
        assertEquals(9, state.siteFiltering.blockedCount)
    }

    @Test
    fun `a failure hides the content area`() {
        val navigation = NavigationState(
            host = "gone.example.test",
            title = "gone.example.test",
            failure = PageLoadFailure.NAME_NOT_RESOLVED,
        )

        assertEquals(BrowserContent.FAILED, projectBrowserMain(navigation, tabs()).content)
    }

    @Test
    fun `a page on its way does not take away the page that is there`() {
        // The rule ux-spec section 2 states: the content area is the web's and
        // Taffy never covers it. A navigation in flight is chrome's business —
        // the address pill's rail — and the reader keeps the page they have
        // until the next one commits.
        val navigation = NavigationState("docs.example.test", "Retention policy", isLoading = true)

        assertEquals(BrowserContent.PAGE, projectBrowserMain(navigation, tabs()).content)
    }

    @Test
    fun `a page that has not been anywhere carries no title to show`() {
        // What the browser opens when the last tab is closed. The seam reports
        // no title rather than the internal address the tab is really on, and
        // this screen renders that as its own start content.
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
            startPageGate = pythonReady,
        )

        assertEquals("", state.host)
        assertEquals("", state.title)
        assertEquals(BrowserContent.START, state.content)
    }

    // -----------------------------------------------------------------------
    // Whether this tab has been anywhere.
    //
    // The defect was that screen SCR-101 had no way to ask. The page host is an
    // engine surface that has to stay composed across a navigation, so a tab
    // with nothing loaded left that surface uncovered over the blank document
    // the engine paints — a white rectangle across two thirds of a dark screen.
    // The answer is read off the selected tab, because that is where it is
    // already true, and it is carried rather than guessed from an empty host.
    // -----------------------------------------------------------------------

    @Test
    fun `the selected tab having been nowhere is what selects the start content`() {
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
            startPageGate = pythonReady,
        )

        assertTrue(state.hasBeenNowhere)
        assertEquals(BrowserContent.START, state.content)
    }

    @Test
    fun `a tab with a page draws the page and not the start content`() {
        val state = projectBrowserMain(NavigationState("docs.example.test", "Retention policy"), tabs())

        assertFalse(state.hasBeenNowhere)
        assertEquals(BrowserContent.PAGE, state.content)
    }

    /**
     * The reason the fact is carried rather than inferred from a blank host.
     *
     * A `data:` page and a `file:` path have no host, and one that also has no
     * title of its own would look exactly like an empty tab to anything reading
     * emptiness. It is a real page, and drawing TaffyGo's own content over it
     * would be the browser writing on something the web served.
     */
    @Test
    fun `a hostless page is still a page`() {
        val tabs = listOf(Tab(TabId("tab_1"), title = "", host = "", isSelected = true))

        val state = projectBrowserMain(NavigationState(host = "", title = ""), tabs)

        assertFalse(state.hasBeenNowhere)
        assertEquals(BrowserContent.PAGE, state.content)
    }

    @Test
    fun `an empty tab that is not the selected one changes nothing here`() {
        val tabs = tabs() + Tab(TabId("tab_7"), title = "", host = "", hasBeenNowhere = true)

        assertEquals(BrowserContent.PAGE, projectBrowserMain(NavigationState("docs.example.test", "Retention"), tabs).content)
    }

    /**
     * The closed answer, and it goes the other way from the private one. With
     * no tab anybody can name there is no page either, so the start content is
     * what is drawn: being wrong in this direction hides nothing, while being
     * wrong in the other one shows a blank document as though it were a page.
     */
    @Test
    fun `no selected tab has been nowhere`() {
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            emptyList(),
            startPageGate = pythonReady,
        )

        assertTrue(state.hasBeenNowhere)
        assertEquals(BrowserContent.START, state.content)
    }

    @Test
    fun `a failure on an empty tab is still the failure`() {
        // Precedence, stated: a tab that has been nowhere and has a failure to
        // report is showing the failure. An invitation to type an address is
        // not an answer to "that address didn't resolve".
        val navigation = NavigationState(host = "", title = "", failure = PageLoadFailure.OFFLINE)

        assertEquals(BrowserContent.FAILED, projectBrowserMain(navigation, tabs(selectedIsBlank = true)).content)
    }

    @Test
    fun `an empty tab that is already fetching keeps its start content`() {
        // The one case with nothing underneath worth keeping: this tab has
        // committed nothing, so the engine's surface is still painting the
        // blank document and the start content is what stands over it until
        // the first navigation lands. `hasBeenNowhere` is what holds it there,
        // which is why the seam reads the committed address and not the
        // visible one — the visible one is already the site being fetched.
        val navigation = NavigationState(host = "", title = "", isLoading = true)

        assertEquals(
            BrowserContent.START,
            projectBrowserMain(
                navigation,
                tabs(selectedIsBlank = true),
                startPageGate = pythonReady,
            ).content,
        )
    }

    @Test
    fun `an empty private tab is both empty and private`() {
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true, selectedIsPrivate = true),
            startPageGate = pythonReady,
        )

        assertTrue(state.isPrivate)
        assertEquals(BrowserContent.START, state.content)
    }

    @Test
    fun `something the browser refused reaches the screen instead of disappearing`() {
        val state = projectBrowserMain(
            navigation = NavigationState("docs.example.test", "Retention policy"),
            tabs = tabs(),
            notice = BrowserNotice.NO_SEARCH_ENGINE,
        )

        assertEquals(BrowserNotice.NO_SEARCH_ENGINE, state.notice)
        // The page is untouched: a refusal is not a page failure, and the
        // notice is drawn beside the content rather than instead of it.
        assertEquals(BrowserContent.PAGE, state.content)
        assertNull(state.failure)
    }

    @Test
    fun `nothing refused means nothing to say`() {
        val state = projectBrowserMain(NavigationState("docs.example.test", "Retention policy"), tabs())

        assertNull(state.notice)
    }

    @Test
    fun `Taffy's tabs are counted apart from the user's`() {
        val state = projectBrowserMain(NavigationState("docs.example.test", "Retention policy"), tabs())

        assertEquals(2, state.userTabCount)
        assertEquals(1, state.taffyTabCount)
    }

    @Test
    fun `forward history reaches the stable action-row target`() {
        val state = projectBrowserMain(
            NavigationState(
                host = "docs.example.test",
                title = "Retention policy",
                canGoForward = true,
            ),
            tabs(),
        )

        assertTrue(state.canGoForward)
    }

    // -----------------------------------------------------------------------
    // Which tab this is.
    //
    // The defect was that this projection had no answer at all, so screen
    // SCR-101 drew a private tab exactly like every other one. The answer is
    // read off the selected tab, because that is where it is already true.
    // -----------------------------------------------------------------------

    @Test
    fun `the selected tab being private is what makes the screen private`() {
        val state = projectBrowserMain(
            NavigationState("prices.example.test", "Price history"),
            tabs(selectedIsPrivate = true),
        )

        assertTrue(state.isPrivate)
    }

    @Test
    fun `a private tab that is not the selected one changes nothing here`() {
        val tabs = tabs() + Tab(TabId("tab_5"), "Price history", "prices.example.test", isPrivate = true)

        assertFalse(projectBrowserMain(NavigationState("docs.example.test", "Retention"), tabs).isPrivate)
    }

    /**
     * The closed answer, and the reason it has to be the closed one: a browser
     * that guessed "private" from a list it could not read would be making the
     * forgetting promise on behalf of a tab nobody had identified.
     */
    @Test
    fun `no selected tab is not a private tab`() {
        val state = projectBrowserMain(NavigationState("docs.example.test", "Retention"), emptyList())

        assertFalse(state.isPrivate)
    }

    @Test
    fun `an empty tab shows the person's own frequent sites as tiles`() {
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
            startPageGate = pythonReady,
            sites = listOf(
                FrequentSite(
                    host = "shop.example.test",
                    title = "Product listing",
                    visitCount = 3,
                    lastVisitEpochMillis = 9,
                ),
            ),
        )

        assertEquals(BrowserContent.START, state.content)
        assertEquals(
            listOf(FrequentTile("shop.example.test", "Product listing")),
            state.frequent,
        )
    }

    @Test
    fun `a private tab's start page shows none of the regular profile's sites`() {
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsPrivate = true, selectedIsBlank = true),
            startPageGate = pythonReady,
            sites = listOf(
                FrequentSite(
                    host = "shop.example.test",
                    title = "Product listing",
                    visitCount = 3,
                    lastVisitEpochMillis = 9,
                ),
            ),
        )

        assertEquals(BrowserContent.START, state.content)
        assertTrue(state.isPrivate)
        assertEquals(emptyList<FrequentTile>(), state.frequent)
        // Not even the line saying where sites will appear: a private tab
        // records no visit, so that line would be a promise it cannot keep.
        assertFalse(state.showsFrequentSites)
    }

    @Test
    fun `with no selected row the tiles fail closed while a private tab exists`() {
        val sites = listOf(
            FrequentSite(
                host = "shop.example.test",
                title = "Product listing",
                visitCount = 3,
                lastVisitEpochMillis = 9,
            ),
        )
        val withPrivate = listOf(
            Tab(TabId("tab_1"), "", "", hasBeenNowhere = true, isPrivate = true),
            Tab(TabId("tab_2"), "Product listing", "shop.example.test"),
        )
        val regularOnly = listOf(Tab(TabId("tab_2"), "Product listing", "shop.example.test"))

        val blank = NavigationState(host = "", title = "")
        val closed = projectBrowserMain(blank, withPrivate, sites = sites)
        val open = projectBrowserMain(blank, regularOnly, sites = sites)

        assertFalse(closed.showsFrequentSites)
        assertEquals(emptyList<FrequentTile>(), closed.frequent)
        assertTrue(open.showsFrequentSites)
        assertEquals(listOf(FrequentTile("shop.example.test", "Product listing")), open.frequent)
    }

    @Test
    fun `scrolling down a page hides the top bar and nothing else`() {
        val state = projectBrowserMain(
            NavigationState("docs.example.test", "Retention policy"),
            tabs(),
            appearance = PageAppearance(topBarVisible = false),
        )

        assertFalse(state.topBarVisible)
        assertFalse(state.showsTopBar())
        // Out of sight, still this content's bar: the layout does not change
        // when it goes, which is what keeps the page the size it was.
        assertTrue(state.hasTopBar())
        assertEquals(BrowserContent.PAGE, state.content)
    }

    @Test
    fun `an empty tab keeps its top bar even if a scroll said to hide it`() {
        val state = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
            appearance = PageAppearance(topBarVisible = false),
        )

        assertTrue(state.topBarVisible)
        assertNull(state.pageBackgroundArgb)
    }

    @Test
    fun `a page colour reaches the screen only while there is a page`() {
        val colour = 0xFF1A73E8.toInt()
        val onAPage = projectBrowserMain(
            NavigationState("docs.example.test", "Retention policy"),
            tabs(),
            appearance = PageAppearance(backgroundArgb = colour),
        )
        val empty = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
            appearance = PageAppearance(backgroundArgb = colour),
        )

        assertEquals(colour, onAPage.pageBackgroundArgb)
        assertNull(empty.pageBackgroundArgb)
    }

    @Test
    fun `a failure forgets the last page colour so the status bar matches`() {
        val colour = 0xFF1A73E8.toInt()
        val state = projectBrowserMain(
            NavigationState(
                host = "gone.example.test",
                title = "gone.example.test",
                failure = PageLoadFailure.NAME_NOT_RESOLVED,
            ),
            tabs(),
            appearance = PageAppearance(backgroundArgb = colour),
        )

        assertEquals(BrowserContent.FAILED, state.content)
        assertNull(state.pageBackgroundArgb)
    }

    @Test
    fun `an https page lights the lock and an insecure one does not`() {
        val secure = projectBrowserMain(
            NavigationState("docs.example.test", "Retention policy", isSecure = true),
            tabs(),
        )
        val plain = projectBrowserMain(
            NavigationState("docs.example.test", "Retention policy", isSecure = false),
            tabs(),
        )

        assertTrue(secure.isSecure)
        assertFalse(plain.isSecure)
    }

    @Test
    fun `an empty tab waits for the python library before drawing the start page`() {
        val waiting = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
        )
        val ready = projectBrowserMain(
            NavigationState(host = "", title = ""),
            tabs(selectedIsBlank = true),
            startPageGate = pythonReady,
        )

        assertEquals(BrowserContent.PREPARING, waiting.content)
        assertEquals(BrowserContent.START, ready.content)
    }

    @Test
    fun `find and save overlays ride the projection`() {
        val find = openFindInPage(available = false)
        val state = projectBrowserMain(
            NavigationState(
                host = "docs.example.test",
                title = "Retention policy",
                canonicalUrl = "https://docs.example.test/policies/retention?region=in#exceptions",
            ),
            tabs(),
            moreOpen = true,
            findInPage = find,
            savePageOpen = true,
            bookmarksWritable = false,
        )

        assertTrue(state.moreOpen)
        assertTrue(state.findInPage.open)
        assertTrue(state.savePageOpen)
        assertFalse(state.savePage.primaryEnabled)
        assertEquals("docs.example.test", state.savePage.host)
        assertEquals(
            "https://docs.example.test/policies/retention?region=in#exceptions",
            state.canonicalUrl,
        )
        assertEquals(state.canonicalUrl, state.savePage.canonicalUrl)
    }

    @Test
    fun `a loaded page is not held for the python library`() {
        val state = projectBrowserMain(
            NavigationState("docs.example.test", "Retention policy"),
            tabs(),
        )

        assertEquals(BrowserContent.PAGE, state.content)
        assertFalse(state.startPageGate.ready)
    }

    private val pythonReady = StartPageGate(
        ready = true,
        partId = TaffyPartId("python-stdlib"),
    )

    private fun tabs(
        selectedIsPrivate: Boolean = false,
        selectedIsBlank: Boolean = false,
    ) = listOf(
        Tab(
            TabId("tab_1"),
            if (selectedIsBlank) "" else "Retention policy",
            if (selectedIsBlank) "" else "docs.example.test",
            hasBeenNowhere = selectedIsBlank,
            isPrivate = selectedIsPrivate,
            isSelected = true,
        ),
        Tab(TabId("tab_2"), "Product listing", "shop.example.test"),
        Tab(TabId("tab_9"), "Independent review", "reviews.example.test", isTaffyTab = true),
    )
}

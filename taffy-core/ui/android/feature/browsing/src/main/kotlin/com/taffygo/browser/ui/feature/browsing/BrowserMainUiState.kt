// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.PageLoadFailure

/**
 * Screen SCR-101, the home of everything.
 *
 * Loading, error, and empty are states of this type rather than booleans
 * scattered through the screen (android-app-architecture section 4), so the
 * screen renders exactly one of them and a test can name which.
 */
data class BrowserMainUiState(
    /** The host of the page, shown instead of a full URL. */
    val host: String = "",
    /** The exact safe HTTP(S) address that last committed, for page actions. */
    val canonicalUrl: String = "",
    /** The page title as the page gave it. */
    val title: String = "",
    /**
     * Whether the tab being shown forgets everything when it closes.
     *
     * A property of the tab and therefore of this screen, not only of the
     * switcher that groups them. A person who opened a private tab, put the
     * phone down and came back has nothing else to go on: the page looks the
     * same either way, so without this the surface they are about to type an
     * address into cannot say which promise it is keeping.
     */
    val isPrivate: Boolean = false,
    /**
     * Whether the tab being shown has not been anywhere at all.
     *
     * A property of the tab, exactly as [isPrivate] is, and read off the same
     * selected tab for the same reason. It is here rather than derived from an
     * empty [host] because the two questions have different answers: a `data:`
     * URL and a `file:` path have no host either, and drawing TaffyGo's start
     * content over one of those would be the browser writing on a page the web
     * served. See `Tab.hasBeenNowhere` for where the fact comes from.
     */
    val hasBeenNowhere: Boolean = false,
    /** Whether the page is still arriving. */
    val isLoading: Boolean = false,
    /**
     * Whether the page is being served over a private connection.
     *
     * The address pill's lock is green only when this is true.
     */
    val isSecure: Boolean = false,
    /**
     * The selected tab's site mark, when the engine or the profile store
     * has one. Drawn in the address pill beside the lock.
     */
    val favicon: Bitmap? = null,
    /** Whether ad and tracker blocking is acting on the page being shown. */
    val filteringActive: Boolean = false,
    /**
     * Requests blocked on the page being shown, for the address pill's
     * shield. Coalesced by the browser: the number may jump, never lie, and
     * it resets when a navigation commits.
     */
    val blockedRequestCount: Int = 0,
    /** Whether the site sheet (screen SCR-204) is open over this screen. */
    val siteFilteringOpen: Boolean = false,
    /** What the site sheet renders, projected whether or not it is open. */
    val siteFiltering: SiteFilteringUiState = SiteFilteringUiState(),
    /** Why the page did not load, when it did not. */
    val failure: PageLoadFailure? = null,
    /**
     * Something the browser was asked to do and did not, still to be read.
     *
     * Separate from [failure], because it is a different kind of thing: a
     * failure is about the page in front of the person, and a notice is about
     * something they asked for that did not happen. The page is untouched, so
     * the notice is drawn beside it rather than over it.
     */
    val notice: BrowserNotice? = null,
    /** How many tabs the user has open. */
    val userTabCount: Int = 0,
    /** How many tabs Taffy opened for a task. */
    val taffyTabCount: Int = 0,
    /** Whether there is history behind this page. */
    val canGoBack: Boolean = false,
    /** Whether there is history ahead of this page. */
    val canGoForward: Boolean = false,
    /**
     * Whether the top bar's overflow menu is open.
     *
     * The page-actions glyph closes the top bar at its trailing edge (decision
     * 0118), so destinations that do not belong in a stable slot of either bar
     * live one tap behind it rather than wrapping onto a second line. There is
     * one such control and this is the one flag that opens it.
     */
    val moreOpen: Boolean = false,
    /**
     * The person's most-visited sites, for the start content.
     *
     * Empty-tab chrome used to say "Nothing open in this tab" while the start
     * page said "New tab". The start content is now the start page, so the
     * same tile grid lives here, projected by [frequentTilesFrom] from the
     * same store SCR-102 reads.
     */
    val frequent: List<FrequentTile> = emptyList(),
    /**
     * Whether the start content draws the frequent-sites grid at all.
     *
     * False while the selected tab is a private one (decision 0255). Separate
     * from an empty [frequent], which on a regular tab still draws the line
     * saying where sites will appear: a private tab records no visit, so that
     * line would promise something it will never do.
     */
    val showsFrequentSites: Boolean = true,
    /**
     * Whether the top bar — the address pill and the overflow — is showing.
     *
     * Hidden while scrolling down a page; a scroll up or a tap brings it back.
     * Always true on a tab that has been nowhere.
     *
     * **One bar, not both.** The action row at the bottom of this screen does
     * not read this field and has no field of its own: it is always up. That
     * is not a simplification of the scroll rule, it is what makes the bottom
     * of a page usable — because a row that never moves can be measured
     * against instead of overlaid, and the page ends above it. A cookie banner
     * anchored to the bottom of its own document, and the buttons in it, are
     * reachable for exactly that reason, and they cost no relayout because a
     * bar that never moves cannot cause one.
     *
     * It was `actionRowVisible`, then briefly `chromeVisible` while both bars
     * answered to it. Named for the one bar it governs, because a reader who
     * set it false expecting the whole chrome to go would be wrong about what
     * happens.
     */
    val topBarVisible: Boolean = true,
    /**
     * The page's own ground as packed ARGB, or null to use the theme surface.
     *
     * The window's status bar is transparent, so this colour is what the
     * icons sit on and what the strip above the page paints. Null on a tab
     * that has been nowhere, and null again on a failure: there is no page
     * colour to honour, and keeping the last one left the status bar
     * painted for a document that is no longer showing.
     */
    val pageBackgroundArgb: Int? = null,
    /**
     * Whether the start page may be drawn, and how far the file it waits on is.
     *
     * Closed by default. A tab that has been nowhere draws the start page only
     * once the Python library is installed; until then it draws the preparing
     * state instead.
     */
    val startPageGate: StartPageGate = StartPageGate(),
    /** Whether find in page (SCR-106) is open over this screen. */
    val findInPage: FindInPageUiState = FindInPageUiState(),
    /**
     * What a task driving this window has the surface say about itself.
     *
     * Inactive by default, which is the closed answer: a screen built by hand
     * — a preview, a semantics test — draws no frame and no band, because
     * claiming a task is working when none is would be the one thing this
     * feedback exists to make impossible to miss.
     *
     * It arrives on this state rather than being projected with the rest of it
     * because it is not a fact about the page. [BrowserTakeoverViewModel] owns
     * it; the screen folds it in.
     */
    val takeover: TakeoverUiState = TakeoverUiState(),
    /** Whether the save-page sheet (SCR-811) is open over this screen. */
    val savePageOpen: Boolean = false,
    /** What the save-page sheet renders, projected whether or not it is open. */
    val savePage: SavePageUiState = SavePageUiState(),
) {
    /**
     * Which of the four things the page area draws. Exactly one, always.
     *
     * The precedence is the order a person would expect to be told things in.
     * A failure comes first because it is about the page they asked for and
     * nothing else is worth saying while it stands. "This tab has been nowhere"
     * comes next, and is two answers: the preparing state until the Python
     * library is installed, then the start page. Otherwise the page itself.
     *
     * **[isLoading] is not consulted, and that is the change.** It used to sit
     * second, which meant every navigation replaced a perfectly readable page
     * with a skeleton until the next one committed. A page on its way does not
     * take the page that is there away; it lights the rail on the address pill,
     * which is chrome, and leaves the content area to the web as
     * `docs/design/ux-spec.md` §2 requires.
     *
     * That leaves one case to get right, and it is [hasBeenNowhere]'s job: a
     * brand-new tab loading its first page has no previous page to keep, and
     * the engine's surface is painting the blank document underneath. The flag
     * therefore has to mean "nothing has committed in this tab yet" rather than
     * "the address box is empty" — the two differ for exactly the half second
     * that would otherwise flash white. `ChromiumBrowserMediator` reads the
     * last-committed URL for this reason.
     */
    val content: BrowserContent
        get() = when {
            failure != null -> BrowserContent.FAILED
            hasBeenNowhere && !startPageGate.ready -> BrowserContent.PREPARING
            hasBeenNowhere -> BrowserContent.START
            else -> BrowserContent.PAGE
        }
}

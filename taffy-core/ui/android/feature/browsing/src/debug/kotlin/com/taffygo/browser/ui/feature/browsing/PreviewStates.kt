// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate

/**
 * Fixed states for the previews of this feature.
 *
 * A preview is a screenshot, and a screenshot is only comparable if it is the
 * same twice. Nothing here reads a clock, a device setting, or a repository.
 */
object PreviewStates {

    private val pythonReady = StartPageGate(
        ready = true,
        partId = TaffyPartId("python-stdlib"),
        downloadedBytes = 1,
        totalBytes = 1,
    )

    /** Screen SCR-101 with a page loaded and three tabs open. */
    val browserMain = BrowserMainUiState(
        host = "docs.example.test",
        canonicalUrl = "https://docs.example.test/policies/retention",
        title = "Retention policy",
        isSecure = true,
        userTabCount = 3,
        taffyTabCount = 1,
        canGoBack = true,
    )

    /**
     * The same screen with a private tab selected.
     *
     * A separate preview rather than a flag on the one above, because the whole
     * point of the treatment is that the two are told apart at a glance — which
     * is only checkable if both are drawn.
     */
    val browserMainPrivate = browserMain.copy(isPrivate = true)

    /**
     * The same screen on a tab that has not been anywhere.
     *
     * The state the browser is in the moment it opens and every time the last
     * tab is closed, and the one this screen used to have no drawing for: the
     * engine's surface was left uncovered over a blank document, which is a
     * white rectangle where the browser's own start content belongs. There is
     * no host and no title here because there is no page to have either.
     */
    val browserMainEmptyTab = BrowserMainUiState(
        hasBeenNowhere = true,
        userTabCount = 1,
        startPageGate = pythonReady,
    )

    /**
     * The same empty tab, waiting for the Python library to finish installing.
     *
     * The start page is not drawn. This is the first-open state until that
     * file is on the device.
     */
    val browserMainPreparing = BrowserMainUiState(
        hasBeenNowhere = true,
        userTabCount = 1,
        startPageGate = StartPageGate(
            ready = false,
            partId = TaffyPartId("python-stdlib"),
            downloadedBytes = 400,
            totalBytes = 1_000,
        ),
    )

    /** The same empty tab, in the treatment private browsing has. */
    val browserMainEmptyPrivateTab = browserMainEmptyTab.copy(isPrivate = true)

    /** Screen SCR-102 with somewhere to go back to. */
    val newTab = NewTabUiState(
        frequent = listOf(
            FrequentTile("docs.example.test", "Retention policy"),
            FrequentTile("shop.example.test", "Product listing"),
            FrequentTile("reviews.example.test", "Independent review"),
        ),
        // One of these four is Taffy's, so the action row's badge draws in
        // accent — the state the badge exists for, and the one worth seeing in
        // a preview rather than the plain count.
        userTabCount = 3,
        taffyTabCount = 1,
        startPageGate = pythonReady,
    )

    /** Screen SCR-103 mid-typing, with a task interpretation on top. */
    val addressBar = AddressBarUiState(
        input = "compare these two policies",
        interpretation = AddressBarInterpretation.TaskForTaffy(
            "compare these two policies",
            TaskTemplate.COMPARE_PRODUCTS,
        ),
        suggestions = listOf(
            Suggestion(
                "suggestion_0",
                "compare these two policies",
                AddressBarInterpretation.TaskForTaffy(
                    "compare these two policies",
                    TaskTemplate.COMPARE_PRODUCTS,
                ),
            ),
            Suggestion(
                "suggestion_1",
                "compare these two policies",
                AddressBarInterpretation.Search("compare these two policies"),
            ),
        ),
    )

    /** The Add pages sheet over the Ask overlay, with one page already ticked. */
    val attachPages = AttachPagesUiState(
        status = AskPagesSnapshot.Status.READY,
        rows = listOf(
            AttachPagesUiState.Row(
                TabId("tab_docs"),
                "Retention policy",
                "docs.example.test",
                ticked = true,
            ),
            AttachPagesUiState.Row(
                TabId("tab_shop"),
                "Product listing",
                "shop.example.test",
                ticked = false,
            ),
        ),
        tickedIds = setOf(TabId("tab_docs")),
    )

    /**
     * Screen SCR-104 with the user's tabs, one private tab, and Taffy's own
     * group — one card of each treatment the grid can draw: plain, selected,
     * amber with a count, and amber while it is still being read.
     */
    val tabSwitcher = TabSwitcherUiState(
        yourTabs = listOf(
            TabCard(TabId("tab_1"), "Retention policy", "docs.example.test", isSelected = true),
            TabCard(TabId("tab_2"), "Product listing", "shop.example.test"),
            // A tab that has not been anywhere, which is what the browser opens
            // when the last tab is closed. It has no title and no host, and the
            // card names itself rather than drawing an empty line.
            TabCard(TabId("tab_3"), title = "", host = ""),
        ),
        privateTabs = listOf(
            TabCard(TabId("tab_5"), "Price history", "prices.example.test", isPrivate = true),
        ),
        taffyTabs = listOf(
            TabCard(
                id = TabId("tab_9"),
                title = "Independent review",
                host = "reviews.example.test",
                openedByTaffy = true,
                factCount = 3,
            ),
            TabCard(
                id = TabId("tab_10"),
                title = "Specifications",
                host = "specs.example.test",
                openedByTaffy = true,
                isBeingRead = true,
            ),
        ),
    )

    /**
     * Screen SCR-201 with visits on two days. Epoch values are fixed so the
     * preview does not read a clock.
     */
    val history = HistoryUiState(
        availability = HistoryUiState.Availability.READY,
        days = listOf(
            HistoryDay(
                kind = HistoryDay.Kind.TODAY,
                epochDay = 19_888,
                visits = listOf(
                    HistoryVisit(
                        HistoryVisit.Id("hv_1"),
                        "Retention policy",
                        "docs.example.test",
                        visitedAtEpochMillis = 1_718_452_800_000L,
                    ),
                    HistoryVisit(
                        HistoryVisit.Id("hv_2"),
                        "Product listing",
                        "shop.example.test",
                        visitedAtEpochMillis = 1_718_449_200_000L,
                    ),
                ),
            ),
            HistoryDay(
                kind = HistoryDay.Kind.YESTERDAY,
                epochDay = 19_887,
                visits = listOf(
                    HistoryVisit(
                        HistoryVisit.Id("hv_3"),
                        "Independent review",
                        "reviews.example.test",
                        visitedAtEpochMillis = 1_718_366_400_000L,
                    ),
                ),
            ),
        ),
        totalCount = 3,
    )

    val historyLoading = HistoryUiState(availability = HistoryUiState.Availability.LOADING)

    val historyUnavailable = HistoryUiState(
        availability = HistoryUiState.Availability.UNAVAILABLE,
    )

    val historyNoMatches = HistoryUiState(
        query = "no such page",
        availability = HistoryUiState.Availability.READY,
        totalCount = 3,
    )

    /** Screen SCR-202 with starred pages in the default folder. */
    val bookmarks = BookmarksUiState(
        availability = BookmarksUiState.Availability.READY,
        folders = listOf(
            BookmarkFolder(
                id = BookmarkFolder.Id.ALL,
                name = "",
                bookmarks = listOf(
                    Bookmark(
                        Bookmark.Id("bm_1"),
                        "Retention policy",
                        "docs.example.test",
                    ),
                    Bookmark(
                        Bookmark.Id("bm_2"),
                        "Product listing",
                        "shop.example.test",
                    ),
                ),
            ),
        ),
        totalCount = 2,
    )

    val bookmarksLoading = BookmarksUiState(availability = BookmarksUiState.Availability.LOADING)

    val bookmarksUnavailable = BookmarksUiState(
        availability = BookmarksUiState.Availability.UNAVAILABLE,
    )
}

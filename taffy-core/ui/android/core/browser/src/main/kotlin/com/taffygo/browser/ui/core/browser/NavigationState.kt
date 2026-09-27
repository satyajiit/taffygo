// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import com.taffygo.browser.ui.core.model.PageLoadFailure

/**
 * What the browser is showing right now, as the Taffy-owned surfaces see it.
 *
 * A failure is a state of the navigation rather than a separate screen the app
 * decides to show: upstream owns the behaviour, and TaffyGo owns the wording.
 */
data class NavigationState(
    /** The host, shown instead of a full URL. */
    val host: String,
    /** The page title as the page gave it. */
    val title: String,
    /** Whether there is history behind this page. */
    val canGoBack: Boolean = false,
    /** Whether there is history ahead of it. */
    val canGoForward: Boolean = false,
    /** Whether the page is still arriving. */
    val isLoading: Boolean = false,
    /** Why the page did not load, when it did not. */
    val failure: PageLoadFailure? = null,
    /**
     * Whether the page is being served over a private connection.
     *
     * The address pill's lock is green only when this is true. A certificate
     * that did not check out is not private, even if the address began with
     * `https`.
     */
    val isSecure: Boolean = false,
    /**
     * Whether ad and tracker blocking is acting on this page: the profile's
     * toggle is on, no exception covers this host, and the rules are loaded.
     */
    val filteringActive: Boolean = false,
    /**
     * Whether a person has allowed this page's site, whatever the profile's
     * master toggle says.
     *
     * Answered by the plane this tab belongs to, so a private tab reports its
     * own allowances rather than the regular profile's (decision 0128). The
     * host never crosses the seam to decide it — the browser reads the tab's
     * own committed host and answers with this boolean.
     */
    val siteExcepted: Boolean = false,
    /**
     * Requests blocked on this page so far. Coalesced by the browser — the
     * number may jump, never lie — and it resets when a navigation commits.
     */
    val blockedRequestCount: Int = 0,
    /**
     * The exact canonical HTTP(S) address that last committed, when it is safe
     * to leave the browser.
     *
     * Unlike [host], this keeps the path, query, and fragment so sharing or
     * saving the page cannot silently point at its home page instead. It is
     * empty for non-web addresses, userinfo-bearing addresses, and whenever
     * the browser cannot name a committed page.
     */
    val canonicalUrl: String = "",
)

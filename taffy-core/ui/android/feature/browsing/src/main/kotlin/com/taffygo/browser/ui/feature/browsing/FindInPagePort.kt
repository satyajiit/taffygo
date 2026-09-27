// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.StateFlow

/**
 * Search the current page for a phrase.
 *
 * Not on `BrowserRepository` today. A build with nothing behind this seam
 * still opens the overlay and reports zero matches rather than drawing fake
 * highlights.
 */
interface FindInPagePort {

    /** Whether the engine can actually search the page in this build. */
    val isAvailable: Boolean

    /**
     * The last final engine answer for the exact page being searched.
     *
     * This also returns to zero when that page is replaced, its tab stops
     * being selected, a request times out, or [clear] is called. Publishing
     * invalidation here keeps the overlay from showing a count that belongs
     * to a page which is no longer in front of the person.
     */
    val matches: StateFlow<MatchCount>

    /**
     * Start a search. [MatchCount.activeIndex] is 1-based when there is a
     * match, and both fields are zero when there is none.
     */
    suspend fun find(query: String): MatchCount

    /** Move to the next match of the current phrase. */
    suspend fun next(): MatchCount

    /** Move to the previous match of the current phrase. */
    suspend fun previous(): MatchCount

    /** Drop the current phrase. Called when the overlay closes. */
    suspend fun clear()

    /**
     * One engine answer: which match is current, and how many there are.
     *
     * The screen state carries the same two numbers on [FindInPageUiState].
     */
    data class MatchCount(
        val activeIndex: Int = 0,
        val total: Int = 0,
    )
}

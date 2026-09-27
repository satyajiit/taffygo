// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * What the find overlay shows after one action.
 *
 * [activeIndex] and [matchCount] are the engine's last answer, not something
 * this function counts. An unavailable port reports zeros; this still writes
 * them rather than inventing a highlight.
 */
internal fun reduceFindInPage(
    state: FindInPageUiState,
    intent: FindInPageIntent,
    activeIndex: Int = state.activeIndex,
    matchCount: Int = state.matchCount,
): FindInPageUiState = when (intent) {
    is FindInPageIntent.QueryChanged -> {
        val empty = intent.query.isBlank()
        state.copy(
            query = intent.query,
            activeIndex = if (empty) 0 else activeIndex,
            matchCount = if (empty) 0 else matchCount,
        )
    }
    FindInPageIntent.Next,
    FindInPageIntent.Previous,
    -> state.copy(activeIndex = activeIndex, matchCount = matchCount)
    FindInPageIntent.Close -> FindInPageUiState(available = state.available)
}

/** Open the overlay over the current page. The field starts empty. */
internal fun openFindInPage(available: Boolean): FindInPageUiState =
    FindInPageUiState(open = true, available = available)

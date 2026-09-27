// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * Screen SCR-501 — collections of things the person asked Taffy to keep.
 *
 * Search is part of the state so the matching rule is a pure function. Empty
 * and no-matches are different sentences; unavailable is a third.
 */
data class LibraryHomeUiState(
    /** Whether the store has not answered yet. */
    val loading: Boolean = false,
    /** Whether the store is not connected. */
    val unavailable: Boolean = false,
    /** Collections that match the query. */
    val collections: List<LibraryRepository.Collection> = emptyList(),
    /** What the person is searching for. */
    val query: String = "",
    /** How many collections exist, matched or not. */
    val totalCount: Int = 0,
) {
    /** Nothing kept at all, as opposed to nothing matching. */
    val isEmpty: Boolean
        get() = !loading && !unavailable && totalCount == 0

    /** A search found nothing, which is a different sentence. */
    val hasNoMatches: Boolean
        get() = !loading && !unavailable && totalCount > 0 && collections.isEmpty()
}

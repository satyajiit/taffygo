// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * What [HistoryRepository] currently knows.
 *
 * Ready with no visits is empty, not unavailable. Unavailable means there is
 * no visit list at all, so the screen must not invent one.
 */
sealed interface HistorySnapshot {
    /** The visit list has not arrived yet. */
    data object Loading : HistorySnapshot

    /** There is no visit list to read. */
    data object Unavailable : HistorySnapshot

    /**
     * The visit list as stored.
     *
     * [visits] may still include private rows or Taffy's working trail; the
     * projection is what drops them. An empty list is honest empty.
     */
    data class Ready(
        val visits: List<HistoryVisit>,
        /** False when the bounded browser reader stopped before the store ended. */
        val complete: Boolean = true,
    ) : HistorySnapshot
}

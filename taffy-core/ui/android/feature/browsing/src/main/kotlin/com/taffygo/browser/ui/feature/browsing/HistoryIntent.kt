// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Everything screen SCR-201 can be asked to do. */
sealed interface HistoryIntent {

    /** Narrow the list. Empty query shows every visit. */
    data class QueryChanged(val query: String) : HistoryIntent

    /** Open the page this visit is about. */
    data class Open(val id: HistoryVisit.Id) : HistoryIntent

    /** Remove one visit, after the screen has confirmed. */
    data class Delete(val id: HistoryVisit.Id) : HistoryIntent

    /** Open Clear browsing data. History itself does not wipe a range. */
    data object Clear : HistoryIntent

    /** Leave this screen. */
    data object Dismiss : HistoryIntent
}

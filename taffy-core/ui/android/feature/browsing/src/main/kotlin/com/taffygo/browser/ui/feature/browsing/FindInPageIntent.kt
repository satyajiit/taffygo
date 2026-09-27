// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Everything the find-in-page overlay can be asked to do. */
sealed interface FindInPageIntent {

    /** The phrase in the field changed. */
    data class QueryChanged(val query: String) : FindInPageIntent

    /** Jump to the next match. */
    data object Next : FindInPageIntent

    /** Jump to the previous match. */
    data object Previous : FindInPageIntent

    /** Close the overlay. The query is forgotten. */
    data object Close : FindInPageIntent
}

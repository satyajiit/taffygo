// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap

/**
 * Screen SCR-201 — pages opened on this phone, grouped by day.
 *
 * Empty is not unavailable, and a search that matches nothing is not empty.
 * Open is disabled when the visit list cannot be read, so a missing store
 * never pretends a row will load a page.
 */
data class HistoryUiState(
    val query: String = "",
    val availability: Availability = Availability.READY,
    val days: List<HistoryDay> = emptyList(),
    val totalCount: Int = 0,
    val siteMarks: Map<String, Bitmap> = emptyMap(),
) {
    /** Whether the visit list has arrived, and whether it exists at all. */
    enum class Availability {
        LOADING,
        READY,
        UNAVAILABLE,
    }

    /** Whether there is nothing at all, as opposed to nothing matching. */
    val isEmpty: Boolean
        get() = availability == Availability.READY && totalCount == 0

    /** Whether a search found nothing, which is a different sentence. */
    val hasNoMatches: Boolean
        get() = availability == Availability.READY && totalCount > 0 && days.isEmpty()

    val isLoading: Boolean
        get() = availability == Availability.LOADING

    val isUnavailable: Boolean
        get() = availability == Availability.UNAVAILABLE

    /** Whether tapping a row may open the page. */
    val canOpen: Boolean
        get() = availability == Availability.READY
}

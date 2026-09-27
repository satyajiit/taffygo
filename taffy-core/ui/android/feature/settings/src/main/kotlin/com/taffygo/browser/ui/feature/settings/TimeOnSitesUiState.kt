// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-411 — time on sites. */
data class TimeOnSitesUiState(
    val availability: YouSurfaceAvailability = YouSurfaceAvailability.UNAVAILABLE,
    val range: Range = Range.TODAY,
    val sites: List<Site> = emptyList(),
    val totalMillis: Long = 0,
    val largestMillis: Long = 0,
    val todayMillis: Long = 0,
    val weekMillis: Long = 0,
    val todaySiteCount: Int = 0,
    val weekSiteCount: Int = 0,
    val clearedWithHistory: Boolean = false,
    val hasGroupedSites: Boolean = false,
    val recoveredFromCorruption: Boolean = false,
) {
    /** Today or the last seven local days. */
    enum class Range {
        TODAY,
        THIS_WEEK,
    }

    /** One site in the current range. */
    data class Site(
        val site: String,
        val durationMillis: Long,
        val grouped: Boolean = false,
    )
}

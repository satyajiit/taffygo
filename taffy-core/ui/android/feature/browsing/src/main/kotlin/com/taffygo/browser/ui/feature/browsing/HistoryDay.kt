// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * One day's visits on screen SCR-201, newest first.
 *
 * [epochDay] is the local calendar day of the visits. The screen turns Today
 * and Yesterday into words and formats any older day; the projection never
 * writes a locale string.
 */
data class HistoryDay(
    val kind: Kind,
    val epochDay: Long,
    val visits: List<HistoryVisit>,
) {
    /** How the screen should name this day. */
    enum class Kind {
        TODAY,
        YESTERDAY,
        DATE,
    }
}

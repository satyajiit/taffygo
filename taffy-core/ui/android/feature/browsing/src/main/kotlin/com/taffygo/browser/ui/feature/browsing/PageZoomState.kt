// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** The selected page's persisted layout-zoom state. */
data class PageZoomState(
    /** Whether the selected tab has a live page that Chromium can zoom. */
    val available: Boolean = false,
    /** The user-visible level after Chromium's factor-to-percent conversion. */
    val percent: Int = DEFAULT_PERCENT,
    /** Whether another smaller preset exists. */
    val canZoomOut: Boolean = false,
    /** Whether another larger preset exists. */
    val canZoomIn: Boolean = false,
    /** Whether reset would change the selected host's stored level. */
    val canReset: Boolean = false,
) {
    companion object {
        const val DEFAULT_PERCENT: Int = 100
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.StateFlow

/** Selected-page zoom, backed by Chromium's per-host zoom store. */
interface PageZoomRepository {
    val state: StateFlow<PageZoomState>

    /** Move to Chromium's next larger layout-zoom preset. */
    suspend fun zoomIn()

    /** Move to Chromium's next smaller layout-zoom preset. */
    suspend fun zoomOut()

    /** Return the selected host to the profile's default zoom. */
    suspend fun reset()
}

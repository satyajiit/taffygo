// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

internal data class BrowserMainChromeOverlay(
    val moreOpen: Boolean,
    val siteFilteringOpen: Boolean,
    val findInPage: FindInPageUiState,
    val savePageOpen: Boolean,
    val savePageStatus: SavePageUiState.SaveStatus,
)

internal data class BrowserMainInputs(
    val navigation: NavigationState,
    val tabs: List<Tab>,
    val chrome: BrowserMainChromeOverlay,
    val notice: BrowserNotice?,
    val appearance: PageAppearance,
)

internal data class BrowserMainResources(
    val artwork: Map<TabId, TabArtwork>,
    val marks: Map<String, Bitmap>,
    val filtering: FilteringSettings,
    val pageZoom: PageZoomState,
    val permissionReset: SitePermissionResetPresentation,
    val siteBlocking: SiteBlockingPresentation,
)

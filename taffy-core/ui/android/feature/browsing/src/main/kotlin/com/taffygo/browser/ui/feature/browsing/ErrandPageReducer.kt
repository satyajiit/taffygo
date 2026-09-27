// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.ErrandPage

/**
 * The page an errand is on, as screen SCR-110 draws it.
 *
 * Pure, and callable from a host test: everything SCR-110 shows is decided
 * here, so what the toolbar says about an origin can be checked on a laptop
 * rather than only on a phone.
 */
internal object ErrandPageReducer {

    /**
     * Projects the errand this route names, or the gone state.
     *
     * The identity is compared rather than trusted. One errand runs at a time,
     * so a screen still composed for a previous one — a frame during a
     * replacement, a restored route — would otherwise draw somebody else's
     * page under its own toolbar and offer a back button that walked it.
     */
    fun project(page: ErrandPage?, errandId: String): ErrandPageUiState {
        if (page == null || page.errandId != errandId) return ErrandPageUiState(gone = true)
        val navigation = page.navigation
        return ErrandPageUiState(
            host = navigation.host,
            isSecure = navigation.isSecure,
            isLoading = navigation.isLoading,
            blockedCount = navigation.blockedRequestCount,
            canGoBack = navigation.canGoBack,
            gone = false,
        )
    }
}

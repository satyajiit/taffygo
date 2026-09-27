// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TimeOnSitesUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        TimeOnSitesContent(state = TimeOnSitesUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun TimeOnSitesReadyDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TimeOnSitesContent(
            state = TimeOnSitesUiState(
                availability = YouSurfaceAvailability.READY,
                sites = listOf(
                    TimeOnSitesUiState.Site("youtube.com", 48L * 60_000),
                    TimeOnSitesUiState.Site("croma.com", 22L * 60_000),
                ),
                totalMillis = 70L * 60_000,
                largestMillis = 48L * 60_000,
                todayMillis = 70L * 60_000,
                weekMillis = 70L * 60_000,
                todaySiteCount = 2,
                weekSiteCount = 2,
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun TimeOnSitesEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        TimeOnSitesContent(
            state = TimeOnSitesUiState(availability = YouSurfaceAvailability.READY),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun TimeOnSitesLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        TimeOnSitesContent(
            state = TimeOnSitesUiState(availability = YouSurfaceAvailability.LOADING),
            onIntent = {},
        )
    }
}

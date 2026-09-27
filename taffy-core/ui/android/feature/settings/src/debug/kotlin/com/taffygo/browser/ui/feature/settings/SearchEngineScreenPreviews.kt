// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.browser.SearchEngineId
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

@ThemePreviews
@Composable
private fun SearchEnginePreview() {
    TaffyPreview(darkTheme = false) {
        SearchEngineContent(
            state = projectSearchEngine(SearchEngineId.GOOGLE, "US"),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun SearchEngineJapanPreview() {
    TaffyPreview(darkTheme = true) {
        SearchEngineContent(
            state = projectSearchEngine(SearchEngineId.YAHOO_JP, "JP"),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun SearchEngineGermanyPreview() {
    TaffyPreview(darkTheme = false) {
        SearchEngineContent(
            state = projectSearchEngine(SearchEngineId.DUCKDUCKGO_DE, "DE"),
            onIntent = {},
        )
    }
}

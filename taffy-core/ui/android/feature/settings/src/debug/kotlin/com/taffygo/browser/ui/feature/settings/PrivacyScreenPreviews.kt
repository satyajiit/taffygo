// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

@ThemePreviews
@Composable
private fun PrivacyPreview() {
    TaffyPreview(darkTheme = false) {
        PrivacyContent(state = PrivacyUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun PrivacyConnectedPreview() {
    TaffyPreview(darkTheme = true) {
        PrivacyContent(
            state = PrivacyUiState(
                route = ProviderRoute.DIRECT_WITH_YOUR_KEY,
            ),
            onIntent = {},
        )
    }
}

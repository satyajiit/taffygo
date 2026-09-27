// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

@ThemePreviews
@Composable
private fun ClearBrowsingDataPreview() {
    TaffyPreview(darkTheme = false) {
        ClearBrowsingDataContent(state = ClearBrowsingDataUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ClearBrowsingDataConfirmPreview() {
    TaffyPreview(darkTheme = true) {
        ClearBrowsingDataContent(
            state = ClearBrowsingDataUiState(available = true, confirming = true),
            onIntent = {},
        )
    }
}

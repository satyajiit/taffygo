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
private fun GeneralPreview() {
    TaffyPreview(darkTheme = false) {
        GeneralContent(state = GeneralUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun GeneralDarkPreview() {
    TaffyPreview(darkTheme = true) {
        GeneralContent(state = GeneralUiState(), onIntent = {})
    }
}

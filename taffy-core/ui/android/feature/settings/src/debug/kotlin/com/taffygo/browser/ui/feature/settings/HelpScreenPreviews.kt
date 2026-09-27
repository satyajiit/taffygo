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
private fun HelpPreview() {
    TaffyPreview(darkTheme = false) {
        HelpContent(state = HelpUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun HelpDarkPreview() {
    TaffyPreview(darkTheme = true) {
        HelpContent(state = HelpUiState(emailUnavailable = true), onIntent = {})
    }
}

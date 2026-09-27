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
private fun AboutUnknownPreview() {
    TaffyPreview(darkTheme = false) {
        AboutContent(state = AboutUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun AboutVersionPreview() {
    TaffyPreview(darkTheme = true) {
        AboutContent(
            state = AboutUiState(
                facts = UnavailableAboutRepository().facts.copy(
                    versionName = "1.0",
                    chromiumVersion = "152.0.7977.42",
                ),
            ),
            onIntent = {},
        )
    }
}

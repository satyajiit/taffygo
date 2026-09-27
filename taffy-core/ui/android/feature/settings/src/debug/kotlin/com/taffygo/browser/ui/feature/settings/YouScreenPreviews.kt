// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun YouSignedOutPreview() {
    TaffyPreview(darkTheme = false) {
        YouContent(state = YouUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun YouNamedDarkPreview() {
    TaffyPreview(darkTheme = true) {
        YouContent(
            state = YouUiState(
                displayName = "Priya Sharma",
                avatar = LocalAvatar.of("a1"),
                monogram = "PS",
                memoryCount = 2,
                detailsCount = 1,
                timeAvailability = YouSurfaceAvailability.READY,
                timeHasSites = true,
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun YouDetailsPreview() {
    TaffyPreview(darkTheme = false) {
        YouContent(
            state = YouUiState(
                detailsOpen = true,
                avatar = LocalAvatar.of("b2"),
                monogram = "T",
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun YouDetailsNamedPreview() {
    TaffyPreview(darkTheme = false) {
        YouContent(
            state = YouUiState(
                detailsOpen = true,
                displayName = "Priya Sharma",
                nameDraft = "Priya S",
                avatar = LocalAvatar.Monogram,
                monogram = "PS",
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun YouPrivatePreview() {
    TaffyPreview(darkTheme = false) {
        YouContent(state = YouUiState(privateTab = true), onIntent = {})
    }
}

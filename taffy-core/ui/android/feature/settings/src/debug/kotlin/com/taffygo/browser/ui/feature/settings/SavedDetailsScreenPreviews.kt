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
private fun SavedDetailsEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        SavedDetailsContent(
            state = SavedDetailsUiState(),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun SavedDetailsListDarkPreview() {
    TaffyPreview(darkTheme = true) {
        SavedDetailsContent(
            state = SavedDetailsUiState(
                people = listOf(
                    SavedDetailsRepository.Person(
                        id = "p1",
                        givenName = "Priya",
                        familyName = "Sharma",
                        email = "priya@email.example",
                        phone = "98765 43210",
                    ),
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun SavedDetailsEditorPreview() {
    TaffyPreview(darkTheme = false) {
        SavedDetailsContent(
            state = SavedDetailsUiState(
                editor = SavedDetailsUiState.Editor(givenName = "Priya"),
            ),
            onIntent = {},
        )
    }
}

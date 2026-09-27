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
private fun SettingsHomePreview() {
    TaffyPreview(darkTheme = false) {
        SettingsHomeContent(state = SettingsHomeUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SettingsHomeDarkPreview() {
    TaffyPreview(darkTheme = true) {
        SettingsHomeContent(
            state = SettingsHomeUiState(
                displayName = "Ada",
                avatar = LocalAvatar.of("a1"),
                monogram = "AL",
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun SettingsHomeNoMatchesPreview() {
    TaffyPreview(darkTheme = false) {
        SettingsHomeContent(state = SettingsHomeUiState(query = "sync"), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SettingsHomeSearchPasswordPreview() {
    TaffyPreview(darkTheme = false) {
        SettingsHomeContent(state = SettingsHomeUiState(query = "password"), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SettingsHomeSearchOpenPreview() {
    TaffyPreview(darkTheme = false) {
        SettingsHomeContent(
            state = SettingsHomeUiState(searchOpen = true),
            onIntent = {},
        )
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun LibraryHomePreview() {
    TaffyPreview(darkTheme = false) {
        LibraryHomeContent(state = LibraryPreviewStates.homePopulated, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun LibraryHomeDarkPreview() {
    TaffyPreview(darkTheme = true) {
        LibraryHomeContent(state = LibraryPreviewStates.homePopulated, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun LibraryHomeEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        LibraryHomeContent(state = LibraryPreviewStates.homeEmpty, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun LibraryHomeUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        LibraryHomeContent(state = LibraryPreviewStates.homeUnavailable, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun LibraryHomeLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        LibraryHomeContent(state = LibraryPreviewStates.homeLoading, onIntent = {})
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun BookmarksPreview() {
    TaffyPreview(darkTheme = false) {
        BookmarksContent(state = PreviewStates.bookmarks, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun BookmarksDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BookmarksContent(state = PreviewStates.bookmarks, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun BookmarksEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        BookmarksContent(state = BookmarksUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun BookmarksLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        BookmarksContent(state = PreviewStates.bookmarksLoading, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun BookmarksUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        BookmarksContent(state = PreviewStates.bookmarksUnavailable, onIntent = {})
    }
}

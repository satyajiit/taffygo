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
private fun WorkspaceListPreview() {
    TaffyPreview(darkTheme = false) {
        WorkspaceListContent(state = WorkspacePreviewStates.list, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun WorkspaceListDarkPreview() {
    TaffyPreview(darkTheme = true) {
        WorkspaceListContent(state = WorkspacePreviewStates.list, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun WorkspaceListEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        WorkspaceListContent(state = WorkspaceListUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun WorkspaceListLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        WorkspaceListContent(state = WorkspaceListUiState(loading = true), onIntent = {})
    }
}

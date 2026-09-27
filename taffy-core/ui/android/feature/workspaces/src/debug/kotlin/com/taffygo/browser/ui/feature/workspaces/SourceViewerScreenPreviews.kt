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
private fun SourceViewerPreview() {
    TaffyPreview(darkTheme = false) {
        SourceViewerContent(state = WorkspacePreviewStates.sourceViewer, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SourceViewerDarkPreview() {
    TaffyPreview(darkTheme = true) {
        SourceViewerContent(state = WorkspacePreviewStates.sourceViewer, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun SourceViewerMissingPreview() {
    TaffyPreview(darkTheme = false) {
        SourceViewerContent(state = SourceViewerUiState(missing = true), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SourceViewerLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        SourceViewerContent(state = SourceViewerUiState(loading = true), onIntent = {})
    }
}

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
private fun ExportSheetPreview() {
    TaffyPreview(darkTheme = false) {
        ExportSheetContent(state = WorkspacePreviewStates.exportSheet, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ExportSheetDarkPreview() {
    TaffyPreview(darkTheme = true) {
        ExportSheetContent(state = WorkspacePreviewStates.exportSheet, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun ExportSheetMissingPreview() {
    TaffyPreview(darkTheme = false) {
        ExportSheetContent(state = ExportSheetUiState(missing = true), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ExportSheetLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        ExportSheetContent(state = ExportSheetUiState(loading = true), onIntent = {})
    }
}

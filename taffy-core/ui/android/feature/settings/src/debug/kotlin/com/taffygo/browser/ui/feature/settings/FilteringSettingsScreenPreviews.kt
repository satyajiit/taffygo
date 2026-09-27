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
private fun FilteringSettingsPreview() {
    TaffyPreview(darkTheme = false) {
        FilteringSettingsContent(
            state = FilteringSettingsUiState(
                blockedTotal = 12_408,
                blockedThisWeek = 1_284,
                minimumSitesThisWeek = 36,
                exceptionHosts = listOf("news.example.test", "docs.example.test"),
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun FilteringSettingsWeekUnknownPreview() {
    TaffyPreview(darkTheme = true) {
        FilteringSettingsContent(
            state = FilteringSettingsUiState(
                enabled = false,
                blockedTotal = 12_408,
            ),
            onIntent = {},
        )
    }
}

/** Blocking off, with allowances still listed and read-only. */
@ThemePreviews
@Composable
private fun FilteringSettingsExceptionsReadOnlyPreview() {
    TaffyPreview(darkTheme = false) {
        FilteringSettingsContent(
            state = FilteringSettingsUiState(
                enabled = false,
                blockedTotal = 12_408,
                exceptionHosts = listOf("news.example.test", "docs.example.test"),
            ),
            onIntent = {},
        )
    }
}

/** A refusal on one row, which used to be discarded. */
@ThemePreviews
@Composable
private fun FilteringSettingsRemovalRefusedPreview() {
    TaffyPreview(darkTheme = true) {
        FilteringSettingsContent(
            state = FilteringSettingsUiState(
                blockedTotal = 12_408,
                exceptionHosts = listOf("news.example.test", "docs.example.test"),
                removal = FilteringSettingsUiState.Removal(
                    host = "news.example.test",
                    status = FilteringSettingsUiState.Removal.Status.FAILED,
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun FilteringSettingsNothingYetPreview() {
    TaffyPreview(darkTheme = false) {
        FilteringSettingsContent(state = FilteringSettingsUiState(), onIntent = {})
    }
}

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
private fun SavedSignInsUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        SavedSignInsContent(state = SavedSignInsUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SavedSignInsListDarkPreview() {
    TaffyPreview(darkTheme = true) {
        SavedSignInsContent(
            state = SavedSignInsUiState(
                availability = YouSurfaceAvailability.READY,
                records = listOf(
                    SavedSignInsRepository.Record(
                        id = "s1",
                        site = "croma.com",
                        username = "you@email.example",
                        lastUsedEpochMillis = 1_780_000_000_000L,
                    ),
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun SavedSignInsDetailPreview() {
    val record = SavedSignInsRepository.Record(
        id = "s1",
        site = "croma.com",
        username = "you@email.example",
        lastUsedEpochMillis = 1_780_000_000_000L,
    )
    TaffyPreview(darkTheme = false) {
        SavedSignInsContent(
            state = SavedSignInsUiState(
                availability = YouSurfaceAvailability.READY,
                records = listOf(record),
                opened = record,
            ),
            onIntent = {},
        )
    }
}

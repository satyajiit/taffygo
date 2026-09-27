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
private fun PersonalityPreview() {
    TaffyPreview(darkTheme = false) {
        PersonalityContent(
            state = projectPersonality(
                PersonalityRepository.Snapshot(
                    availability = PersonalityRepository.Availability.READY,
                    selected = PersonalityRepository.Preset.CAREFUL_RESEARCHER,
                    scales = PersonalityRepository.Preset.CAREFUL_RESEARCHER.scales(),
                ),
                false,
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun PersonalityDarkPreview() {
    TaffyPreview(darkTheme = true) {
        PersonalityContent(
            state = PersonalityUiState(
                availability = PersonalityRepository.Availability.READY,
                selected = PersonalityRepository.Preset.QUICK_SHOPPER,
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun PersonalityUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        PersonalityContent(
            state = PersonalityUiState(
                availability = PersonalityRepository.Availability.UNAVAILABLE,
                choiceNotSaved = true,
            ),
            onIntent = {},
        )
    }
}

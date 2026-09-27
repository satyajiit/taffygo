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
private fun PersonalityTuningPreview() {
    TaffyPreview(darkTheme = false) {
        PersonalityTuningContent(
            state = projectPersonalityTuning(
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
private fun PersonalityTuningDarkPreview() {
    TaffyPreview(darkTheme = true) {
        PersonalityTuningContent(
            state = PersonalityTuningUiState(
                availability = PersonalityRepository.Availability.READY,
                scales = PersonalityRepository.Scales(pace = 2, length = 2, checkIn = 2),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun PersonalityTuningUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        PersonalityTuningContent(
            state = PersonalityTuningUiState(
                availability = PersonalityRepository.Availability.UNAVAILABLE,
                notSaved = true,
            ),
            onIntent = {},
        )
    }
}

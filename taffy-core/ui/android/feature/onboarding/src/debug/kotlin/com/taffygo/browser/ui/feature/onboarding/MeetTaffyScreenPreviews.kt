// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/**
 * Debug-only Compose previews; never part of the product APK.
 *
 * A preview draws the start poster and never a frame of film: `AndroidView`
 * renders nothing under `LocalInspectionMode`, and the decoder these pages
 * create is a platform `MediaPlayer`. That is the honest picture of the first
 * moment of the screen, not a shortcoming of the preview.
 */
@ThemePreviews
@Composable
private fun MeetTaffyPreview() {
    TaffyPreview(darkTheme = false) {
        MeetTaffyContent(
            onIntent = {},
            soundEnabled = true,
            onSoundEnabledChange = {},
        )
    }
}

@ThemePreviews
@Composable
private fun MeetTaffyDarkPreview() {
    TaffyPreview(darkTheme = true) {
        MeetTaffyContent(
            onIntent = {},
            soundEnabled = false,
            onSoundEnabledChange = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun MeetTaffyScaledPreview() {
    TaffyPreview(darkTheme = false) {
        MeetTaffyContent(
            onIntent = {},
            soundEnabled = false,
            onSoundEnabledChange = {},
        )
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TabletPreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/**
 * Debug-only Compose previews; never part of the product APK.
 *
 * There is no screenshot harness in this repository, so these are the only
 * light/dark artifact SCR-004 has. They are kept deliberately wide of each
 * other: nothing chosen is the state a person actually arrives in, the chosen
 * state is the one that proves fill means selection and nothing else, and the
 * scaled and tablet previews are where a band with a figure standing in it and
 * a card whose glyph, title and mark are on three separate baselines go wrong
 * if they are going to.
 */
@ThemePreviews
@Composable
private fun AiSetupPreview() {
    TaffyPreview(darkTheme = false) {
        AiSetupContent(state = AiSetupUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun AiSetupChosenPreview() {
    TaffyPreview(darkTheme = true) {
        AiSetupContent(
            state = AiSetupUiState(route = ProviderRoute.DIRECT_WITH_YOUR_KEY),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun AiSetupScaledPreview() {
    TaffyPreview(darkTheme = false) {
        AiSetupContent(
            state = AiSetupUiState(route = ProviderRoute.DIRECT_WITH_YOUR_KEY),
            onIntent = {},
        )
    }
}

@TabletPreviews
@Composable
private fun AiSetupTabletPreview() {
    TaffyPreview(darkTheme = false) {
        AiSetupContent(state = AiSetupUiState(), onIntent = {})
    }
}

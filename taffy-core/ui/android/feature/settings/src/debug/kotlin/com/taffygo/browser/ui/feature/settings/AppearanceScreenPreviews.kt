// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.ThemePreviews
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun AppearancePreview() {
    TaffyPreview(darkTheme = false) {
        AppearanceContent(state = AppearanceUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun AppearanceDarkPreview() {
    TaffyPreview(darkTheme = true) {
        AppearanceContent(
            state = AppearanceUiState(theme = ThemePreference.DARK, pseudoLocalization = true),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun AppearanceScaledPreview() {
    TaffyPreview(darkTheme = false) {
        AppearanceContent(state = AppearanceUiState(theme = ThemePreference.LIGHT), onIntent = {})
    }
}

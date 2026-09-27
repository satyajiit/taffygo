// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

@ThemePreviews
@Composable
private fun SiteSettingsEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        SiteSettingsContent(state = SiteSettingsUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SiteSettingsUnavailablePreview() {
    TaffyPreview(darkTheme = true) {
        SiteSettingsContent(state = SiteSettingsUiState(available = false), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun SiteSettingsListPreview() {
    TaffyPreview(darkTheme = false) {
        SiteSettingsContent(
            state = SiteSettingsUiState(
                defaults = listOf(
                    SiteSettingsUiState.Default(
                        SiteSettingsRepository.Capability.CAMERA,
                        enabled = true,
                        userModifiable = true,
                    ),
                    SiteSettingsUiState.Default(
                        SiteSettingsRepository.Capability.POP_UPS,
                        enabled = false,
                        userModifiable = true,
                    ),
                ),
                sites = listOf(
                    SiteSettingsUiState.Site("news.example.test", 1),
                ),
            ),
            onIntent = {},
        )
    }
}

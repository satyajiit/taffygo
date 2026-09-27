// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LanguageRegionPolicy

/** Screen SCR-407's pure half: country never wipes a still-valid language. */
internal fun reduceAppearance(
    state: AppearanceUiState,
    intent: AppearanceIntent,
): AppearanceUiState =
    when (intent) {
        is AppearanceIntent.SelectTab -> state.copy(tab = intent.tab)
        is AppearanceIntent.ChooseTheme -> state.copy(theme = intent.theme)
        is AppearanceIntent.ChooseLanguage -> state.copy(
            appLanguage = LanguageRegionPolicy.coerce(intent.language, state.regionCode),
        )
        AppearanceIntent.OpenRegionPicker -> state.copy(regionPickerVisible = true)
        AppearanceIntent.CloseRegionPicker -> state.copy(
            regionPickerVisible = false,
            regionSearchQuery = "",
        )
        is AppearanceIntent.RegionSearchChanged -> state.copy(regionSearchQuery = intent.query)
        is AppearanceIntent.ChooseRegion -> {
            val region = LanguageRegionPolicy.normalizedRegionCode(intent.regionCode)
                ?: return state
            state.copy(
                regionCode = region,
                appLanguage = LanguageRegionPolicy.coerce(state.appLanguage, region),
                regionPickerVisible = false,
                regionSearchQuery = "",
            )
        }
        AppearanceIntent.TogglePseudoLocalization ->
            state.copy(pseudoLocalization = !state.pseudoLocalization)
        AppearanceIntent.ToggleForceDarkWeb ->
            state.copy(forceDarkWeb = !state.forceDarkWeb)
    }

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.ThemePreference

/** Everything screen SCR-407 can be asked to do. */
sealed interface AppearanceIntent {

    /** Switch Theme / Language. */
    data class SelectTab(val tab: AppearanceTab) : AppearanceIntent

    /** Choose a theme. */
    data class ChooseTheme(val theme: ThemePreference) : AppearanceIntent

    /** Choose the app's language. */
    data class ChooseLanguage(val language: AppLanguage) : AppearanceIntent

    /** Open the searchable country sheet. */
    data object OpenRegionPicker : AppearanceIntent

    /** Dismiss the searchable country sheet. */
    data object CloseRegionPicker : AppearanceIntent

    /** Narrow the country list as the person types. */
    data class RegionSearchChanged(val query: String) : AppearanceIntent

    /** Choose the country used for search, prices and units. */
    data class ChooseRegion(val regionCode: String) : AppearanceIntent

    /** Turn the pseudo-localization variant on or off. */
    data object TogglePseudoLocalization : AppearanceIntent

    /** Ask sites without a dark look to draw dark. */
    data object ToggleForceDarkWeb : AppearanceIntent
}

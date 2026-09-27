// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.ThemePreference

/**
 * Screen SCR-407 — Appearance.
 *
 * Theme and language sit on two tabs. [pseudoLocalization] is the PAR-L10N-001
 * check: stretch TaffyGo's own labels so missing translations and overflow
 * show up. [forceDarkWeb] asks pages without a dark look to draw dark when
 * TaffyGo itself is dark.
 *
 * Country and language are independent. [regionCode] is the ISO country for
 * local defaults; it does not hide Hindi or SYSTEM.
 */
data class AppearanceUiState(
    /** Theme or language. Local to this screen, not a preference. */
    val tab: AppearanceTab = AppearanceTab.THEME,
    /** Which theme to draw. */
    val theme: ThemePreference = ThemePreference.SYSTEM,
    /** Which language to draw in. */
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    /** ISO country code for search, prices and units. */
    val regionCode: String = "IN",
    /** Whether every string is shown pseudo-localized. */
    val pseudoLocalization: Boolean = false,
    /**
     * Whether pages without a dark look should be drawn dark when TaffyGo
     * itself is dark.
     */
    val forceDarkWeb: Boolean = false,
    /** Whether the complete country picker is open. */
    val regionPickerVisible: Boolean = false,
    /** The country-list filter inside the picker. */
    val regionSearchQuery: String = "",
)

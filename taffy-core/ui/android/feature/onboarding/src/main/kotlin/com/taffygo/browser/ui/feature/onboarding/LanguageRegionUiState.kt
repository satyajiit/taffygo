// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.AppLanguage

/**
 * Screen SCR-006 — Language and region.
 *
 * Both the country and app language are real stored choices. Country and
 * language are independent: English and Hindi are offered everywhere, and
 * SYSTEM follows the device when TaffyGo has words for that language.
 */
data class LanguageRegionUiState(
    /** Which language the interface is drawn in. */
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    /** ISO 3166-1 alpha-2 country or region. */
    val regionCode: String = "IN",
    /** The language-list filter. */
    val searchQuery: String = "",
    /** Whether the complete country picker is open. */
    val regionPickerVisible: Boolean = false,
    /** The country-list filter inside the picker. */
    val regionSearchQuery: String = "",
)

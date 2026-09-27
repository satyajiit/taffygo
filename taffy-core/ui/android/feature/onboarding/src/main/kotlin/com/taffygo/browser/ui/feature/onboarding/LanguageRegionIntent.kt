// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.AppLanguage

/** Everything screen SCR-006 can be asked to do. */
sealed interface LanguageRegionIntent {

    /** Filter the languages shown by the chooser. */
    data class SearchChanged(val query: String) : LanguageRegionIntent

    /** Open the complete country and region list. */
    data object OpenRegionPicker : LanguageRegionIntent

    /** Close the country and region list without changing the choice. */
    data object CloseRegionPicker : LanguageRegionIntent

    /** Filter the country and region list. */
    data class RegionSearchChanged(val query: String) : LanguageRegionIntent

    /** Use this ISO country code. A still-valid language is kept. */
    data class ChooseRegion(val regionCode: String) : LanguageRegionIntent

    /** Draw the interface in this language. */
    data class ChooseLanguage(val language: AppLanguage) : LanguageRegionIntent

    /**
     * Leave. There is no separate save: the language applies the moment it is
     * chosen, so this button confirms rather than commits, which is what the
     * mock's Save means here.
     */
    data object Done : LanguageRegionIntent
}

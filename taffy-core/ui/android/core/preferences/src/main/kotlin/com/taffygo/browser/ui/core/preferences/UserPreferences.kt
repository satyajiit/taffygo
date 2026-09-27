// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.preferences

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference

/**
 * UI-safe preferences projected by the browser profile owner.
 */
data class UserPreferences(
    /** Which theme to draw (screen SCR-407). */
    val theme: ThemePreference = ThemePreference.SYSTEM,
    /** Which language to draw in (screen SCR-407). */
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    /** ISO 3166-1 alpha-2 region used for local search, prices and units. */
    val regionCode: String = DEFAULT_REGION_CODE,
    /** Whether every string is shown pseudo-localized (parity row PAR-L10N-001). */
    val pseudoLocalization: Boolean = false,
    /**
     * Whether pages without a dark look should be drawn dark when TaffyGo
     * itself is dark.
     */
    val forceDarkWeb: Boolean = false,
    /** Where model requests go (screen SCR-404). */
    val providerRoute: ProviderRoute = ProviderRoute.NOT_CONFIGURED,
    /** Which notification topics the person enabled (screen SCR-406). */
    val notificationTopics: Set<NotificationTopic> = setOf(NotificationTopic.TASK_PROGRESS),
    /** Whether the first-run sequence (SCR-001…SCR-004) has finished. */
    val onboardingCompleted: Boolean = false,
    /**
     * Whether the composer offers a suggestion while a person types (screen
     * SCR-405, decision
     * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`).
     *
     * Off until the person chooses it, and the default is the whole of the
     * protection: a suggestion is a model call made from what somebody is
     * halfway through typing, so nobody may be opted into it by a default.
     */
    val composerSuggestions: Boolean = false,
    /**
     * Whether these values came from disk. The flow starts with the compiled-in
     * defaults so the first frame never waits, and a default is indistinguishable
     * from a stored choice without this flag. The language application reads it:
     * applying the placeholder default would reset a stored language and then
     * re-apply it, recreating the activity twice at startup.
     */
    val loaded: Boolean = false,
) {
    init {
        require(LanguageRegionPolicy.normalizedRegionCode(regionCode) == regionCode) {
            "The selected region must be a normalized ISO country code"
        }
        require(LanguageRegionPolicy.isAvailable(appLanguage, regionCode)) {
            "The app language must be available in the selected region"
        }
    }
}

/** The first-run region until the user chooses another country. */
const val DEFAULT_REGION_CODE: String = "IN"

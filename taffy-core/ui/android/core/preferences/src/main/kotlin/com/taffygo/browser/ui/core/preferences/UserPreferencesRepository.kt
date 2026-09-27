// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.preferences

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import kotlinx.coroutines.flow.StateFlow

/**
 * The interface preferences, read once and kept current.
 *
 * The value is a `StateFlow` with a browser-projected initial value, so the
 * first frame never performs storage work. Canonical preference state and
 * persistence remain behind the profile Core API.
 */
interface UserPreferencesRepository {

    /** The preferences as they are now. */
    val preferences: StateFlow<UserPreferences>

    /** Choose a theme. */
    suspend fun setTheme(theme: ThemePreference)

    /** Choose the app's language when it is available in the selected region. */
    suspend fun setAppLanguage(language: AppLanguage)

    /**
     * Choose the country used for local defaults. A still-valid language is
     * kept; country and language are independent.
     */
    suspend fun setRegionCode(regionCode: String)

    /** Turn the pseudo-localization variant on or off. */
    suspend fun setPseudoLocalization(enabled: Boolean)

    /** Ask sites without a dark look to draw dark. */
    suspend fun setForceDarkWeb(enabled: Boolean)

    /** Choose where model requests go. */
    suspend fun setProviderRoute(route: ProviderRoute)

    /** Turn one notification topic on or off. */
    suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean)

    /** Record that the first-run sequence finished. */
    suspend fun setOnboardingCompleted(completed: Boolean)

    /**
     * Let the composer offer a suggestion while a person types, or stop it.
     *
     * Turning it off stops the requests as well as the ghost text: there is
     * nothing to keep asking for once nothing is drawn.
     */
    suspend fun setComposerSuggestions(enabled: Boolean)
}

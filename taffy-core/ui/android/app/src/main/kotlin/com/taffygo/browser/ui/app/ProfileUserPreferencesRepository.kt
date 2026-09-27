// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.preferences.DEFAULT_REGION_CODE
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Profile-scoped UI preference projection backed only by Chromium PrefService.
 *
 * [fallbackRegion] is what a profile that has never chosen one starts on. It is
 * a parameter rather than a constant so the Android detection in
 * `DeviceRegion.kt` can supply it, and so this class stays a pure projection
 * over the store that a host test can construct with no device behind it. The
 * default is the fixed one, which is what the detection itself falls back to.
 */
class ProfileUserPreferencesRepository(
    private val store: ProfilePreferenceStore,
    private val fallbackRegion: String = DEFAULT_REGION_CODE,
) : UserPreferencesRepository {
    private val current = MutableStateFlow(read())

    override val preferences: StateFlow<UserPreferences> = current.asStateFlow()

    override suspend fun setTheme(theme: ThemePreference) {
        store.putString(ProfilePreferenceNames.THEME, theme.name)
        current.value = current.value.copy(theme = theme)
    }

    override suspend fun setAppLanguage(language: AppLanguage) {
        val accepted = LanguageRegionPolicy.coerce(language, current.value.regionCode)
        store.putString(ProfilePreferenceNames.APP_LANGUAGE, accepted.name)
        current.value = current.value.copy(appLanguage = accepted)
    }

    override suspend fun setRegionCode(regionCode: String) {
        val normalized = LanguageRegionPolicy.normalizedRegionCode(regionCode)
            ?: throw IllegalArgumentException("Region must be an ISO alpha-2 country code")
        val kept = LanguageRegionPolicy.coerce(current.value.appLanguage, normalized)
        store.putString(ProfilePreferenceNames.REGION_CODE, normalized)
        if (kept != current.value.appLanguage) {
            store.putString(ProfilePreferenceNames.APP_LANGUAGE, kept.name)
        }
        current.value = current.value.copy(
            appLanguage = kept,
            regionCode = normalized,
        )
    }

    override suspend fun setPseudoLocalization(enabled: Boolean) {
        store.putBoolean(ProfilePreferenceNames.PSEUDO_LOCALIZATION, enabled)
        current.value = current.value.copy(pseudoLocalization = enabled)
    }

    override suspend fun setForceDarkWeb(enabled: Boolean) {
        store.putBoolean(ProfilePreferenceNames.FORCE_DARK_WEB, enabled)
        current.value = current.value.copy(forceDarkWeb = enabled)
    }

    override suspend fun setProviderRoute(route: ProviderRoute) {
        store.putString(ProfilePreferenceNames.PROVIDER_ROUTE, route.name)
        current.value = current.value.copy(providerRoute = route)
    }

    override suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean) {
        val updated = current.value.notificationTopics.toMutableSet().apply {
            if (enabled) add(topic) else remove(topic)
        }.toSet()
        store.putString(
            ProfilePreferenceNames.NOTIFICATION_TOPICS,
            updated.sortedBy(NotificationTopic::ordinal).joinToString(",") { it.name },
        )
        current.value = current.value.copy(notificationTopics = updated)
    }

    override suspend fun setOnboardingCompleted(completed: Boolean) {
        store.putBoolean(ProfilePreferenceNames.ONBOARDING_COMPLETED, completed)
        current.value = current.value.copy(onboardingCompleted = completed)
    }

    override suspend fun setComposerSuggestions(enabled: Boolean) {
        store.putBoolean(ProfilePreferenceNames.COMPOSER_SUGGESTIONS, enabled)
        current.value = current.value.copy(composerSuggestions = enabled)
    }

    private fun read(): UserPreferences {
        val region = LanguageRegionPolicy.normalizedRegionCode(
            store.getString(ProfilePreferenceNames.REGION_CODE),
        ) ?: fallbackRegion
        val language = LanguageRegionPolicy.coerce(storedLanguage(), region)
        return UserPreferences(
            theme = enumValueOrDefault(
                store.getString(ProfilePreferenceNames.THEME),
                ThemePreference.SYSTEM,
            ),
            appLanguage = language,
            regionCode = region,
            pseudoLocalization = store.getBoolean(ProfilePreferenceNames.PSEUDO_LOCALIZATION),
            forceDarkWeb = store.getBoolean(ProfilePreferenceNames.FORCE_DARK_WEB),
            providerRoute = enumValueOrDefault(
                store.getString(ProfilePreferenceNames.PROVIDER_ROUTE),
                ProviderRoute.NOT_CONFIGURED,
            ),
            notificationTopics = store.getString(ProfilePreferenceNames.NOTIFICATION_TOPICS)
                .split(',')
                .mapNotNull { encoded ->
                    NotificationTopic.entries.firstOrNull { it.name == encoded }
                }
                .toSet(),
            onboardingCompleted = store.getBoolean(ProfilePreferenceNames.ONBOARDING_COMPLETED),
            composerSuggestions = store.getBoolean(ProfilePreferenceNames.COMPOSER_SUGGESTIONS),
            loaded = true,
        )
    }

    /**
     * An unset language follows the device. A corrupt stored name fails closed
     * to English rather than to a device-dependent answer.
     */
    private fun storedLanguage(): AppLanguage {
        val encoded = store.getString(ProfilePreferenceNames.APP_LANGUAGE)
        if (encoded.isEmpty()) return AppLanguage.SYSTEM
        return enumValueOrDefault(encoded, AppLanguage.ENGLISH)
    }

    private inline fun <reified T : Enum<T>> enumValueOrDefault(encoded: String, default: T): T =
        enumValues<T>().firstOrNull { it.name == encoded } ?: default
}

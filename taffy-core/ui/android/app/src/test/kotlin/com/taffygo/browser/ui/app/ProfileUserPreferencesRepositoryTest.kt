// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileUserPreferencesRepositoryTest {
    @Test
    fun `profile values are loaded and every mutation is persisted`() = runTest {
        val store = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.THEME, ThemePreference.DARK.name)
            putString(ProfilePreferenceNames.APP_LANGUAGE, AppLanguage.ENGLISH.name)
            putString(ProfilePreferenceNames.REGION_CODE, "US")
            putString(
                ProfilePreferenceNames.PROVIDER_ROUTE,
                ProviderRoute.DIRECT_WITH_YOUR_KEY.name,
            )
            putString(
                ProfilePreferenceNames.NOTIFICATION_TOPICS,
                NotificationTopic.TASK_PROGRESS.name,
            )
        }
        val repository = ProfileUserPreferencesRepository(store)

        assertTrue(repository.preferences.value.loaded)
        assertEquals(ThemePreference.DARK, repository.preferences.value.theme)
        assertEquals("US", repository.preferences.value.regionCode)

        repository.setTheme(ThemePreference.LIGHT)
        repository.setNotificationTopic(NotificationTopic.DOWNLOADS, true)
        repository.setForceDarkWeb(true)
        repository.setComposerSuggestions(true)

        // The composer's own setting reaches the same store under its own name
        // (decision 0097): the projection and the pref have to agree, or the
        // switch comes back off on the next start.
        assertTrue(store.getBoolean(ProfilePreferenceNames.COMPOSER_SUGGESTIONS))
        assertTrue(repository.preferences.value.composerSuggestions)

        assertTrue(store.getBoolean(ProfilePreferenceNames.FORCE_DARK_WEB))
        assertTrue(repository.preferences.value.forceDarkWeb)

        assertEquals(ThemePreference.LIGHT.name, store.getString(ProfilePreferenceNames.THEME))
        assertEquals(
            setOf(NotificationTopic.TASK_PROGRESS, NotificationTopic.DOWNLOADS),
            repository.preferences.value.notificationTopics,
        )
    }

    @Test
    fun `changing country keeps a still-valid language`() = runTest {
        val store = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.APP_LANGUAGE, AppLanguage.HINDI.name)
            putString(ProfilePreferenceNames.REGION_CODE, "IN")
        }
        val repository = ProfileUserPreferencesRepository(store)

        repository.setRegionCode("US")

        assertEquals("US", repository.preferences.value.regionCode)
        assertEquals(AppLanguage.HINDI, repository.preferences.value.appLanguage)
        assertEquals(
            AppLanguage.HINDI.name,
            store.getString(ProfilePreferenceNames.APP_LANGUAGE),
        )
    }

    @Test
    fun `system language survives a country change`() = runTest {
        val store = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.APP_LANGUAGE, AppLanguage.SYSTEM.name)
            putString(ProfilePreferenceNames.REGION_CODE, "IN")
        }
        val repository = ProfileUserPreferencesRepository(store)

        repository.setRegionCode("jp")

        assertEquals("JP", repository.preferences.value.regionCode)
        assertEquals(AppLanguage.SYSTEM, repository.preferences.value.appLanguage)
    }

    @Test
    fun `an unset language follows the device`() {
        val loaded = ProfileUserPreferencesRepository(MemoryPreferenceStore()).preferences.value
        assertEquals(AppLanguage.SYSTEM, loaded.appLanguage)
    }

    @Test
    fun `hindi and system can be stored outside India`() = runTest {
        val store = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.APP_LANGUAGE, AppLanguage.ENGLISH.name)
            putString(ProfilePreferenceNames.REGION_CODE, "US")
        }
        val repository = ProfileUserPreferencesRepository(store)

        repository.setAppLanguage(AppLanguage.HINDI)
        assertEquals(AppLanguage.HINDI, repository.preferences.value.appLanguage)

        repository.setRegionCode("JP")
        repository.setAppLanguage(AppLanguage.SYSTEM)
        assertEquals("JP", repository.preferences.value.regionCode)
        assertEquals(AppLanguage.SYSTEM, repository.preferences.value.appLanguage)
    }

    @Test
    fun `corrupt profile values fail closed to portable defaults`() {
        val store = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.THEME, "unknown")
            putString(ProfilePreferenceNames.APP_LANGUAGE, "unknown")
            putString(ProfilePreferenceNames.REGION_CODE, "not-a-region")
            putString(ProfilePreferenceNames.PROVIDER_ROUTE, "unknown")
            putString(ProfilePreferenceNames.NOTIFICATION_TOPICS, "unknown")
        }

        val loaded = ProfileUserPreferencesRepository(store).preferences.value

        assertEquals(ThemePreference.SYSTEM, loaded.theme)
        assertEquals(AppLanguage.ENGLISH, loaded.appLanguage)
        assertEquals(ProviderRoute.NOT_CONFIGURED, loaded.providerRoute)
        assertTrue(loaded.notificationTopics.isEmpty())
    }

    /**
     * A withdrawn route reads as no route, and the literal is the point.
     *
     * "MANAGED" is not a corrupt value — this product wrote it, on phones,
     * for as long as the managed route was offered. The route was withdrawn
     * with the service behind it, so the honest reading of a stored one is
     * that nothing is configured rather than that something is. Spelling it
     * out rather than leaning on the test above keeps the case named after
     * the enum member is gone and nothing else can name it.
     */
    @Test
    fun `a route this build no longer has reads as none`() {
        val store = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.PROVIDER_ROUTE, "MANAGED")
        }

        assertEquals(
            ProviderRoute.NOT_CONFIGURED,
            ProfileUserPreferencesRepository(store).preferences.value.providerRoute,
        )
    }

    private class MemoryPreferenceStore : ProfilePreferenceStore {
        private val strings = mutableMapOf<String, String>()
        private val booleans = mutableMapOf<String, Boolean>()

        override fun getString(name: String): String = strings[name].orEmpty()

        override fun putString(name: String, value: String) {
            strings[name] = value
        }

        override fun getBoolean(name: String): Boolean = booleans[name] ?: false

        override fun putBoolean(name: String, value: Boolean) {
            booleans[name] = value
        }
    }
}

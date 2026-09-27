// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

/**
 * Where the UI host opens.
 *
 * The decision is made once, from stored state, and it has three answers: the
 * first-run sequence, the browser, or wherever a killed process left the user.
 * Getting it wrong is not a cosmetic bug — it either hides the welcome screen
 * from someone who has never seen it, or shows setup to someone who finished
 * it — so each answer is asserted rather than assumed.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ShellViewModelTest {

    private val preferences = FakePreferences()

    @Before
    fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `a first run opens on the welcome screen`() {
        val shell = ShellViewModel(preferences, SavedStateHandle())

        preferences.emit(UserPreferences(onboardingCompleted = false, loaded = true))

        assertEquals(
            BackStack.start(TaffyDestination.OnboardingWelcome),
            shell.state.value.backStack,
        )
    }

    @Test
    fun `a device that finished the sequence opens on the browser`() {
        val shell = ShellViewModel(preferences, SavedStateHandle())

        preferences.emit(UserPreferences(onboardingCompleted = true, loaded = true))

        assertEquals(BackStack.start(), shell.state.value.backStack)
    }

    @Test
    fun `nothing moves before the stored answer arrives`() {
        val shell = ShellViewModel(preferences, SavedStateHandle())

        preferences.emit(UserPreferences(onboardingCompleted = false, loaded = false))

        assertEquals(BackStack.start(), shell.state.value.backStack)
    }

    @Test
    fun `the shell carries the country beside the language`() {
        val shell = ShellViewModel(preferences, SavedStateHandle())

        preferences.emit(
            UserPreferences(
                appLanguage = AppLanguage.ENGLISH,
                regionCode = "US",
                loaded = true,
            ),
        )

        assertEquals("US", shell.state.value.regionCode)
        assertEquals(AppLanguage.ENGLISH, shell.state.value.appLanguage)
    }

    @Test
    fun `a saved stack is where the user was, not where the sequence starts`() {
        val saved = SavedStateHandle(
            mapOf(BACK_STACK_KEY to arrayListOf(TaffyDestination.Appearance.route)),
        )
        val shell = ShellViewModel(preferences, saved)

        preferences.emit(UserPreferences(onboardingCompleted = false, loaded = true))

        assertEquals(
            BackStack.start(TaffyDestination.Appearance),
            shell.state.value.backStack,
        )
    }

    /** The preferences, under the test's control rather than a disk's. */
    private class FakePreferences : UserPreferencesRepository {

        private val stored = MutableStateFlow(UserPreferences())

        override val preferences: StateFlow<UserPreferences> = stored.asStateFlow()

        /** Hand the shell what disk would have handed it. */
        fun emit(value: UserPreferences) {
            stored.value = value
        }

        override suspend fun setTheme(theme: ThemePreference) {
            stored.value = stored.value.copy(theme = theme)
        }

        override suspend fun setAppLanguage(language: AppLanguage) {
            stored.value = stored.value.copy(
                appLanguage = LanguageRegionPolicy.coerce(language, stored.value.regionCode),
            )
        }

        override suspend fun setRegionCode(regionCode: String) {
            val normalized = LanguageRegionPolicy.normalizedRegionCode(regionCode) ?: return
            stored.value = stored.value.copy(
                appLanguage = LanguageRegionPolicy.coerce(stored.value.appLanguage, normalized),
                regionCode = normalized,
            )
        }

        override suspend fun setPseudoLocalization(enabled: Boolean) {
            stored.value = stored.value.copy(pseudoLocalization = enabled)
        }

        override suspend fun setForceDarkWeb(enabled: Boolean) {
            stored.value = stored.value.copy(forceDarkWeb = enabled)
        }

        override suspend fun setProviderRoute(route: ProviderRoute) {
            stored.value = stored.value.copy(providerRoute = route)
        }

        override suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean) {
            val topics = stored.value.notificationTopics
            stored.value = stored.value.copy(
                notificationTopics = if (enabled) topics + topic else topics - topic,
            )
        }

        override suspend fun setOnboardingCompleted(completed: Boolean) {
            stored.value = stored.value.copy(onboardingCompleted = completed)
        }

        override suspend fun setComposerSuggestions(enabled: Boolean) {
            stored.value = stored.value.copy(composerSuggestions = enabled)
        }
    }

    private companion object {
        const val BACK_STACK_KEY = "shell_back_stack"
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-405's reducer leaves the hub to the navigator, and its one switch
 * to the store.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TaffySettingsReducerTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `opening a child leaves the hub state as it is`() {
        val state = TaffySettingsUiState()
        assertEquals(state, reduceTaffySettings(state, TaffySettingsIntent.OpenPersonality))
        assertEquals(state, reduceTaffySettings(state, TaffySettingsIntent.OpenSkills))
        assertEquals(state, reduceTaffySettings(state, TaffySettingsIntent.OpenAiProviders))
    }

    @Test
    fun `turning suggestions on writes the stored preference`() = runTest(dispatcher) {
        val stored = StoredPreferences(UserPreferences(composerSuggestions = false))
        val viewModel = TaffySettingsViewModel(stored, NoAnalytics())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(TaffySettingsIntent.ToggleComposerSuggestions, RecordingNavigator())
        runCurrent()

        assertTrue(stored.preferences.value.composerSuggestions)
        assertTrue(viewModel.state.value.composerSuggestions)
    }

    @Test
    fun `the switch is drawn from what is stored, never from what was pressed`() {
        val stored = StoredPreferences(UserPreferences(composerSuggestions = true))
        val viewModel = TaffySettingsViewModel(stored, NoAnalytics())

        assertEquals(TaffySettingsUiState(composerSuggestions = true), viewModel.state.value)
        // The reducer leaves it alone: only a stored value moves the switch.
        assertEquals(
            viewModel.state.value,
            reduceTaffySettings(
                viewModel.state.value,
                TaffySettingsIntent.ToggleComposerSuggestions,
            ),
        )
    }

    @Test
    fun `opening a child goes to that destination`() {
        val navigator = RecordingNavigator()
        val viewModel = TaffySettingsViewModel(
            StoredPreferences(UserPreferences()),
            NoAnalytics(),
        )

        viewModel.onIntent(TaffySettingsIntent.OpenPersonality, navigator)
        viewModel.onIntent(TaffySettingsIntent.OpenSkills, navigator)
        viewModel.onIntent(TaffySettingsIntent.OpenAiProviders, navigator)

        // The AI row opens SCR-419 and not the hub: it means "my providers",
        // and SCR-419 hands over to the hub by itself when there are none.
        assertEquals(
            listOf(
                TaffyDestination.Personality,
                TaffyDestination.SkillsList,
                TaffyDestination.ConnectedProviders,
            ),
            navigator.opened,
        )
    }

    private class RecordingNavigator : TaffyNavigator {
        val opened = mutableListOf<TaffyDestination>()
        override fun goTo(destination: TaffyDestination) {
            opened += destination
        }
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack(): Boolean = false
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    /** The preferences, with the one value this screen can move. */
    private class StoredPreferences(stored: UserPreferences) : UserPreferencesRepository {
        override val preferences = MutableStateFlow(stored)
        override suspend fun setTheme(theme: ThemePreference) = Unit
        override suspend fun setAppLanguage(language: AppLanguage) = Unit
        override suspend fun setRegionCode(regionCode: String) = Unit
        override suspend fun setPseudoLocalization(enabled: Boolean) = Unit
        override suspend fun setForceDarkWeb(enabled: Boolean) = Unit
        override suspend fun setProviderRoute(route: ProviderRoute) = Unit
        override suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean) = Unit
        override suspend fun setOnboardingCompleted(completed: Boolean) = Unit

        override suspend fun setComposerSuggestions(enabled: Boolean) {
            preferences.value = preferences.value.copy(composerSuggestions = enabled)
        }
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

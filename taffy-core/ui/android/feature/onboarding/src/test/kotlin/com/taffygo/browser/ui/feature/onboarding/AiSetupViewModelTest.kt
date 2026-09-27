// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-004 finishes two ways, and neither may finish as the other.
 *
 * The direct route must continue into provider setup instead of losing Own
 * key; setting up later must complete the first run with no provider at all
 * and leave nothing behind that claims one is coming.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class AiSetupViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `finishing own key setup opens providers above the browser`() = runTest(dispatcher) {
        val preferences = FakePreferences(ProviderRoute.DIRECT_WITH_YOUR_KEY)
        val navigator = RecordingNavigator()
        val viewModel = AiSetupViewModel(preferences, NoAnalytics())

        viewModel.onIntent(AiSetupIntent.Finish, navigator)
        runCurrent()

        assertTrue(preferences.preferences.value.onboardingCompleted)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.restarted)
        assertEquals(listOf(TaffyDestination.AiAndProviders), navigator.visited)
    }

    @Test
    fun `setting up later completes the first run with no provider`() = runTest(dispatcher) {
        val preferences = FakePreferences(ProviderRoute.NOT_CONFIGURED)
        val navigator = RecordingNavigator()
        val viewModel = AiSetupViewModel(preferences, NoAnalytics())

        viewModel.onIntent(AiSetupIntent.SetUpLater, navigator)
        runCurrent()

        assertTrue(preferences.preferences.value.onboardingCompleted)
        assertEquals(
            ProviderRoute.NOT_CONFIGURED,
            preferences.preferences.value.providerRoute,
        )
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.restarted)
        assertTrue(navigator.visited.isEmpty())
    }

    /**
     * Choosing the key route writes it immediately, so skipping afterwards has
     * a stored answer to undo. Leaving it would hand the browser a route whose
     * setup the person declined to do.
     */
    @Test
    fun `setting up later clears a route chosen a moment earlier`() = runTest(dispatcher) {
        val preferences = FakePreferences(ProviderRoute.NOT_CONFIGURED)
        val navigator = RecordingNavigator()
        val viewModel = AiSetupViewModel(preferences, NoAnalytics())

        viewModel.onIntent(
            AiSetupIntent.ChooseRoute(ProviderRoute.DIRECT_WITH_YOUR_KEY),
            navigator,
        )
        runCurrent()
        viewModel.onIntent(AiSetupIntent.SetUpLater, navigator)
        runCurrent()

        assertEquals(
            ProviderRoute.NOT_CONFIGURED,
            preferences.preferences.value.providerRoute,
        )
        assertTrue(preferences.preferences.value.onboardingCompleted)
        assertTrue(navigator.visited.isEmpty())
    }

    @Test
    fun `fast own key choice cannot finish with the previous route`() = runTest(dispatcher) {
        val preferences = FakePreferences(ProviderRoute.NOT_CONFIGURED)
        val navigator = RecordingNavigator()
        val viewModel = AiSetupViewModel(preferences, NoAnalytics())

        viewModel.onIntent(
            AiSetupIntent.ChooseRoute(ProviderRoute.DIRECT_WITH_YOUR_KEY),
            navigator,
        )
        viewModel.onIntent(AiSetupIntent.Finish, navigator)
        runCurrent()

        assertEquals(
            ProviderRoute.DIRECT_WITH_YOUR_KEY,
            preferences.preferences.value.providerRoute,
        )
        assertEquals(listOf(TaffyDestination.AiAndProviders), navigator.visited)
    }

    @Test
    fun `finishing freezes the screen before another tap can change it`() = runTest(dispatcher) {
        val preferences = FakePreferences(ProviderRoute.DIRECT_WITH_YOUR_KEY)
        val navigator = RecordingNavigator()
        val viewModel = AiSetupViewModel(preferences, NoAnalytics())

        viewModel.onIntent(AiSetupIntent.Finish, navigator)
        viewModel.onIntent(AiSetupIntent.SetUpLater, navigator)
        runCurrent()

        assertEquals(
            ProviderRoute.DIRECT_WITH_YOUR_KEY,
            preferences.preferences.value.providerRoute,
        )
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.restarted)
        assertEquals(listOf(TaffyDestination.AiAndProviders), navigator.visited)
    }

    @Test
    fun `finishing without a route does nothing`() = runTest(dispatcher) {
        val preferences = FakePreferences(ProviderRoute.NOT_CONFIGURED)
        val navigator = RecordingNavigator()
        val viewModel = AiSetupViewModel(preferences, NoAnalytics())

        viewModel.onIntent(AiSetupIntent.Finish, navigator)
        runCurrent()

        assertTrue(navigator.replaced.isEmpty())
        assertTrue(navigator.restarted.isEmpty())
        assertFalse(preferences.preferences.value.onboardingCompleted)
    }

    private class FakePreferences(route: ProviderRoute) : UserPreferencesRepository {
        private val stored = MutableStateFlow(UserPreferences(providerRoute = route))
        override val preferences: StateFlow<UserPreferences> = stored.asStateFlow()

        override suspend fun setTheme(theme: ThemePreference) = Unit
        override suspend fun setAppLanguage(language: AppLanguage) = Unit
        override suspend fun setRegionCode(regionCode: String) = Unit
        override suspend fun setPseudoLocalization(enabled: Boolean) = Unit
        override suspend fun setForceDarkWeb(enabled: Boolean) = Unit

        override suspend fun setProviderRoute(route: ProviderRoute) {
            stored.value = stored.value.copy(providerRoute = route)
        }

        override suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean) = Unit

        override suspend fun setOnboardingCompleted(completed: Boolean) {
            stored.value = stored.value.copy(onboardingCompleted = completed)
        }

        override suspend fun setComposerSuggestions(enabled: Boolean) = Unit
    }

    private class RecordingNavigator : TaffyNavigator {
        val visited = mutableListOf<TaffyDestination>()
        val restarted = mutableListOf<TaffyDestination>()
        val replaced = mutableListOf<TaffyDestination>()

        override fun goTo(destination: TaffyDestination) {
            visited += destination
        }

        override fun restart(destination: TaffyDestination) {
            restarted += destination
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            replaced += destination
        }

        override fun goBack(): Boolean = false
        override fun goHome() = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

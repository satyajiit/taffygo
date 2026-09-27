// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
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

/** Screen SCR-404 reads three published flows and navigates. */
@OptIn(ExperimentalCoroutinesApi::class)
class ProviderHubViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a roster republication reaches the screen with no reload gesture`() = runTest(dispatcher) {
        val roster = FakeRoster()
        val viewModel = viewModel(roster = roster)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        assertEquals(ProviderHubStatus.LOADING, viewModel.state.value.status)

        roster.publish(row("openrouter"), row("mine", origin = RosterProviderOrigin.CUSTOM))
        runCurrent()

        assertEquals(ProviderHubStatus.READY, viewModel.state.value.status)
        assertEquals(
            listOf(ProviderHubGroup.BRING_YOUR_OWN_KEY, ProviderHubGroup.YOUR_OWN_ENDPOINT),
            viewModel.state.value.sections.map { it.group },
        )
    }

    // The browser's own store is enough to take a row off the hub, without
    // waiting for the roster to echo it: the person has just supplied the key
    // and must not be offered the same thing to set up again.
    @Test
    fun `a browser handle arriving alone takes the row off the hub`() = runTest(dispatcher) {
        val roster = FakeRoster()
        val credentials = FakeCredentials()
        val viewModel = viewModel(roster = roster, credentials = credentials)
        backgroundScope.launch { viewModel.state.collect {} }
        roster.publish(row("openrouter"))
        runCurrent()
        assertEquals(
            ProviderHubGroup.BRING_YOUR_OWN_KEY,
            viewModel.state.value.rows.single().group,
        )

        credentials.hold("openrouter")
        runCurrent()

        assertEquals(emptyList<ProviderHubRow>(), viewModel.state.value.rows)
        assertEquals(ProviderHubStatus.ALL_CONNECTED, viewModel.state.value.status)
    }

    @Test
    fun `pressing a row goes where that row's own offer leads`() = runTest(dispatcher) {
        val roster = FakeRoster()
        val navigator = RecordingNavigator()
        val viewModel = viewModel(roster = roster)
        backgroundScope.launch { viewModel.state.collect {} }
        roster.publish(row("openrouter"), row("mine", origin = RosterProviderOrigin.CUSTOM))
        runCurrent()

        viewModel.state.value.rows.forEach {
            viewModel.onIntent(ProviderHubIntent.OpenRow(it), navigator)
        }

        assertEquals(
            listOf(
                TaffyDestination.ProviderConfig("openrouter"),
                TaffyDestination.CustomEndpointSetup("mine"),
            ),
            navigator.visited,
        )
    }

    @Test
    fun `pressing a blocked row goes nowhere at all`() = runTest(dispatcher) {
        val roster = FakeRoster()
        val navigator = RecordingNavigator()
        val viewModel = viewModel(roster = roster)
        backgroundScope.launch { viewModel.state.collect {} }
        roster.publish(row("held", enabled = false))
        runCurrent()

        viewModel.onIntent(ProviderHubIntent.OpenRow(viewModel.state.value.rows.single()), navigator)

        assertEquals(emptyList<TaffyDestination>(), navigator.visited)
    }

    @Test
    fun `adding a provider opens the endpoint screen with nothing named`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = viewModel()

        viewModel.onIntent(ProviderHubIntent.AddYourOwnProvider, navigator)

        assertEquals(listOf(TaffyDestination.CustomEndpointSetup()), navigator.visited)
    }

    @Test
    fun `choosing a tab shows that category and nowhere is navigated`() = runTest(dispatcher) {
        val roster = FakeRoster()
        val navigator = RecordingNavigator()
        val viewModel = viewModel(roster = roster)
        backgroundScope.launch { viewModel.state.collect {} }
        roster.publish(row("openrouter"), row("mine", origin = RosterProviderOrigin.CUSTOM))
        runCurrent()
        assertEquals(ProviderHubGroup.BRING_YOUR_OWN_KEY, viewModel.state.value.showing)

        viewModel.onIntent(
            ProviderHubIntent.ShowCategory(ProviderHubGroup.YOUR_OWN_ENDPOINT),
            navigator,
        )
        runCurrent()

        assertEquals(ProviderHubGroup.YOUR_OWN_ENDPOINT, viewModel.state.value.showing)
        assertEquals(
            listOf("mine"),
            viewModel.state.value.rowsIn(ProviderHubGroup.YOUR_OWN_ENDPOINT)
                .map { it.providerId },
        )
        // A tab is not a destination.
        assertEquals(emptyList<TaffyDestination>(), navigator.visited)
    }

    @Test
    fun `a republication does not move a person off the tab they are reading`() =
        runTest(dispatcher) {
            val roster = FakeRoster()
            val credentials = FakeCredentials()
            val viewModel = viewModel(roster = roster, credentials = credentials)
            backgroundScope.launch { viewModel.state.collect {} }
            roster.publish(row("openrouter"), row("mine", origin = RosterProviderOrigin.CUSTOM))
            runCurrent()
            viewModel.onIntent(
                ProviderHubIntent.ShowCategory(ProviderHubGroup.YOUR_OWN_ENDPOINT),
                RecordingNavigator(),
            )
            runCurrent()

            // A key saved on the provider's own page comes back as a whole new
            // roster, and the projection's opening tab would be the first one
            // still holding something.
            credentials.hold("openrouter")
            runCurrent()

            assertEquals(
                listOf("mine"),
                viewModel.state.value.rows.map { it.providerId },
            )
            assertEquals(ProviderHubGroup.YOUR_OWN_ENDPOINT, viewModel.state.value.showing)
        }

    @Test
    fun `an empty tab is a state the screen can be in and holds nothing`() = runTest(dispatcher) {
        val roster = FakeRoster()
        val viewModel = viewModel(roster = roster)
        backgroundScope.launch { viewModel.state.collect {} }
        roster.publish(row("openrouter"))
        runCurrent()

        viewModel.onIntent(
            ProviderHubIntent.ShowCategory(ProviderHubGroup.SUBSCRIPTION),
            RecordingNavigator(),
        )
        runCurrent()

        assertEquals(ProviderHubGroup.SUBSCRIPTION, viewModel.state.value.showing)
        assertEquals(0, viewModel.state.value.countIn(ProviderHubGroup.SUBSCRIPTION))
        assertTrue(viewModel.state.value.rowsIn(ProviderHubGroup.SUBSCRIPTION).isEmpty())
    }

    @Test
    fun `the screen records itself as SCR-404`() = runTest(dispatcher) {
        val analytics = RecordingAnalytics()
        viewModel(analytics = analytics).onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.AiAndProviders.screenId)),
            analytics.recorded,
        )
    }

    private fun viewModel(
        roster: ProviderRosterRepository = FakeRoster(),
        credentials: ProviderCredentialsRepository = FakeCredentials(),
        analytics: AnalyticsClient = RecordingAnalytics(),
    ) = ProviderHubViewModel(roster, credentials, analytics)

    private fun row(
        providerId: String,
        origin: RosterProviderOrigin = RosterProviderOrigin.CATALOG,
        enabled: Boolean = true,
    ): ProviderRosterRow = ProviderRosterRow(
        providerId = providerId,
        displayName = providerId,
        origin = origin,
        authMethods = listOf(RosterAuthMethod.API_KEY),
        stored = null,
        signingIn = false,
        enabled = enabled,
        configurable = true,
        subscription = false,
        catalogLayer = RosterCatalogLayer.EMBEDDED_BASELINE,
        selectedModelId = null,
        thinking = null,
        presentation = null,
        endpointBase = null,
        lastRefusal = null,
        modelCount = 0,
    )

    private class FakeRoster : ProviderRosterRepository {
        private val state = MutableStateFlow(ProviderRosterState())
        override val roster: StateFlow<ProviderRosterState> = state.asStateFlow()
        override val models: StateFlow<Map<String, List<ProviderModel>>> =
            MutableStateFlow(emptyMap<String, List<ProviderModel>>()).asStateFlow()

        fun publish(vararg rows: ProviderRosterRow) {
            state.value = ProviderRosterState(ready = true, rows = rows.toList())
        }
    }

    private class FakeCredentials : ProviderCredentialsRepository {
        private val configured = MutableStateFlow(emptySet<String>())
        override val configuredProviderIds: StateFlow<Set<String>> = configured.asStateFlow()

        fun hold(providerId: String) {
            configured.value = configured.value + providerId
        }

        override suspend fun saveApiKey(providerId: String, material: ByteArray) {
            hold(providerId)
        }

        override suspend fun sealApiKey(providerId: String, material: ByteArray): String {
            hold(providerId)
            return providerId
        }

        override suspend fun heldCredentialHandle(providerId: String): String? =
            providerId.takeIf { it in configured.value }

        override suspend fun discardSealedKey(providerId: String) {
            configured.value = configured.value - providerId
        }

        override suspend fun forget(providerId: String) {
            configured.value = configured.value - providerId
        }
    }

    private class FakePreferences(
        route: ProviderRoute = ProviderRoute.DIRECT_WITH_YOUR_KEY,
    ) : UserPreferencesRepository {
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
        override suspend fun setOnboardingCompleted(completed: Boolean) = Unit
        override suspend fun setComposerSuggestions(enabled: Boolean) = Unit
    }

    private class RecordingAnalytics : AnalyticsClient {
        val recorded = mutableListOf<AnalyticsEvent>()
        override fun record(event: AnalyticsEvent) {
            recorded += event
        }

        override fun recent(): List<AnalyticsEvent> = recorded
    }

    private class RecordingNavigator : TaffyNavigator {
        val visited = mutableListOf<TaffyDestination>()
        override fun goTo(destination: TaffyDestination) {
            visited += destination
        }

        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack(): Boolean = false
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }
}

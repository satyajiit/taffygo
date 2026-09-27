// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-419's one consequential path: removing a credential.
 *
 * It goes through the same `ProviderCredentialsRepository` screen SCR-415
 * removes one through, so the coordinator behind it — the one writer decision
 * 0078 requires — serializes both. What the two screens share directly is the
 * warning, which is `ProviderRemovalSheet` and is drawn from one place.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ConnectedProvidersViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val roster = FakeProviderRoster()
    private val credentials = FakeProviderCredentials()
    private val preferences = FakeProviderPreferences()
    private val analytics = RecordingProviderAnalytics()
    private val navigator = RecordingProviderNavigator()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `removing needs the confirmation first`() = runTest(dispatcher) {
        val viewModel = started()

        viewModel.onIntent(ConnectedProvidersIntent.ConfirmSignOut, navigator)
        runCurrent()

        assertTrue(credentials.forgotten.isEmpty())
    }

    @Test
    fun `the confirmation names the row, and confirming removes it`() = runTest(dispatcher) {
        val viewModel = started()
        val row = viewModel.state.value.rows.single { it.providerId == "keyed" }

        viewModel.onIntent(ConnectedProvidersIntent.AskSignOut(row), navigator)
        runCurrent()
        assertEquals("keyed", viewModel.state.value.confirming?.providerId)

        viewModel.onIntent(ConnectedProvidersIntent.ConfirmSignOut, navigator)
        runCurrent()

        assertEquals(listOf("keyed"), credentials.forgotten)
        assertNull(viewModel.state.value.confirming)
    }

    @Test
    fun `changing your mind leaves the credential alone`() = runTest(dispatcher) {
        val viewModel = started()
        val row = viewModel.state.value.rows.single { it.providerId == "keyed" }

        viewModel.onIntent(ConnectedProvidersIntent.AskSignOut(row), navigator)
        viewModel.onIntent(ConnectedProvidersIntent.CancelSignOut, navigator)
        runCurrent()

        assertNull(viewModel.state.value.confirming)
        assertTrue(credentials.forgotten.isEmpty())
    }

    @Test
    fun `a removal that fails leaves the row where it was`() = runTest(dispatcher) {
        credentials.refuses = true
        val viewModel = started()
        val row = viewModel.state.value.rows.single { it.providerId == "keyed" }

        viewModel.onIntent(ConnectedProvidersIntent.AskSignOut(row), navigator)
        runCurrent()
        viewModel.onIntent(ConnectedProvidersIntent.ConfirmSignOut, navigator)
        runCurrent()

        assertEquals(listOf("keyed", "mine"), viewModel.state.value.rows.map { it.providerId })
    }

    @Test
    fun `a row leads to the surface that owns changing it`() = runTest(dispatcher) {
        val viewModel = started()
        val rows = viewModel.state.value.rows

        rows.forEach { viewModel.onIntent(ConnectedProvidersIntent.OpenRow(it), navigator) }
        runCurrent()

        assertEquals(
            listOf(
                TaffyDestination.ProviderConfig("keyed"),
                TaffyDestination.CustomEndpointSetup("mine"),
            ),
            navigator.visited,
        )
    }

    // Adding is the one thing this screen sends a person elsewhere for, and it
    // is pushed rather than replacing: changing their mind is one press back.
    @Test
    fun `adding a provider opens the hub`() = runTest(dispatcher) {
        val viewModel = started()

        viewModel.onIntent(ConnectedProvidersIntent.AddProvider, navigator)
        runCurrent()

        assertEquals(listOf(TaffyDestination.AiAndProviders), navigator.visited)
    }

    @Test
    fun `the screen records itself as SCR-419`() = runTest(dispatcher) {
        started().onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.ConnectedProviders.screenId)),
            analytics.recorded,
        )
    }

    private fun TestScope.started(): ConnectedProvidersViewModel {
        roster.publish(
            providerRow("keyed", stored = storedCredential()),
            providerRow(
                "mine",
                stored = storedCredential(),
                origin = RosterProviderOrigin.CUSTOM,
                catalogLayer = RosterCatalogLayer.USER_OVERRIDE,
            ),
            providerRow("offered"),
        )
        val viewModel =
            ConnectedProvidersViewModel(roster, credentials, analytics)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        return viewModel
    }
}

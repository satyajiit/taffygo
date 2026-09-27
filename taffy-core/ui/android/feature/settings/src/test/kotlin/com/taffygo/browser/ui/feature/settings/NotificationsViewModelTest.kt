// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import kotlinx.coroutines.CompletableDeferred
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
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission

@OptIn(ExperimentalCoroutinesApi::class)
class NotificationsViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `grant is persisted only after the Android result`() = runTest(dispatcher) {
        val stored = StoredPreferences()
        val permission = FakePermissionRequester(requestableDenial())
        val viewModel = viewModel(stored, permission)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(NotificationsIntent.Toggle(NotificationTopic.TASK_PROGRESS))
        runCurrent()

        assertEquals(listOf(PlatformPermission.NOTIFICATIONS), permission.requests)
        assertTrue(viewModel.state.value.isOn(NotificationTopic.TASK_PROGRESS))
        assertEquals(
            listOf(NotificationTopic.TASK_PROGRESS to true),
            stored.notificationWrites,
        )
    }

    @Test
    fun `denial leaves the preference off and projects Android rationale`() = runTest(dispatcher) {
        val stored = StoredPreferences()
        val permission = FakePermissionRequester(
            requestableDenial(),
            next = rationaleDenial(),
        )
        val viewModel = viewModel(stored, permission)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS))
        runCurrent()

        assertFalse(viewModel.state.value.isOn(NotificationTopic.DOWNLOADS))
        assertTrue(viewModel.state.value.permissionRationale)
        assertFalse(viewModel.state.value.permissionRequestInFlight)
        assertTrue(stored.notificationWrites.none { it.second })
    }

    @Test
    fun `final denial never loops the prompt and opens settings only on explicit intent`() =
        runTest(dispatcher) {
            val stored = StoredPreferences(setOf(NotificationTopic.TASK_PROGRESS))
            val permission = FakePermissionRequester(finalDenial())
            val viewModel = viewModel(stored, permission)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(NotificationsIntent.Toggle(NotificationTopic.TASK_PROGRESS))
            runCurrent()
            assertTrue(permission.requests.isEmpty())
            assertTrue(permission.settings.isEmpty())
            assertTrue(stored.preferences.value.notificationTopics.isEmpty())

            viewModel.onIntent(NotificationsIntent.OpenSystemSettings)
            runCurrent()
            assertEquals(listOf(PlatformPermission.NOTIFICATIONS), permission.settings)
        }

    @Test
    fun `resume after OS revocation clears every stored topic`() = runTest(dispatcher) {
        val stored = StoredPreferences(NotificationTopic.entries.toSet())
        val permission = FakePermissionRequester(granted())
        val viewModel = viewModel(stored, permission)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        permission.current = finalDenial()
        viewModel.onIntent(NotificationsIntent.RefreshPermission)
        runCurrent()

        assertTrue(stored.preferences.value.notificationTopics.isEmpty())
        assertTrue(viewModel.state.value.enabled.isEmpty())
    }

    @Test
    fun `rapid taps share one in-flight Android prompt`() = runTest(dispatcher) {
        val gate = CompletableDeferred<PlatformPermissionRequester.Snapshot>()
        val stored = StoredPreferences()
        val permission = FakePermissionRequester(requestableDenial(), gate = gate)
        val viewModel = viewModel(stored, permission)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS))
        viewModel.onIntent(NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS))
        runCurrent()
        assertEquals(1, permission.requests.size)
        assertTrue(viewModel.state.value.permissionRequestInFlight)

        gate.complete(granted())
        runCurrent()
        assertTrue(viewModel.state.value.isOn(NotificationTopic.DOWNLOADS))
    }

    private fun viewModel(
        stored: StoredPreferences,
        permission: FakePermissionRequester,
    ) = NotificationsViewModel(stored, permission, NoAnalytics())

    private class FakePermissionRequester(
        var current: PlatformPermissionRequester.Snapshot,
        private val next: PlatformPermissionRequester.Snapshot = granted(),
        private val gate: CompletableDeferred<PlatformPermissionRequester.Snapshot>? = null,
    ) : PlatformPermissionRequester {
        val requests = mutableListOf<PlatformPermission>()
        val settings = mutableListOf<PlatformPermission>()

        override fun current(permission: PlatformPermission) = current

        override suspend fun request(
            permission: PlatformPermission,
        ): PlatformPermissionRequester.Snapshot {
            requests += permission
            current = gate?.await() ?: next
            return current
        }

        override fun openSettings(permission: PlatformPermission): Boolean {
            settings += permission
            return true
        }
    }

    private class StoredPreferences(
        topics: Set<NotificationTopic> = emptySet(),
    ) : UserPreferencesRepository {
        private val current = MutableStateFlow(
            UserPreferences(notificationTopics = topics, loaded = true),
        )
        override val preferences: StateFlow<UserPreferences> = current
        val notificationWrites = mutableListOf<Pair<NotificationTopic, Boolean>>()

        override suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean) {
            notificationWrites += topic to enabled
            val changed = preferences.value.notificationTopics.toMutableSet().apply {
                if (enabled) add(topic) else remove(topic)
            }
            current.value = current.value.copy(
                notificationTopics = changed,
            )
        }

        override suspend fun setTheme(theme: ThemePreference) = Unit
        override suspend fun setAppLanguage(language: AppLanguage) = Unit
        override suspend fun setRegionCode(regionCode: String) = Unit
        override suspend fun setPseudoLocalization(enabled: Boolean) = Unit
        override suspend fun setForceDarkWeb(enabled: Boolean) = Unit
        override suspend fun setProviderRoute(route: ProviderRoute) = Unit
        override suspend fun setOnboardingCompleted(completed: Boolean) = Unit
        override suspend fun setComposerSuggestions(enabled: Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private companion object {
        fun granted() = PlatformPermissionRequester.Snapshot(
            PermissionDecision.GRANTED,
            canRequest = false,
            shouldShowRationale = false,
        )

        fun requestableDenial() = PlatformPermissionRequester.Snapshot(
            PermissionDecision.DENIED,
            canRequest = true,
            shouldShowRationale = false,
        )

        fun rationaleDenial() = PlatformPermissionRequester.Snapshot(
            PermissionDecision.DENIED,
            canRequest = true,
            shouldShowRationale = true,
        )

        fun finalDenial() = PlatformPermissionRequester.Snapshot(
            PermissionDecision.DENIED,
            canRequest = false,
            shouldShowRationale = false,
        )
    }
}

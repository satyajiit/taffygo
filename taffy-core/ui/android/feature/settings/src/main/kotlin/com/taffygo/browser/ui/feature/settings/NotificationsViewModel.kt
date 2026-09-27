// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import javax.inject.Inject
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission

/** Screen SCR-406's one source of truth. */
class NotificationsViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val permissions: PlatformPermissionRequester,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val permission = MutableStateFlow(
        permissions.current(PlatformPermission.NOTIFICATIONS),
    )
    private val requestingPermission = MutableStateFlow(false)
    private var promptRunning = false

    /** What screen SCR-406 renders. */
    val state: StateFlow<NotificationsUiState> = combine(
        preferences.preferences,
        permission,
        requestingPermission,
    ) { stored, permission, requesting ->
        projectNotifications(stored.notificationTopics, permission, requesting)
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectNotifications(
                preferences.preferences.value.notificationTopics,
                permission.value,
                requestingPermission.value,
            ),
        )

    init {
        if (permission.value.decision != PermissionDecision.GRANTED) {
            viewModelScope.launch { disableStoredTopics() }
        }
    }

    /** Act on something the user did. */
    fun onIntent(intent: NotificationsIntent) {
        viewModelScope.launch {
            when (intent) {
                is NotificationsIntent.Toggle -> {
                    toggle(intent)
                }
                NotificationsIntent.RefreshPermission -> refreshPermission()
                NotificationsIntent.OpenSystemSettings -> openSystemSettings()
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Notifications.screenId))
    }

    private suspend fun toggle(intent: NotificationsIntent.Toggle) {
        val before = currentState()
        val next = reduceNotifications(before, intent)
        when {
            before.isOn(intent.topic) ->
                preferences.setNotificationTopic(intent.topic, false)
            next.isOn(intent.topic) ->
                preferences.setNotificationTopic(intent.topic, true)
            next.permissionRequestInFlight && !before.permissionRequestInFlight ->
                requestAndEnable(intent.topic)
        }
    }

    private suspend fun requestAndEnable(
        topic: NotificationTopic,
    ) {
        if (promptRunning) return
        promptRunning = true
        requestingPermission.value = true
        try {
            val result = permissions.request(PlatformPermission.NOTIFICATIONS)
            permission.value = result
            if (result.decision == PermissionDecision.GRANTED) {
                preferences.setNotificationTopic(topic, true)
            } else {
                disableStoredTopics()
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: RuntimeException) {
            permission.value = unavailablePermission()
            disableStoredTopics()
        } finally {
            promptRunning = false
            requestingPermission.value = false
        }
    }

    private suspend fun refreshPermission() {
        val refreshed = permissions.current(PlatformPermission.NOTIFICATIONS)
        permission.value = refreshed
        if (refreshed.decision != PermissionDecision.GRANTED) disableStoredTopics()
    }

    private fun openSystemSettings() {
        if (currentState().permissionRequiresSystemSettings) {
            permissions.openSettings(PlatformPermission.NOTIFICATIONS)
        }
    }

    private suspend fun disableStoredTopics() {
        preferences.preferences.value.notificationTopics.forEach { topic ->
            try {
                preferences.setNotificationTopic(topic, false)
            } catch (_: RuntimeException) {
                // The UI remains masked while Android denies notifications. A later resume retries.
            }
        }
    }

    private fun currentState(): NotificationsUiState = projectNotifications(
        preferences.preferences.value.notificationTopics,
        permission.value,
        requestingPermission.value,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L

        fun unavailablePermission() = PlatformPermissionRequester.Snapshot(
            decision = PermissionDecision.UNAVAILABLE,
            canRequest = false,
            shouldShowRationale = false,
        )
    }
}

private fun projectNotifications(
    stored: Set<NotificationTopic>,
    permission: PlatformPermissionRequester.Snapshot,
    requesting: Boolean,
) = NotificationsUiState(
    enabled = if (permission.decision == PermissionDecision.GRANTED) stored else emptySet(),
    permissionDecision = permission.decision,
    permissionCanRequest = permission.canRequest,
    permissionRationale = permission.shouldShowRationale,
    permissionRequestInFlight = requesting,
)

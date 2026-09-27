// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString
import taffy.core_api.PermissionDecision

/** Screen SCR-406 — notifications. */
@Composable
fun NotificationsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: NotificationsViewModel = screenViewModel(TaffyDestination.Notifications)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }
    LifecycleEventEffect(Lifecycle.Event.ON_RESUME) {
        viewModel.onIntent(NotificationsIntent.RefreshPermission)
    }

    NotificationsContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. Stacked topic cards; monitoring stays named, not off. */
@Composable
fun NotificationsContent(
    state: NotificationsUiState,
    onIntent: (NotificationsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.Notifications,
        title = taffyString(R.string.taffy_notifications_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .testTag(TOPIC_LIST_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            SettingsHomeEyebrow(title = taffyString(R.string.taffy_notifications_topics_heading))
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                state.topics.forEach { topic ->
                    NotificationTopicCard(
                        topic = topic,
                        checked = state.isOn(topic),
                        enabled = !state.permissionRequestInFlight,
                        onCheckedChange = { onIntent(NotificationsIntent.Toggle(topic)) },
                    )
                }
            }
        }

        if (state.permissionDecision != PermissionDecision.GRANTED) {
            NotificationPermissionTile(state, onIntent)
        }

        TaffyInfoTile(testTag = NOTIFICATIONS_ABSENT_TEST_TAG) {
            Text(
                text = taffyString(R.string.taffy_notifications_absent),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun NotificationPermissionTile(
    state: NotificationsUiState,
    onIntent: (NotificationsIntent) -> Unit,
) {
    val message = when {
        state.permissionRequestInFlight -> R.string.taffy_notifications_permission_requesting
        state.permissionRequiresSystemSettings -> R.string.taffy_notifications_permission_blocked
        state.permissionRationale -> R.string.taffy_notifications_permission_rationale
        state.permissionDecision == PermissionDecision.UNAVAILABLE ->
            R.string.taffy_notifications_permission_unavailable
        else -> R.string.taffy_notifications_permission_first_request
    }
    TaffyInfoTile(testTag = NOTIFICATION_PERMISSION_TEST_TAG) {
        Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug)) {
            Text(
                text = taffyString(message),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            if (state.permissionRequiresSystemSettings) {
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_notifications_open_settings),
                    onClick = { onIntent(NotificationsIntent.OpenSystemSettings) },
                    testTag = NOTIFICATION_SETTINGS_TEST_TAG,
                )
            }
        }
    }
}

/** The tags screen SCR-406's semantics tests name. */
const val TOPIC_LIST_TEST_TAG: String = "notifications_topics"
const val TOPIC_TEST_TAG_PREFIX: String = "notifications_topic_"
const val SWITCH_TEST_TAG_PREFIX: String = "notifications_switch_"
const val NOTIFICATIONS_ABSENT_TEST_TAG: String = "notifications_absent"
const val NOTIFICATION_PERMISSION_TEST_TAG: String = "notifications_permission"
const val NOTIFICATION_SETTINGS_TEST_TAG: String = "notifications_open_settings"

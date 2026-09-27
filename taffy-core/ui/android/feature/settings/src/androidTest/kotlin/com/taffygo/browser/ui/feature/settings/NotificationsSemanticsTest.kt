// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import taffy.core_api.PermissionDecision

/**
 * Screen SCR-406 — what the UI host may notify about.
 *
 * Each topic is a switch that reports whether it is on, so its state is
 * available to a screen reader rather than only to someone looking at it.
 */
class NotificationsSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<NotificationsIntent>()

    @Test
    fun everyTopicIsARowWithASwitch() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                NotificationsContent(
                    state = NotificationsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.Notifications.screenId).assertExists()
        NotificationTopic.entries.forEach { topic ->
            compose.onNodeWithTag("$TOPIC_TEST_TAG_PREFIX${topic.label}").assertExists()
            compose.onNodeWithTag("$SWITCH_TEST_TAG_PREFIX${topic.label}").assertExists()
        }
    }

    @Test
    fun aTopicThatIsOnReportsThatItIsOn() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                NotificationsContent(
                    state = NotificationsUiState(
                        enabled = setOf(NotificationTopic.TASK_PROGRESS),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$SWITCH_TEST_TAG_PREFIX${NotificationTopic.TASK_PROGRESS.label}")
            .assertIsOn()
        compose.onNodeWithTag("$SWITCH_TEST_TAG_PREFIX${NotificationTopic.DOWNLOADS.label}")
            .assertIsOff()
    }

    @Test
    fun togglingATopicSendsThatTopic() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                NotificationsContent(
                    state = NotificationsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$SWITCH_TEST_TAG_PREFIX${NotificationTopic.DOWNLOADS.label}")
            .performClick()

        assertEquals(
            listOf(NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS)),
            intents,
        )
    }

    @Test
    fun unavailableTopicsAreNamedRatherThanShownAsDeadSwitches() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                NotificationsContent(
                    state = NotificationsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(NOTIFICATIONS_ABSENT_TEST_TAG).assertExists()
    }

    @Test
    fun finalPermissionDenialOffersAnExplicitSystemSettingsAction() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                NotificationsContent(
                    state = NotificationsUiState(
                        permissionDecision = PermissionDecision.DENIED,
                        permissionCanRequest = false,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(NOTIFICATION_PERMISSION_TEST_TAG).assertExists()
        compose.onNodeWithTag(NOTIFICATION_SETTINGS_TEST_TAG).performClick()
        assertEquals(listOf(NotificationsIntent.OpenSystemSettings), intents)
    }
}

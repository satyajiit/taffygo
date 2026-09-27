// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.NotificationTopic
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.PermissionDecision

class NotificationsReducerTest {

    @Test
    fun `toggling a topic turns it on and leaves the other off`() {
        val after = reduceNotifications(
            NotificationsUiState(),
            NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS),
        )
        assertTrue(after.isOn(NotificationTopic.DOWNLOADS))
        assertFalse(after.isOn(NotificationTopic.TASK_PROGRESS))
        assertEquals(NotificationTopic.entries, after.topics)
    }

    @Test
    fun `toggling a topic that is on turns only that topic off`() {
        val before = NotificationsUiState(
            enabled = setOf(NotificationTopic.TASK_PROGRESS, NotificationTopic.DOWNLOADS),
        )
        val after = reduceNotifications(
            before,
            NotificationsIntent.Toggle(NotificationTopic.TASK_PROGRESS),
        )
        assertFalse(after.isOn(NotificationTopic.TASK_PROGRESS))
        assertTrue(after.isOn(NotificationTopic.DOWNLOADS))
    }

    @Test
    fun `a requestable denial starts one prompt without optimistically enabling`() {
        val before = NotificationsUiState(
            permissionDecision = PermissionDecision.DENIED,
            permissionCanRequest = true,
            permissionRationale = true,
        )

        val after = reduceNotifications(
            before,
            NotificationsIntent.Toggle(NotificationTopic.TASK_PROGRESS),
        )

        assertTrue(after.permissionRequestInFlight)
        assertFalse(after.isOn(NotificationTopic.TASK_PROGRESS))
    }

    @Test
    fun `an in-flight or final denial never starts another prompt`() {
        val inFlight = NotificationsUiState(
            permissionDecision = PermissionDecision.DENIED,
            permissionCanRequest = true,
            permissionRequestInFlight = true,
        )
        val blocked = inFlight.copy(
            permissionCanRequest = false,
            permissionRequestInFlight = false,
        )

        assertEquals(
            inFlight,
            reduceNotifications(
                inFlight,
                NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS),
            ),
        )
        assertEquals(
            blocked,
            reduceNotifications(
                blocked,
                NotificationsIntent.Toggle(NotificationTopic.DOWNLOADS),
            ),
        )
    }
}

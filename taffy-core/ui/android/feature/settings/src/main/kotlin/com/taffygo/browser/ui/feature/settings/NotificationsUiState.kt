// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.NotificationTopic
import taffy.core_api.PermissionDecision

/** Screen SCR-406 — what the UI layer may notify about. */
data class NotificationsUiState(
    /** Every topic that exists. */
    val topics: List<NotificationTopic> = NotificationTopic.entries,
    /** The topics that are on. */
    val enabled: Set<NotificationTopic> = emptySet(),
    /** Android's current decision for posting notifications. */
    val permissionDecision: PermissionDecision = PermissionDecision.GRANTED,
    /** Whether another person-started system prompt is safe. */
    val permissionCanRequest: Boolean = false,
    /** Whether Android asks the app to explain before another prompt. */
    val permissionRationale: Boolean = false,
    /** Whether the one allowed permission prompt is currently open. */
    val permissionRequestInFlight: Boolean = false,
) {
    /** Whether a topic is on. */
    fun isOn(topic: NotificationTopic): Boolean = topic in enabled

    /** A denial that Android will only let the person change in system settings. */
    val permissionRequiresSystemSettings: Boolean
        get() = permissionDecision == PermissionDecision.DENIED && !permissionCanRequest
}

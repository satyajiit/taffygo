// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import taffy.core_api.PermissionDecision

/** Screen SCR-406 flips one topic and leaves every other topic alone. */
internal fun reduceNotifications(
    state: NotificationsUiState,
    intent: NotificationsIntent,
): NotificationsUiState = when (intent) {
    is NotificationsIntent.Toggle -> {
        when {
            state.isOn(intent.topic) -> state.copy(enabled = state.enabled - intent.topic)
            state.permissionDecision == PermissionDecision.GRANTED ->
                state.copy(enabled = state.enabled + intent.topic)
            state.permissionCanRequest && !state.permissionRequestInFlight ->
                state.copy(permissionRequestInFlight = true)
            else -> state
        }
    }
    NotificationsIntent.RefreshPermission,
    NotificationsIntent.OpenSystemSettings,
    -> state
}

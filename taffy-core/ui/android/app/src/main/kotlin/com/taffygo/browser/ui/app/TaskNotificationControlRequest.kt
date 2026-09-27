// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Content-free, exact authority carried by one immutable notification action. */
internal data class TaskNotificationControlRequest(
    val profileToken: String,
    val taskId: String,
    val taskRevision: ULong,
    val action: TaskNotificationAction,
) {
    /** An injective PendingIntent identity; extras alone do not affect equality. */
    fun pendingIntentIdentifier(): String = buildString {
        append("v1|")
        append(profileToken.length).append(':').append(profileToken)
        append(taskId.length).append(':').append(taskId)
        append(taskRevision).append(':').append(action.wireName)
    }
}

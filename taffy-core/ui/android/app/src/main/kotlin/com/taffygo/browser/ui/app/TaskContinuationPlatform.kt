// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Android notification/foreground-service mechanics behind one narrow port. */
internal interface TaskContinuationPlatform {
    fun canPostNotifications(): Boolean

    /** Returns false when Android refused the requested foreground continuation. */
    fun publish(projection: TaskNotificationProjection, requireForeground: Boolean): Boolean

    fun remove(profileToken: String, notificationId: Int)
}

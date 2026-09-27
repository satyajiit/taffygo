// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.NotificationTopic

/** Everything screen SCR-406 can be asked to do. */
sealed interface NotificationsIntent {

    /** Turn one topic on or off. */
    data class Toggle(val topic: NotificationTopic) : NotificationsIntent

    /** Reconcile preferences after this Window becomes visible again. */
    data object RefreshPermission : NotificationsIntent

    /** Open the app's Android notification settings after a final denial. */
    data object OpenSystemSettings : NotificationsIntent
}

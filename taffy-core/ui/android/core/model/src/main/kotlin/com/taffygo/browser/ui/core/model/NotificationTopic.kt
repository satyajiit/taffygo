// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * What the UI layer may notify about (screen SCR-406). Monitoring alerts belong
 * to milestone M6 and are absent rather than shown as a disabled row that
 * promises something no code can deliver yet.
 */
enum class NotificationTopic(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** The ongoing-task notification with pause and stop (screen SCR-801). */
    TASK_PROGRESS("task_progress"),

    /** Download progress and completion (screen SCR-802). */
    DOWNLOADS("downloads"),
}

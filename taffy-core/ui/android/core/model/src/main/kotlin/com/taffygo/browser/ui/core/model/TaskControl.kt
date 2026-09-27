// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Closed controls projected from the task reducer's current legality.
 *
 * They are one type rather than three booleans so every surface that shows a
 * task shows the same admitted controls, in the same order, with the same words.
 */
enum class TaskControl(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Hold the task. It waits indefinitely; nothing expires it. */
    PAUSE("pause"),

    /** Re-enter the queue after a user-requested hold has settled. */
    RESUME("resume"),

    /** Take over the tab. Works mid-action and preempts queued work. */
    TAKE_OVER("take_over"),

    /** Stop the task. What the user did, never conflated with a failure. */
    STOP("stop"),
}

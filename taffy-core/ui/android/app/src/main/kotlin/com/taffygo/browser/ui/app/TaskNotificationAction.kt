// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.TaskControl

/** The three exact task controls the ongoing notification may expose. */
internal enum class TaskNotificationAction(
    val wireName: String,
    val control: TaskControl,
) {
    PAUSE("pause", TaskControl.PAUSE),
    RESUME("resume", TaskControl.RESUME),
    STOP("stop", TaskControl.STOP),
    ;

    companion object {
        fun fromControl(control: TaskControl): TaskNotificationAction? = when (control) {
            TaskControl.PAUSE -> PAUSE
            TaskControl.RESUME -> RESUME
            TaskControl.STOP -> STOP
            TaskControl.TAKE_OVER -> null
        }

        fun fromWireName(value: String): TaskNotificationAction? =
            entries.firstOrNull { it.wireName == value }
    }
}

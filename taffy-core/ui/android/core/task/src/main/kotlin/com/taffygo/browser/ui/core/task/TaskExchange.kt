// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/**
 * One exchange of a task's conversation: the person's words and what Taffy
 * said under them, or null while it is still to say it.
 */
data class TaskExchange(
    val question: String,
    val answer: TaskAnswerProjection?,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.model.TaskControl

/**
 * One control the browser would not take, and the task revision it named.
 *
 * Here rather than in a view model because two surfaces offer the same
 * controls over the same task — the pill and the task view — and a refusal
 * either of them provokes is a fact about the task, not about the screen that
 * asked. Holding it in one place is also what lets the pill withdraw a control
 * it has learned does not work while the task view explains why, which is the
 * split decision 0141 already draws between those two surfaces.
 *
 * The control is kept beside the reason because a person can act on one of
 * them and not on the other. A refused Resume has a move behind it: the task
 * cannot be continued, so stop it and ask again. The reasons do not — nobody
 * can act on the difference between `INVALID_REQUEST` and `STALE_REVISION` —
 * which is the same argument the workspace refusal already makes (decision
 * 0221).
 */
data class TaskControlRefusal(
    val taskId: String,
    val taskRevision: ULong,
    val control: TaskControl,
    val reason: FailureReason,
)

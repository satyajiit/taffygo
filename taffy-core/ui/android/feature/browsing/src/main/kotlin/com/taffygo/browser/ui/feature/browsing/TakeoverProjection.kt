// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskInputRequest
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.task.TaskRepositoryState

/**
 * The pure projection from task truth to what the browsing surface draws over
 * the page.
 *
 * A function rather than a branch inside a view model, for the reason every
 * other projection in this feature is one: the two rules worth getting right
 * are both rules about *not* drawing, and a host test can hold them.
 */
internal fun projectTakeover(
    repository: TaskRepositoryState,
    request: TaskInputRequest?,
    pageHost: String,
): TakeoverUiState {
    val task = repository.task
    // A task that is not being driven still exists, and the chrome row reads
    // that rather than `isUnderWay` — so the early return carries both out.
    val display = task?.displayState
    val hasTask = display != null
    val taskEnded = display?.isFinal == true
    if (task?.isUnderWay != true) {
        return TakeoverUiState(hasTask = hasTask, taskEnded = taskEnded)
    }
    return TakeoverUiState(
        taskId = task.id,
        taskRevision = task.revision,
        active = true,
        hasTask = true,
        taskEnded = false,
        waitingForYou = task.displayState == TaskDisplayState.WAITING_FOR_YOU,
        canTakeOver = TaskControl.TAKE_OVER in task.allowedControls,
        // Only when the widget is on the page being shown. A cut-out is a claim
        // about a position, and a position on one page means nothing on
        // another: drawn over the wrong page it points at whatever happens to
        // be there and hides the rest.
        highlight = request
            ?.takeIf { it.isInteractiveChallenge && it.host == pageHost }
            ?.interactiveHighlight,
    )
}

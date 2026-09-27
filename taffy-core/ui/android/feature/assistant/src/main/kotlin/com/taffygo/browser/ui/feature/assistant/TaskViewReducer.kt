// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.model.AssistantMode

/** Pure projection of browser-owned Core API state onto screen SCR-303. */
internal fun reduceTaskView(
    state: TaskViewUiState,
    intent: TaskViewIntent,
): TaskViewUiState = when (intent) {
    TaskViewIntent.RememberThis,
    TaskViewIntent.DismissRememberThis,
    -> state.copy(rememberThis = null)
    TaskViewIntent.DismissSetup -> state.copy(setupDismissed = true)
    else -> state
}

internal fun projectTaskView(repository: TaskRepositoryState): TaskViewUiState {
    val task = repository.task
    return TaskViewUiState(
        taskId = task?.id,
        taskRevision = task?.revision,
        goal = task?.goal.orEmpty(),
        state = task?.displayState,
        mode = task?.mode ?: AssistantMode.YOU_BROWSE,
        timeline = task?.timeline.orEmpty(),
        sources = task?.sources.orEmpty(),
        facts = task?.facts.orEmpty(),
        liveAnswer = task?.liveAnswer,
        artifacts = task?.artifacts.orEmpty(),
        canSaveWorkspace = task?.canSaveWorkspace == true,
        canDiscardWorkspace = task?.canDiscardWorkspace == true,
        workspaceSaved = task?.workspaceSaved == true,
        controls = controlsFor(task),
        approvalActionId = task?.pendingAction?.id,
        approvalHost = task?.pendingAction?.host,
        approvalCount = task?.pendingAction?.itemCount ?: 0,
        hasHandover = task?.hasHandover == true,
        hasAsk = task?.hasAsk == true,
        askPrompt = task?.askPrompt,
        notice = repository.availability.toTaskNotice(),
        retryAvailable = repository.availability == CoreUiAvailability.RETRY_REQUIRED,
        failure = task?.failure.toTaskFailureReason(),
    )
}

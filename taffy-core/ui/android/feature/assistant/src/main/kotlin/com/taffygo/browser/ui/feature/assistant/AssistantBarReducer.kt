// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.AssistantMode
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState

/** Pure projection from generated Core API state to the existing Assistant bar design. */
internal fun projectAssistantBar(
    repository: TaskRepositoryState,
    listening: Boolean = false,
    readiness: TaffyReadiness = TaffyReadiness.Unknown,
): AssistantBarUiState {
    val task = repository.task
    return AssistantBarUiState(
        taskId = task?.id,
        taskRevision = task?.revision,
        state = task?.displayState,
        listening = listening && task?.displayState == null,
        mode = task?.mode ?: AssistantMode.YOU_BROWSE,
        sourcesRead = pagesReadIn(task?.timeline.orEmpty()),
        sourcesPlanned = pagesPlannedBy(task),
        latestStep = task?.timeline?.maxByOrNull { it.sequence },
        approvalHost = task?.pendingAction?.host,
        approvalCount = task?.pendingAction?.itemCount ?: 0,
        hasHandover = task?.hasHandover == true,
        hasAsk = task?.hasAsk == true,
        hasInputRequest = task?.hasInputRequest == true,
        askPrompt = task?.askPrompt,
        controls = controlsFor(task),
        notice = repository.availability.toTaskNotice(),
        failure = task?.failure.toTaskFailureReason(),
        readiness = readiness,
        statusMessageKey = task?.statusMessageKey,
        progressBasisPoints = task?.progressBasisPoints?.toInt() ?: 0,
    )
}

/** The reducer-owned list for this exact task revision, preserved verbatim. */
internal fun controlsFor(task: TaskProjection?): List<TaskControl> =
    task?.allowedControls.orEmpty()

/**
 * How many pages a task has read: distinct hosts, not steps.
 *
 * The timeline is what the task did rather than what it saved (decision
 * 0148), so a page read three times is three steps — and "3 pages read" for
 * one page is a count standing in for a fact. A read that named no host counts
 * once for itself. The bar and the failed task's tile both count this way, so
 * the two numbers a person sees for one task cannot disagree.
 */
internal fun pagesReadIn(timeline: List<TaskTimelineEntry>): Int = timeline
    .filter { it.kind == TaskTimelineKind.READ_PAGE }
    .distinctBy { it.host ?: it.sequence }
    .size

/**
 * How many pages a task set out to read, or nought when it set out to read no
 * fixed number.
 *
 * The workspace's sources begin as the pages the person consented to at the
 * start. For a comparison or a brief that is the plan, and "2 of 4 pages read"
 * counts against it. An errand finds its own pages as it goes (decision 0223),
 * so the page it was asked from is where it began and not how many it will
 * read, and its list is neither a plan nor a count of what it read. Counting an
 * errand's reads against it put "Paused — 3 of 1 page read" on a phone: a
 * numerator from what the task did over a denominator from what its workspace
 * kept, which are two different records.
 */
internal fun pagesPlannedBy(task: TaskProjection?): Int = when {
    task == null -> 0
    task.template == TaskTemplate.WEB_ERRAND -> 0
    else -> task.sources.size
}

internal fun CoreUiAvailability.toTaskNotice(): TaskNotice? = when (this) {
    CoreUiAvailability.STARTING,
    CoreUiAvailability.READY,
    -> null
    CoreUiAvailability.UNAVAILABLE -> TaskNotice.CORE_UNAVAILABLE
    CoreUiAvailability.RETRY_REQUIRED -> TaskNotice.RETRY_REQUIRED
}

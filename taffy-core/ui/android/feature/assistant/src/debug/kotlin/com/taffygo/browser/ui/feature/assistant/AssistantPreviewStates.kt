// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.AssistantMode
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.task.TaffyReadiness

/**
 * Fixed states for this feature's previews, covering every one of the seven
 * user-visible task states across the screens that show them.
 */
object AssistantPreviewStates {

    private const val CAPTURED_AT = 1_767_225_600_000L
    private const val DOCS = "docs.example.test"
    private const val SHOP = "shop.example.test"

    val rememberThis = RememberThisSuggestion(
        statement = "Prefers nonstop flights",
        why = "You corrected this twice in this workspace.",
    )

    /** The Assistant bar while a task runs. */
    val running = AssistantBarUiState(
        state = TaskDisplayState.RUNNING,
        mode = AssistantMode.TAFFY_BROWSES,
        sourcesRead = 2,
        sourcesPlanned = 4,
        controls = listOf(TaskControl.PAUSE, TaskControl.TAKE_OVER, TaskControl.STOP),
    )

    /** The Assistant bar while a task waits on the user. */
    val waitingForYou = AssistantBarUiState(
        state = TaskDisplayState.WAITING_FOR_YOU,
        mode = AssistantMode.BROWSE_TOGETHER,
        approvalHost = SHOP,
        approvalCount = 2,
        controls = listOf(TaskControl.PAUSE, TaskControl.TAKE_OVER, TaskControl.STOP),
    )

    /** The Assistant bar after the profile service disconnects. */
    val runningNotDriven = running.copy(
        sourcesRead = 0,
        notice = TaskNotice.CORE_UNAVAILABLE,
    )

    /** The Assistant bar after a task finished. */
    val done = AssistantBarUiState(
        state = TaskDisplayState.DONE,
        sourcesRead = 3,
        sourcesPlanned = 3,
        conflicts = 1,
    )

    /** The Assistant bar after a task finished with gaps. */
    val partlyDone = AssistantBarUiState(
        state = TaskDisplayState.PARTLY_DONE,
        sourcesRead = 3,
    )

    /** The Assistant bar while a task is held and this revision admits the resume. */
    val paused = AssistantBarUiState(
        state = TaskDisplayState.PAUSED,
        sourcesRead = 2,
        sourcesPlanned = 4,
        controls = listOf(TaskControl.RESUME, TaskControl.STOP),
    )

    /**
     * The same held task on a revision that does not admit it.
     *
     * The outline pill without the chip: a Resume the reducer would refuse is a
     * control the bar must not draw.
     */
    val pausedWithoutResume = paused.copy(controls = emptyList())

    /** The Assistant bar after the person stopped a task. */
    val stopped = AssistantBarUiState(
        state = TaskDisplayState.STOPPED,
        sourcesRead = 1,
        sourcesPlanned = 4,
    )

    /** The Assistant bar after a task failed because no provider could be reached. */
    val failedProvider = AssistantBarUiState(
        state = TaskDisplayState.FAILED,
        failure = TaskFailureReason.PROVIDER_UNAVAILABLE,
    )

    /** The Assistant bar, idle, on a phone where nothing is set up. */
    val notSetUp = AssistantBarUiState(readiness = TaffyReadiness.NotSetUp)

    /** The task view while a task runs. */
    val taskRunning = TaskViewUiState(
        goal = "compare these two policies",
        state = TaskDisplayState.RUNNING,
        mode = AssistantMode.TAFFY_BROWSES,
        timeline = listOf(
            TaskTimelineEntry(1, TaskTimelineKind.READ_PAGE, DOCS, 3, CAPTURED_AT),
            TaskTimelineEntry(0, TaskTimelineKind.BUILT_OUTPUT, count = 3, atEpochMillis = CAPTURED_AT),
        ),
        sources = listOf(
            SourceRecord(SourceId(DOCS), "Retention policy", DOCS, CAPTURED_AT, 3),
            SourceRecord(SourceId(SHOP), "Product listing", SHOP, CAPTURED_AT, 0),
        ),
        facts = listOf(
            Fact(FactId("f1"), "price", "62,999", FactKind.FROM_THE_PAGE, listOf(SourceId(DOCS))),
            Fact(
                FactId("f2"),
                "warranty",
                "2 years",
                FactKind.SUMMARIZED,
                listOf(SourceId(DOCS)),
                hasConflict = true,
            ),
        ),
        controls = listOf(TaskControl.PAUSE, TaskControl.TAKE_OVER, TaskControl.STOP),
    )

    /** The task view after a task finished with gaps. */
    val taskPartlyDone = taskRunning.copy(
        state = TaskDisplayState.PARTLY_DONE,
        mode = AssistantMode.YOU_BROWSE,
        controls = emptyList(),
        rememberThis = rememberThis,
    )

    /** The task view while a site needs a sign-in. */
    val taskWaiting = taskRunning.copy(
        state = TaskDisplayState.WAITING_FOR_YOU,
        approvalActionId = "action-preview",
        approvalHost = SHOP,
        approvalCount = 2,
        controls = listOf(TaskControl.STOP),
    )

    /** The task view while Taffy has handed the page back. */
    val taskHandover = taskRunning.copy(
        state = TaskDisplayState.WAITING_FOR_YOU,
        hasHandover = true,
        controls = listOf(TaskControl.STOP),
    )

    /** The task view while Taffy is waiting for a typed answer. */
    val taskAsk = taskRunning.copy(
        state = TaskDisplayState.WAITING_FOR_YOU,
        hasAsk = true,
        controls = listOf(TaskControl.STOP),
    )

    /**
     * The task view retained while the profile service is unavailable.
     */
    val taskNotDriven = taskRunning.copy(
        timeline = emptyList(),
        sources = emptyList(),
        facts = emptyList(),
        notice = TaskNotice.CORE_UNAVAILABLE,
    )

    /** The task view after a task failed because no provider could be reached. */
    val taskFailedProvider = taskPartlyDone.copy(
        state = TaskDisplayState.FAILED,
        failure = TaskFailureReason.PROVIDER_UNAVAILABLE,
        rememberThis = null,
    )

    val taskRefused = taskPartlyDone

    /** The task view while a task is held by the provider's limit. */
    val taskPaused = taskRunning.copy(
        state = TaskDisplayState.PAUSED,
        controls = listOf(TaskControl.RESUME, TaskControl.STOP),
    )
}

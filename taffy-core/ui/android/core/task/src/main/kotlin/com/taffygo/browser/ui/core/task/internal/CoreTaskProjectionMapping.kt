// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.PendingActionProjection
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskArtifactProjection
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.TaskActivityKind
import taffy.core_api.TaskActivityView
import taffy.core_api.TaskPhase
import taffy.core_api.TaskTemplateId
import taffy.core_api.TaskViewState
import taffy.core_api.WorkspaceViewState

// How one immutable generated `CoreStatus` becomes the Android task
// projection. Pure over its inputs: nothing here reads a flow or holds state,
// which is what lets `CoreTaskRepository` key its `distinctUntilChangedBy` on
// [taskProjectionVersion] and trust that an equal version means an equal
// projection. The repository owns the stream; this file owns the reading.

internal fun CoreStatus.toRepositoryState(
    followedTaskId: String?,
    putAwayTaskIds: Set<String> = emptySet(),
): TaskRepositoryState {
    val complete = hasCompleteProjection()
    val views = active_tasks.takeIf { complete }.orEmpty().residing(putAwayTaskIds)
    val followed = views.followedTask(followedTaskId)
    // Only the followed task carries its workspace: that projection is the
    // expensive one, and it is the one the surfaces draw.
    val tasks = views.map { task ->
        val workspace = task.takeIf { it === followed }?.workspace_id?.let { workspaceId ->
            workspaces.firstOrNull { it.workspace_id == workspaceId }
        }
        task.toProjection(
            workspace?.toTaskOutputProjectionOrNull(),
            answer = null,
        )
    }
    return TaskRepositoryState(
        availability = when (availability) {
            CoreAvailability.STARTING -> CoreUiAvailability.STARTING
            CoreAvailability.READY -> if (complete) {
                CoreUiAvailability.READY
            } else {
                CoreUiAvailability.UNAVAILABLE
            }
            CoreAvailability.UNAVAILABLE -> CoreUiAvailability.UNAVAILABLE
            CoreAvailability.CIRCUIT_OPEN -> CoreUiAvailability.RETRY_REQUIRED
        },
        generation = generation,
        task = tasks.firstOrNull { it.id == followed?.task_id },
        others = tasks.filter { it.id != followed?.task_id },
    )
}

/**
 * The one task the surfaces follow: the id a composer named if the core still
 * holds it, else the first task under way, else the first task there is.
 *
 * Under way first because a finished session the core still lists is not what
 * a bar should be talking about while another task is running; first-of-list
 * last because a single finished task is still the thing to show.
 *
 * **That last fall-through is only safe because the core decides what is in the
 * list and in what order.** It used to be a guess, and the guess was wrong in a
 * way nothing here could see: `active_tasks` carried every task the profile had
 * ever run, keyed and therefore ordered by identity, so "the first task there
 * is" was an arbitrary old one — a bar on a freshly started browser saying
 * "Couldn't finish" about a task from the day before. The core now sends tasks
 * newest first and leaves out anything that ended before this browser run
 * (decision 0149), which makes this line mean what it says. Do not add a
 * different ordering here: two orderings is how the first one stopped being
 * checked.
 */
/**
 * What is left after the person has put some ended tasks away.
 *
 * Re-read from the published list on every projection rather than remembered
 * once, so an id can only ever hide the exact task it named and only while
 * that task is still ended. A core that republishes a task as running — a
 * resume, a restored session, an id reused across generations — brings it
 * straight back, which is the property that makes a residency filter safe to
 * apply to a list the browser does not own.
 */
private fun List<TaskViewState>.residing(putAwayTaskIds: Set<String>): List<TaskViewState> =
    if (putAwayTaskIds.isEmpty()) {
        this
    } else {
        filterNot { it.task_id in putAwayTaskIds && it.phase.hasEnded() }
    }

/**
 * Whether a person can be done with this task.
 *
 * PAUSED is deliberately absent: a pause is waiting, and the way out of it is
 * Resume. IDLE is absent because a task that has not started has nothing to
 * put away.
 */
internal fun TaskPhase.hasEnded(): Boolean = when (this) {
    TaskPhase.COMPLETED,
    TaskPhase.PARTIAL,
    TaskPhase.FAILED,
    TaskPhase.CANCELLED,
    TaskPhase.OUTCOME_UNKNOWN,
    -> true
    TaskPhase.IDLE,
    TaskPhase.PLANNING,
    TaskPhase.WAITING_FOR_USER,
    TaskPhase.RUNNING,
    TaskPhase.PAUSED,
    -> false
}

private fun List<TaskViewState>.followedTask(followedTaskId: String?): TaskViewState? =
    firstOrNull { it.task_id == followedTaskId }
        ?: firstOrNull { it.phase.isUnderWay() }
        ?: firstOrNull()

private fun TaskPhase.isUnderWay(): Boolean = when (this) {
    TaskPhase.PLANNING,
    TaskPhase.WAITING_FOR_USER,
    TaskPhase.RUNNING,
    -> true
    TaskPhase.IDLE,
    TaskPhase.COMPLETED,
    TaskPhase.PARTIAL,
    TaskPhase.FAILED,
    TaskPhase.CANCELLED,
    TaskPhase.OUTCOME_UNKNOWN,
    TaskPhase.PAUSED,
    -> false
}

/** Only these generated facts can change the Android task projection. */
internal data class TaskProjectionVersion(
    val availability: CoreAvailability,
    val complete: Boolean,
    val generation: ULong,
    val tasks: List<TaskViewState>,
    val followedTaskId: String?,
    val workspace: WorkspaceViewState?,
)

internal fun CoreStatus.taskProjectionVersion(
    followedTaskId: String?,
    putAwayTaskIds: Set<String> = emptySet(),
): TaskProjectionVersion {
    val complete = hasCompleteProjection()
    val views = active_tasks.takeIf { complete }.orEmpty().residing(putAwayTaskIds)
    val followed = views.followedTask(followedTaskId)
    return TaskProjectionVersion(
        availability = availability,
        complete = complete,
        generation = generation,
        tasks = views,
        followedTaskId = followed?.task_id,
        workspace = followed?.workspace_id?.let { workspaceId ->
            workspaces.firstOrNull { it.workspace_id == workspaceId }
        },
    )
}

private fun TaskViewState.toProjection(
    output: TaskOutputProjection?,
    answer: TaskAnswerProjection?,
): TaskProjection = TaskProjection(
    id = task_id,
    revision = revision,
    phase = phase,
    goal = goal,
    template = template_id.toUiTemplate(),
    progressBasisPoints = progress_basis_points,
    statusMessageKey = status_message_key,
    failure = failure?.code,
    pendingAction = pending_action?.let { action ->
        PendingActionProjection(
            id = action.action_id,
            host = action.host,
            itemCount = action.item_count.toInt(),
            summaryMessageKey = action.summary_message_key,
        )
    },
    askPrompt = pending_ask_prompt,
    inputRequest = pending_field_value_request,
    allowedControls = allowed_controls.map { control ->
        when (control) {
            taffy.core_api.TaskControlKind.PAUSE -> TaskControl.PAUSE
            taffy.core_api.TaskControlKind.RESUME -> TaskControl.RESUME
            taffy.core_api.TaskControlKind.TAKE_OVER -> TaskControl.TAKE_OVER
            taffy.core_api.TaskControlKind.STOP -> TaskControl.STOP
        }
    },
    // Newest first, which is how the panel draws it (UX spec section 6) and
    // what [TaskTimelineEntry] says it holds. The core publishes oldest first,
    // because that is the order things happened; reversing is a presentation
    // choice and this is the one place it is made.
    timeline = activity.asReversed().map { it.toTimelineEntry(output?.sources.orEmpty()) },
    sources = output?.sources.orEmpty(),
    facts = output?.facts.orEmpty(),
    workspaceId = output?.workspaceId,
    workspaceRevision = output?.workspaceRevision,
    workspaceSaved = output?.saved == true,
    canSaveWorkspace = output?.canSave == true,
    canDiscardWorkspace = output?.canDiscard == true,
    liveAnswer = answer,
    artifacts = artifacts.map { artifact ->
        TaskArtifactProjection(
            id = artifact.artifact_id,
            kind = artifact.kind,
            workspaceRevision = artifact.workspace_revision,
            accepted = artifact.accepted,
        )
    },
)

/**
 * One published step becomes one timeline entry (decision 0148).
 *
 * The count is the one thing that is joined rather than carried. A read's step
 * says which host was read and says nothing about how many facts came back,
 * because facts belong to the workspace and the reducer never sees one — so
 * "Read croma.com — found 2 prices" is composed here, from the workspace's own
 * source row for that host. A read whose host has no source row keeps its
 * nought and renders the sentence without a number, which is the honest one:
 * an errand saves nothing and still read the page.
 */
private fun TaskActivityView.toTimelineEntry(sources: List<SourceRecord>): TaskTimelineEntry {
    val kind = kind.toUiTimelineKind()
    val joined = if (kind == TaskTimelineKind.READ_PAGE && count == 0u) {
        sources.firstOrNull { !it.excluded && it.host == host }?.factCount ?: 0
    } else {
        count.toIntSaturating()
    }
    return TaskTimelineEntry(
        sequence = sequence.toLongSaturating(),
        kind = kind,
        host = host,
        count = joined,
        atEpochMillis = at_epoch_ms.toLongSaturating(),
    )
}

private fun TaskActivityKind.toUiTimelineKind(): TaskTimelineKind = when (this) {
    TaskActivityKind.OPENED_PAGE -> TaskTimelineKind.OPENED_PAGE
    TaskActivityKind.READ_PAGE -> TaskTimelineKind.READ_PAGE
    TaskActivityKind.PAGE_UNAVAILABLE -> TaskTimelineKind.PAGE_UNAVAILABLE
    TaskActivityKind.MOVE_REFUSED -> TaskTimelineKind.MOVE_REFUSED
    TaskActivityKind.ASKED_YOU -> TaskTimelineKind.ASKED_YOU
    TaskActivityKind.YOU_ANSWERED -> TaskTimelineKind.YOU_ANSWERED
    TaskActivityKind.HANDED_BACK -> TaskTimelineKind.HANDED_BACK
    TaskActivityKind.YOU_TOOK_OVER -> TaskTimelineKind.YOU_TOOK_OVER
    TaskActivityKind.BUILT_OUTPUT -> TaskTimelineKind.BUILT_OUTPUT
}

// A step is one line on one screen, so a number too large for the platform type
// is clamped rather than dropping the step: the sentence stays true about what
// happened, which is what the line is for. Neither bound is reachable — the
// record holds thirty-two steps and the clock is milliseconds — and clamping is
// what keeps that sentence from resting on an assumption.
private fun ULong.toLongSaturating(): Long = if (this > Long.MAX_VALUE.toULong()) Long.MAX_VALUE else toLong()

private fun UInt.toIntSaturating(): Int = if (this > Int.MAX_VALUE.toUInt()) Int.MAX_VALUE else toInt()

private fun TaskTemplateId.toUiTemplate(): TaskTemplate = when (this) {
    TaskTemplateId.COMPARE_PRODUCTS -> TaskTemplate.COMPARE_PRODUCTS
    TaskTemplateId.SUMMARIZE_EVIDENCE -> TaskTemplate.SUMMARIZE_EVIDENCE
    TaskTemplateId.BUILD_SOURCE_TABLE -> TaskTemplate.BUILD_A_SOURCE_TABLE
    TaskTemplateId.WEB_ERRAND -> TaskTemplate.WEB_ERRAND
}

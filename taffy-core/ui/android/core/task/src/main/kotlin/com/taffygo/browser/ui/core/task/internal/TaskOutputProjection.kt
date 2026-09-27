// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import taffy.core_api.WorkspaceFactKind
import taffy.core_api.WorkspaceFactView
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceSourceView
import taffy.core_api.WorkspaceViewState

/** One all-or-nothing task-screen projection of an already validated workspace. */
internal data class TaskOutputProjection(
    val workspaceId: String,
    val workspaceRevision: ULong,
    val saved: Boolean,
    val canSave: Boolean,
    val canDiscard: Boolean,
    val sources: List<SourceRecord>,
    val facts: List<Fact>,
)

/**
 * Joins the task view to its workspace without inventing a second output store.
 *
 * Numeric narrowing is all-or-nothing. A malformed timestamp or count must not
 * leave a screen showing some sources and facts while silently dropping the
 * row that failed to fit the platform model.
 *
 * **It no longer derives a timeline.** It used to: one step per workspace
 * source, plus one for the output, ordered by when a fact landed. That is a
 * picture of what was saved rather than of what happened, so an errand that
 * saves nothing had no timeline at all and a page read three times appeared
 * once. The steps now come from the task's own record (decision 0148) and this
 * projection supplies only the per-source fact count that record deliberately
 * does not carry — facts belong to the workspace, and the reducer never sees
 * one.
 */
internal fun WorkspaceViewState.toTaskOutputProjectionOrNull(): TaskOutputProjection? {
    val projectedSources = sources.map { it.toTaskSourceOrNull() ?: return null }
    val projectedFacts = facts.map(WorkspaceFactView::toTaskFact)
    return TaskOutputProjection(
        workspaceId = workspace_id,
        workspaceRevision = revision,
        saved = saved,
        canSave = !saved && phase.isSavable(),
        canDiscard = !saved && phase.isTerminal(),
        sources = projectedSources,
        facts = projectedFacts,
    )
}

private fun WorkspaceSourceView.toTaskSourceOrNull(): SourceRecord? {
    val readAt = read_at_epoch_ms.toLongOrNull() ?: return null
    val count = fact_count.toIntOrNull() ?: return null
    return SourceRecord(
        id = SourceId(source_id),
        title = title,
        host = host,
        readAtEpochMillis = readAt,
        factCount = count,
        excluded = excluded,
    )
}

private fun WorkspaceFactView.toTaskFact(): Fact = Fact(
    id = FactId(fact_id),
    field = field,
    value = value,
    kind = when (kind) {
        WorkspaceFactKind.FROM_PAGE -> FactKind.FROM_THE_PAGE
        WorkspaceFactKind.SUMMARIZED -> FactKind.SUMMARIZED
        WorkspaceFactKind.TAFFY_INFERENCE -> FactKind.TAFFY_INFERENCE
        WorkspaceFactKind.USER_ENTERED -> FactKind.YOU_ENTERED
    },
    sources = sources.map(::SourceId),
    correction = correction,
    hasConflict = has_conflict,
    needsANewSource = needs_new_source,
)

private fun ULong.toLongOrNull(): Long? = takeIf { it <= Long.MAX_VALUE.toULong() }?.toLong()

private fun UInt.toIntOrNull(): Int? = takeIf { it <= Int.MAX_VALUE.toUInt() }?.toInt()

private fun WorkspacePhase.isSavable(): Boolean = when (this) {
    WorkspacePhase.DONE,
    WorkspacePhase.PARTLY_DONE,
    -> true
    WorkspacePhase.RUNNING,
    WorkspacePhase.WAITING_FOR_USER,
    WorkspacePhase.PAUSED,
    WorkspacePhase.STOPPED,
    WorkspacePhase.FAILED,
    -> false
}

private fun WorkspacePhase.isTerminal(): Boolean = when (this) {
    WorkspacePhase.DONE,
    WorkspacePhase.PARTLY_DONE,
    WorkspacePhase.STOPPED,
    WorkspacePhase.FAILED,
    -> true
    WorkspacePhase.RUNNING,
    WorkspacePhase.WAITING_FOR_USER,
    WorkspacePhase.PAUSED,
    -> false
}

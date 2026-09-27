// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.internal

import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceExport
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.model.WorkspaceDeletionPreview
import taffy.core_api.TaskTemplateId
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.WorkspaceExportView
import taffy.core_api.WorkspaceFactKind
import taffy.core_api.WorkspaceFactView
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceSourceView
import taffy.core_api.WorkspaceViewState

internal fun WorkspaceViewState.toUiWorkspaceOrNull(): Workspace? {
    val updated = last_updated_epoch_ms.toLongOrNull() ?: return null
    val projectedSources = sources.mapNotNull { it.toUiSourceOrNull() }
    if (projectedSources.size != sources.size) return null
    return Workspace(
        id = WorkspaceId(workspace_id),
        goal = goal,
        state = phase.toUiState(),
        lastUpdatedEpochMillis = updated,
        template = template_id.toUiTemplate(),
        sources = projectedSources,
        facts = facts.map(WorkspaceFactView::toUiFact),
        revision = revision,
        displayName = display_name,
        deletionPreview = deletion_preview?.let { preview ->
            WorkspaceDeletionPreview(
                sources = preview.sources,
                facts = preview.facts,
                artifactMetadata = preview.artifact_metadata,
                derivedIndexes = preview.derived_indexes,
                confirmationToken = preview.confirmation_token,
            )
        },
    )
}

internal fun WorkspaceExportView.toUiExport(): WorkspaceExport = WorkspaceExport(
    requestId = request_id,
    workspaceId = WorkspaceId(workspace_id),
    revision = revision,
    format = format.toUiFormat(),
    content = content,
)

private fun WorkspaceSourceView.toUiSourceOrNull(): SourceRecord? {
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

private fun WorkspaceFactView.toUiFact(): Fact = Fact(
    id = FactId(fact_id),
    field = field,
    value = value,
    kind = kind.toUiKind(),
    sources = sources.map(::SourceId),
    correction = correction,
    hasConflict = has_conflict,
    needsANewSource = needs_new_source,
)

private fun WorkspacePhase.toUiState(): TaskDisplayState = when (this) {
    WorkspacePhase.RUNNING -> TaskDisplayState.RUNNING
    WorkspacePhase.WAITING_FOR_USER -> TaskDisplayState.WAITING_FOR_YOU
    WorkspacePhase.PAUSED -> TaskDisplayState.PAUSED
    WorkspacePhase.DONE -> TaskDisplayState.DONE
    WorkspacePhase.PARTLY_DONE -> TaskDisplayState.PARTLY_DONE
    WorkspacePhase.STOPPED -> TaskDisplayState.STOPPED
    WorkspacePhase.FAILED -> TaskDisplayState.FAILED
}

private fun WorkspaceFactKind.toUiKind(): FactKind = when (this) {
    WorkspaceFactKind.FROM_PAGE -> FactKind.FROM_THE_PAGE
    WorkspaceFactKind.SUMMARIZED -> FactKind.SUMMARIZED
    WorkspaceFactKind.TAFFY_INFERENCE -> FactKind.TAFFY_INFERENCE
    WorkspaceFactKind.USER_ENTERED -> FactKind.YOU_ENTERED
}

private fun TaskTemplateId.toUiTemplate(): TaskTemplate = when (this) {
    TaskTemplateId.COMPARE_PRODUCTS -> TaskTemplate.COMPARE_PRODUCTS
    TaskTemplateId.SUMMARIZE_EVIDENCE -> TaskTemplate.SUMMARIZE_EVIDENCE
    TaskTemplateId.BUILD_SOURCE_TABLE -> TaskTemplate.BUILD_A_SOURCE_TABLE
    TaskTemplateId.WEB_ERRAND -> TaskTemplate.WEB_ERRAND
}

private fun WorkspaceExportFormat.toUiFormat(): ExportFormat = when (this) {
    WorkspaceExportFormat.MARKDOWN -> ExportFormat.MARKDOWN
    WorkspaceExportFormat.CSV -> ExportFormat.COMMA_SEPARATED
}

private fun ULong.toLongOrNull(): Long? = takeIf { it <= Long.MAX_VALUE.toULong() }?.toLong()

private fun UInt.toIntOrNull(): Int? = takeIf { it <= Int.MAX_VALUE.toUInt() }?.toInt()

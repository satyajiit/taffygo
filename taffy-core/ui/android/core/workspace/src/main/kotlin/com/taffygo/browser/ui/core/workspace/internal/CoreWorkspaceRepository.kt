// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.internal

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.api.submitCoreApiCommand
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceExport
import com.taffygo.browser.ui.core.model.WorkspaceId
import java.security.MessageDigest
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.WorkspaceExportView

/** Profile-owned projection over immutable Core API workspace status. */
internal class CoreWorkspaceRepository(
    private val core: CoreApiClient,
    lifetime: TaffyProfileLifetime,
) : WorkspaceRepository {
    override val availability: StateFlow<WorkspaceRepository.Availability> = core.status
        .map(CoreStatus::toWorkspaceAvailability)
        .distinctUntilChanged()
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toWorkspaceAvailability(),
        )

    override val workspaces: StateFlow<List<Workspace>> = core.status
        // Task, provider and account publications cannot change a saved
        // workspace. Re-projecting its nested facts and sources on every such
        // update used to allocate the entire workspace graph on the UI path.
        .distinctUntilChanged { previous, current ->
            previous.hasCompleteProjection() == current.hasCompleteProjection() &&
                sameWorkspaceProjectionVersion(previous.workspaces, current.workspaces)
        }
        .map(CoreStatus::toSavedWorkspaces)
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toSavedWorkspaces(),
        )

    override val latestExport: StateFlow<WorkspaceExport?> = core.status
        .distinctUntilChangedBy { status ->
            CompleteWorkspaceExportVersion(
                complete = status.hasCompleteProjection(),
                export = workspaceExportVersion(status.workspace_export),
            )
        }
        .map { status -> status.completeWorkspaceExport()?.toUiExport() }
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.completeWorkspaceExport()?.toUiExport(),
        )

    override fun workspace(id: WorkspaceId): Workspace? =
        workspaces.value.firstOrNull { it.id == id }

    override suspend fun correctFact(id: WorkspaceId, factId: FactId, value: String): TaffyResult<Unit> {
        val status = core.completeStatus()
            ?: return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = status.workspaces.firstSaved(id)
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        return submitCoreApiCommand {
            core.correctWorkspaceFact(id.value, current.revision, factId.value, value)
        }
    }

    override suspend fun excludeSource(id: WorkspaceId, sourceId: SourceId): TaffyResult<Unit> {
        val status = core.completeStatus()
            ?: return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = status.workspaces.firstSaved(id)
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        return submitCoreApiCommand {
            core.excludeWorkspaceSource(id.value, current.revision, sourceId.value)
        }
    }

    override fun renderExport(id: WorkspaceId, format: ExportFormat): String? {
        val current = core.completeStatus()?.workspaces?.firstSaved(id) ?: return null
        return latestExport.value?.takeIf {
            it.workspaceId == id && it.revision == current.revision && it.format == format
        }?.content
    }

    override suspend fun requestExport(id: WorkspaceId, format: ExportFormat): TaffyResult<Unit> {
        val status = core.completeStatus()
            ?: return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = status.workspaces.firstSaved(id)
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        return submitCoreApiCommand {
            core.requestWorkspaceExport(
                requestId = exportRequestId(id, current.revision, format),
                workspaceId = id.value,
                expectedRevision = current.revision,
                format = format.toCoreFormat(),
            )
        }
    }

    override suspend fun rename(
        id: WorkspaceId,
        expectedRevision: ULong,
        displayName: String,
    ): TaffyResult<Unit> {
        val status = core.completeStatus()
            ?: return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = status.workspaces.firstSaved(id)
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        if (current.revision != expectedRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand {
            core.renameWorkspace(id.value, expectedRevision, displayName)
        }
    }

    override suspend fun delete(
        id: WorkspaceId,
        expectedRevision: ULong,
        confirmationToken: String,
    ): TaffyResult<Unit> {
        val status = core.completeStatus()
            ?: return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = status.workspaces.firstSaved(id)
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        if (current.revision != expectedRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        val preview = current.deletion_preview
            ?: return TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION)
        if (preview.confirmation_token != confirmationToken) {
            return TaffyResult.Failure(FailureReason.INVALID_REQUEST)
        }
        return submitCoreApiCommand {
            core.deleteWorkspace(id.value, expectedRevision, confirmationToken)
        }
    }
}

private fun CoreStatus.toWorkspaceAvailability(): WorkspaceRepository.Availability =
    when (availability) {
        CoreAvailability.STARTING -> WorkspaceRepository.Availability.LOADING
        CoreAvailability.READY -> if (hasCompleteProjection()) {
            WorkspaceRepository.Availability.READY
        } else {
            WorkspaceRepository.Availability.UNAVAILABLE
        }
        CoreAvailability.UNAVAILABLE,
        CoreAvailability.CIRCUIT_OPEN,
        -> WorkspaceRepository.Availability.UNAVAILABLE
    }

private fun CoreStatus.toSavedWorkspaces(): List<Workspace> =
    workspaces.takeIf { hasCompleteProjection() }
        .orEmpty()
        .savedOnly()
        .mapNotNull { it.toUiWorkspaceOrNull() }

private fun CoreApiClient.completeStatus(): CoreStatus? =
    status.value.takeIf(CoreStatus::hasCompleteProjection)

private fun CoreStatus.completeWorkspaceExport(): WorkspaceExportView? =
    workspace_export.takeIf { hasCompleteProjection() }

/**
 * Exact allocation-free key comparison for the saved-workspace projection.
 *
 * A workspace revision owns its complete published row. Identity, revision,
 * saved state and order are therefore sufficient; any content change without
 * a revision change is a core protocol violation, not a UI cache event.
 */
internal fun sameWorkspaceProjectionVersion(
    previous: List<taffy.core_api.WorkspaceViewState>,
    current: List<taffy.core_api.WorkspaceViewState>,
): Boolean {
    if (previous.size != current.size) return false
    return previous.indices.all { index ->
        val before = previous[index]
        val after = current[index]
        before.workspace_id == after.workspace_id &&
            before.revision == after.revision &&
            before.saved == after.saved
    }
}

/** Stable identity of the exact deterministic workspace export. */
internal data class WorkspaceExportVersion(
    val requestId: String,
    val workspaceId: String,
    val revision: ULong,
    val format: WorkspaceExportFormat,
)

internal data class CompleteWorkspaceExportVersion(
    val complete: Boolean,
    val export: WorkspaceExportVersion?,
)

internal fun workspaceExportVersion(export: WorkspaceExportView?): WorkspaceExportVersion? =
    export?.let {
        WorkspaceExportVersion(
            requestId = it.request_id,
            workspaceId = it.workspace_id,
            revision = it.revision,
            format = it.format,
        )
    }

private fun List<taffy.core_api.WorkspaceViewState>.savedOnly() = filter { it.saved }

private fun List<taffy.core_api.WorkspaceViewState>.firstSaved(id: WorkspaceId) =
    firstOrNull { it.saved && it.workspace_id == id.value }

private fun exportRequestId(id: WorkspaceId, revision: ULong, format: ExportFormat): String {
    val input = "${id.value}\u0000$revision\u0000${format.label}".encodeToByteArray()
    val digest = MessageDigest.getInstance("SHA-256").digest(input)
    return buildString(71) {
        append("export-")
        digest.forEach { byte ->
            val value = byte.toInt() and 0xff
            append(HEX[value ushr 4])
            append(HEX[value and 0x0f])
        }
    }
}

private fun ExportFormat.toCoreFormat(): WorkspaceExportFormat = when (this) {
    ExportFormat.MARKDOWN -> WorkspaceExportFormat.MARKDOWN
    ExportFormat.COMMA_SEPARATED -> WorkspaceExportFormat.CSV
}

private const val HEX = "0123456789abcdef"

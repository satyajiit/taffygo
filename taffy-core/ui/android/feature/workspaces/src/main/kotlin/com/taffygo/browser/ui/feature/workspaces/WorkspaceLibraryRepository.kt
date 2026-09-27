// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.submitCoreApiCommand
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryEntryView
import taffy.core_api.LibraryRefreshPreviewView
import taffy.core_api.LibraryRefreshResultView

/** Live projection over the dedicated durable Library published by the core. */
internal class WorkspaceLibraryRepository(
    private val core: CoreApiClient,
    private val workspaces: WorkspaceRepository,
    scope: CoroutineScope,
) : LibraryRepository {
    private val libraryStatus = core.status
        // CoreStatus also changes for tasks, accounts, providers, Memory and
        // Saved Data. Re-sorting and rebuilding as many as 1,024 Library rows
        // for each unrelated publication is pure allocation on the UI thread.
        // A Library revision owns its complete entry set; the search identity
        // owns its result. Only either changing can change this projection.
        .distinctUntilChangedBy { librarySnapshotVersion(it.availability, it.library) }

    override val snapshot: StateFlow<LibraryRepository.Snapshot> = combine(
        libraryStatus,
        workspaces.workspaces,
        ::projectLibrary,
    )
        .stateIn(
            scope = scope,
            started = SharingStarted.Eagerly,
            initialValue = projectLibrary(core.status.value, workspaces.workspaces.value),
        )

    override val latestExport: StateFlow<LibraryRepository.Export?> = core.status
        // Export content is deterministic for an exact Library revision,
        // collection and format. Keep the already-projected String when a
        // task/provider status update republishes that same artifact.
        .distinctUntilChangedBy {
            libraryExportVersion(it.availability, it.library, it.library_export)
        }
        .map(::projectLibraryExport)
        .stateIn(
            scope = scope,
            started = SharingStarted.Eagerly,
            initialValue = projectLibraryExport(core.status.value),
        )

    override val canKeep: Boolean
        get() = libraryAvailable()

    override val canMutate: Boolean
        get() = libraryAvailable()

    override val canManageCollections: Boolean
        get() = libraryAvailable()

    override suspend fun search(query: String): TaffyResult<Unit> {
        val library = core.status.value.library
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        if (query.trim().isEmpty()) return TaffyResult.Success(Unit)
        return submitCoreApiCommand {
            core.searchLibrary(
                requestId = libraryRequestId("search", library.revision, null, query),
                query = query,
                limit = MAX_LIBRARY_SEARCH_RESULTS,
            )
        }
    }

    override suspend fun saveFact(
        workspaceId: String,
        expectedWorkspaceRevision: ULong,
        factId: String,
    ): TaffyResult<Unit> {
        val library = core.status.value.library
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = library.entries.firstOrNull {
            it.source_workspace_id == workspaceId && it.source_fact_id == factId
        }
        return submitCoreApiCommand {
            core.saveLibraryFact(
                workspaceId = workspaceId,
                expectedWorkspaceRevision = expectedWorkspaceRevision,
                factId = factId,
                expectedLibraryRevision = library.revision,
                expectedEntryRevision = current?.revision ?: 0uL,
            )
        }
    }

    override suspend fun removeItem(
        itemId: String,
        expectedEntryRevision: ULong,
    ): TaffyResult<Unit> {
        val library = core.status.value.library
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = library.entries.firstOrNull { it.entry_id == itemId }
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        if (current.revision != expectedEntryRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand {
            core.removeLibraryEntry(itemId, library.revision, expectedEntryRevision)
        }
    }

    override suspend fun startRefresh(
        preview: LibraryRepository.RefreshPreview,
    ): TaffyResult<Unit> {
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        val current = core.status.value.library.refresh_previews.firstOrNull {
            it.preview_id == preview.previewId &&
                it.collection_id == preview.collectionId &&
                it.library_revision == preview.libraryRevision &&
                it.source_workspace_revision == preview.workspaceRevision &&
                it.sources.size == preview.sources.size
        } ?: return TaffyResult.Failure(FailureReason.STALE_REVISION)
        if (current.toLibraryRefreshPreview() != preview) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand {
            core.startLibraryRefresh(
                previewId = preview.previewId,
                collectionId = preview.collectionId,
                expectedLibraryRevision = preview.libraryRevision,
                expectedWorkspaceRevision = preview.workspaceRevision,
                sourceCount = preview.sources.size.toUInt(),
            )
        }
    }

    override suspend fun requestExport(
        collectionId: String?,
        format: ExportFormat,
    ): TaffyResult<Unit> {
        val library = core.status.value.library
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        if (collectionId != null && library.entries.none { it.collection_id == collectionId }) {
            return TaffyResult.Failure(FailureReason.NOT_FOUND)
        }
        return submitCoreApiCommand {
            core.requestLibraryExport(
                requestId = libraryRequestId("export", library.revision, collectionId, format.label),
                expectedLibraryRevision = library.revision,
                collectionId = collectionId,
                format = format.toCoreFormat(),
            )
        }
    }

    override fun renderExport(collectionId: String?, format: ExportFormat): String? {
        val currentRevision = (snapshot.value as? LibraryRepository.Snapshot.Ready)?.revision
            ?: return null
        return latestExport.value?.takeIf {
            it.revision == currentRevision &&
                it.collectionId == collectionId &&
                it.format == format
        }?.content
    }

    override suspend fun renameCollection(
        collectionId: String,
        expectedRevision: ULong,
        displayName: String,
    ): TaffyResult<Unit> {
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        return workspaces.rename(WorkspaceId(collectionId), expectedRevision, displayName)
    }

    override suspend fun deleteCollection(
        collectionId: String,
        expectedRevision: ULong,
        confirmationToken: String,
    ): TaffyResult<Unit> {
        if (!libraryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        return workspaces.delete(WorkspaceId(collectionId), expectedRevision, confirmationToken)
    }

    private fun libraryAvailable(): Boolean {
        val status = core.status.value
        return status.availability == CoreAvailability.READY &&
            status.library.availability == LibraryAvailability.AVAILABLE
    }
}

/** Maps only explicitly kept Library entries; workspace and browsing rows never enter here. */
internal fun projectLibrary(
    status: taffy.core_api.CoreStatus,
    workspaces: List<Workspace> = emptyList(),
): LibraryRepository.Snapshot =
    when (status.availability) {
        CoreAvailability.STARTING -> LibraryRepository.Snapshot.Loading
        CoreAvailability.UNAVAILABLE,
        CoreAvailability.CIRCUIT_OPEN,
        -> LibraryRepository.Snapshot.Unavailable
        CoreAvailability.READY -> when (status.library.availability) {
            LibraryAvailability.AVAILABLE -> readyLibrary(status, workspaces)
            LibraryAvailability.PRIVATE_PROFILE,
            LibraryAvailability.UNAVAILABLE,
            -> LibraryRepository.Snapshot.Unavailable
        }
    }

private fun projectLibraryExport(status: taffy.core_api.CoreStatus): LibraryRepository.Export? {
    if (status.availability != CoreAvailability.READY ||
        status.library.availability != LibraryAvailability.AVAILABLE
    ) {
        return null
    }
    val export = status.library_export
        ?.takeIf { it.library_revision == status.library.revision }
        ?: return null
    return LibraryRepository.Export(
        collectionId = export.collection_id,
        revision = export.library_revision,
        format = export.format.toUiFormat(),
        content = export.content,
    )
}

private fun readyLibrary(
    status: taffy.core_api.CoreStatus,
    workspaces: List<Workspace>,
): LibraryRepository.Snapshot.Ready {
    val entries = status.library.entries.sortedWith(
        compareBy<LibraryEntryView> { it.collection_id }.thenBy { it.entry_id },
    )
    val collections = ArrayList<LibraryRepository.Collection>()
    val workspacesById = workspaces.associateBy { it.id.value }
    val refreshPreviews = status.library.refresh_previews.associateBy { it.collection_id }
    val refreshResults = status.library.refresh_results.associateBy { it.collection_id }
    var start = 0
    while (start < entries.size) {
        val collectionId = entries[start].collection_id
        var end = start + 1
        while (end < entries.size && entries[end].collection_id == collectionId) end += 1
        collections += libraryCollection(
            collectionId,
            entries.subList(start, end),
            status.library.revision,
            workspacesById[collectionId],
            refreshPreviews[collectionId],
            refreshResults[collectionId],
        )
        start = end
    }
    val search = status.library.search
        ?.takeIf { it.library_revision == status.library.revision }
        ?.let { result ->
            LibraryRepository.Search(
                query = result.query,
                itemIds = result.hits.mapTo(linkedSetOf()) { it.entry_id },
            )
        }
    return LibraryRepository.Snapshot.Ready(
        collections = collections,
        revision = status.library.revision,
        search = search,
    )
}

internal fun libraryCollection(
    collectionId: String,
    entries: List<LibraryEntryView>,
    libraryRevision: ULong,
    workspace: Workspace? = null,
    refreshPreview: LibraryRefreshPreviewView? = null,
    refreshResult: LibraryRefreshResultView? = null,
): LibraryRepository.Collection {
    val projectedPreview = refreshPreview
        ?.takeIf {
            it.library_revision == libraryRevision &&
                workspace?.revision == it.source_workspace_revision
        }
        ?.toLibraryRefreshPreview()
    val projectedResult = refreshResult
        ?.takeIf { result -> result.preview_id == projectedPreview?.previewId }
        ?.toLibraryRefreshResult()
    val sourcesByEntry = entries.map { entry -> entry.sources.map { it.source_id } }
    val relatedByEntry = relatedLibraryEntryIndices(sourcesByEntry, MAX_RELATED_LIBRARY_ITEMS)
    val items = entries.mapIndexed { index, entry ->
        LibraryRepository.Item(
            id = entry.entry_id,
            collectionId = entry.collection_id,
            title = entry.field,
            revision = entry.revision,
            body = entry.correction ?: entry.original_value,
            sources = entry.sources.map { source ->
                LibraryRepository.Source(host = source.host, title = source.title)
            },
            capturedAt = libraryDate(entry.captured_at_epoch_ms),
            freshness = libraryFreshness(entry.last_checked_epoch_ms),
            hasConflict = entry.has_conflict,
            conflictSummary = libraryConflictSummary(entry),
            related = relatedByEntry[index].map { relatedIndex ->
                val candidate = entries[relatedIndex]
                LibraryRepository.RelatedItem(
                    collectionId = candidate.collection_id,
                    itemId = candidate.entry_id,
                    title = candidate.field,
                )
            },
        )
    }
    return LibraryRepository.Collection(
        id = collectionId,
        name = workspace?.displayName ?: entries.first().collection_name,
        revision = workspace?.revision ?: libraryRevision,
        goal = workspace?.goal ?: entries.first().collection_name,
        deletionPreview = workspace?.deletionPreview,
        canManage = workspace != null,
        // A collection never claims to be newer than its oldest kept check.
        freshness = entries
            .minOfOrNull(LibraryEntryView::last_checked_epoch_ms)
            ?.takeIf { it != 0uL }
            ?.let(::libraryFreshness),
        conflictCount = entries.count { it.has_conflict },
        refreshPreview = projectedPreview,
        refreshResult = projectedResult,
        items = items,
    )
}

private const val MAX_LIBRARY_SEARCH_RESULTS = 32u
private const val MAX_RELATED_LIBRARY_ITEMS = 8

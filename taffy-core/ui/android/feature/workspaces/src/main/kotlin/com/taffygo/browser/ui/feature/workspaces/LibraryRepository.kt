// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.WorkspaceDeletionPreview
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Things the person asked Taffy to keep.
 *
 * Empty Ready is honest: nothing has been kept, and nothing here invents a
 * fact, a page extract, a file, or a workspace. Conflict counts are whatever
 * the port supplies; they are omitted when it supplies none.
 */
interface LibraryRepository {

    /** Collections and their items, or why they cannot be shown. */
    val snapshot: StateFlow<Snapshot>

    /** Latest exact Rust-rendered Library export, or no current artifact. */
    val latestExport: StateFlow<Export?>

    /** Whether a cited fact from a saved workspace can be explicitly kept. */
    val canKeep: Boolean

    /** Whether item-level refresh or mutation can actually run. */
    val canMutate: Boolean

    /** Whether workspace-backed collection rename, export, and delete are live. */
    val canManageCollections: Boolean
        get() = false

    /** Ask the portable core for a bounded deterministic search. */
    suspend fun search(query: String): TaffyResult<Unit> =
        TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** Promote one exact saved-workspace fact; nothing is copied from browsing. */
    suspend fun saveFact(
        workspaceId: String,
        expectedWorkspaceRevision: ULong,
        factId: String,
    ): TaffyResult<Unit> = TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** Remove an entry and all of its cited and indexed content. */
    suspend fun removeItem(itemId: String, expectedEntryRevision: ULong): TaffyResult<Unit> =
        TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** Start only the exact refresh the person reviewed and approved. */
    suspend fun startRefresh(preview: RefreshPreview): TaffyResult<Unit> =
        TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** Generate an exact-revision export for one collection or all of Library. */
    suspend fun requestExport(
        collectionId: String?,
        format: ExportFormat,
    ): TaffyResult<Unit> = TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** Return only an export matching the currently published Library revision. */
    fun renderExport(collectionId: String?, format: ExportFormat): String? = null

    suspend fun renameCollection(
        collectionId: String,
        expectedRevision: ULong,
        displayName: String,
    ): TaffyResult<Unit> = TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    suspend fun deleteCollection(
        collectionId: String,
        expectedRevision: ULong,
        confirmationToken: String,
    ): TaffyResult<Unit> = TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** What Library can currently show. */
    sealed interface Snapshot {
        /** The store has not answered yet. */
        data object Loading : Snapshot

        /** The store is not connected. */
        data object Unavailable : Snapshot

        /** The store answered. An empty list is nothing kept, not a failure. */
        data class Ready(
            val collections: List<Collection>,
            val revision: ULong = 0uL,
            val search: Search? = null,
        ) : Snapshot
    }

    /** Exact result identities from the core's latest bounded search. */
    data class Search(val query: String, val itemIds: Set<String>)

    /** Exact export content stays here until the trusted document writer asks for it. */
    data class Export(
        val collectionId: String?,
        val revision: ULong,
        val format: ExportFormat,
        val content: String,
    )

    /**
     * One named pile of kept items.
     *
     * [conflictCount] is shown only when greater than zero. [freshness] is a
     * display sentence the port already wrote, or absent.
     */
    data class Collection(
        val id: String,
        val name: String,
        val revision: ULong = 0uL,
        val goal: String = name,
        val deletionPreview: WorkspaceDeletionPreview? = null,
        val canManage: Boolean = false,
        val freshness: String? = null,
        val conflictCount: Int = 0,
        val refreshPreview: RefreshPreview? = null,
        val refreshResult: RefreshResult? = null,
        val items: List<Item> = emptyList(),
    ) {
        /** How many items this collection holds. */
        val itemCount: Int
            get() = items.size
    }

    /**
     * One kept fact, page extract, file, or workspace output.
     *
     * [conflictSummary] is both claims, when the port has them. A conflict
     * badge without that sentence still says the sources disagree; it does not
     * invent the claims.
     */
    data class Item(
        val id: String,
        val collectionId: String,
        val title: String,
        val revision: ULong = 0uL,
        val body: String = "",
        val sources: List<Source> = emptyList(),
        val capturedAt: String? = null,
        val freshness: String? = null,
        val hasConflict: Boolean = false,
        val conflictSummary: String? = null,
        val related: List<RelatedItem> = emptyList(),
    )

    /** Where one kept item came from. */
    data class Source(
        val host: String,
        val title: String = "",
    )

    /** Another kept item that still shows its own sources. */
    data class RelatedItem(
        val collectionId: String,
        val itemId: String,
        val title: String,
    )

    /** Content-free work the core can perform after one explicit approval. */
    data class RefreshPreview(
        val previewId: String,
        val collectionId: String,
        val libraryRevision: ULong,
        val workspaceRevision: ULong,
        val navigationCount: UInt,
        val observationCount: UInt,
        val sources: List<RefreshSource>,
    )

    /** One saved page named in the preview; its locator never reaches the UI. */
    data class RefreshSource(
        val id: String,
        val title: String,
        val host: String,
    )

    /** Content-free terminal comparison against the preserved original. */
    data class RefreshResult(
        val previewId: String,
        val collectionId: String,
        val items: List<RefreshResultItem>,
    ) {
        fun count(disposition: RefreshDisposition): Int =
            items.count { it.disposition == disposition }
    }

    data class RefreshResultItem(
        val sourceId: String,
        val disposition: RefreshDisposition,
    )

    enum class RefreshDisposition {
        CHANGED,
        MISSING,
        UNCHANGED,
    }
}

/** Ready, with nothing kept. Keep this cannot add anything. */
internal class EmptyLibraryRepository : LibraryRepository {
    override val snapshot: StateFlow<LibraryRepository.Snapshot> =
        MutableStateFlow(LibraryRepository.Snapshot.Ready(emptyList()))
    override val latestExport: StateFlow<LibraryRepository.Export?> = MutableStateFlow(null)
    override val canKeep: Boolean = false
    override val canMutate: Boolean = false
}

/** The store is not connected. Nothing is listed, and nothing is added. */
internal class UnavailableLibraryRepository : LibraryRepository {
    override val snapshot: StateFlow<LibraryRepository.Snapshot> =
        MutableStateFlow(LibraryRepository.Snapshot.Unavailable)
    override val latestExport: StateFlow<LibraryRepository.Export?> = MutableStateFlow(null)
    override val canKeep: Boolean = false
    override val canMutate: Boolean = false
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.api.SavedDataCoreApiClient
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.MemoryScopeKind
import taffy.core_api.MemorySensitivity
import taffy.core_api.MemoryWorkspaceView

/** Exact-revision Core API forwarding for saved workspaces and the Library. */
internal class CoreApiSavedWorkOperations(
    private val proxy: TaffyProfileCoreApi,
    private val submissions: CoreApiSubmissionDispatcher,
) : SavedDataCoreApiClient {
    suspend fun correctWorkspaceFact(
        workspaceId: String,
        expectedRevision: ULong,
        factId: String,
        value: String,
    ) = submissions.submit { callback ->
        proxy.correctWorkspaceFact(
            workspaceId,
            expectedRevision.toLong(),
            factId,
            value,
            callback,
        )
    }

    suspend fun excludeWorkspaceSource(
        workspaceId: String,
        expectedRevision: ULong,
        sourceId: String,
    ) = submissions.submit { callback ->
        proxy.excludeWorkspaceSource(workspaceId, expectedRevision.toLong(), sourceId, callback)
    }

    suspend fun requestWorkspaceExport(
        requestId: String,
        workspaceId: String,
        expectedRevision: ULong,
        format: WorkspaceExportFormat,
    ) = submissions.submit { callback ->
        proxy.requestWorkspaceExport(
            requestId,
            workspaceId,
            expectedRevision.toLong(),
            format.wire.toInt(),
            callback,
        )
    }

    suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong) =
        submissions.submit { callback ->
            proxy.saveWorkspace(workspaceId, expectedRevision.toLong(), callback)
        }

    suspend fun renameWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        displayName: String,
    ) = submissions.submit { callback ->
        proxy.renameWorkspace(workspaceId, expectedRevision.toLong(), displayName, callback)
    }

    suspend fun deleteWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        confirmationToken: String,
    ) = submissions.submit { callback ->
        proxy.deleteWorkspace(
            workspaceId,
            expectedRevision.toLong(),
            confirmationToken,
            callback,
        )
    }

    suspend fun discardWorkspace(workspaceId: String, expectedRevision: ULong) =
        submissions.submit { callback ->
            proxy.discardWorkspace(workspaceId, expectedRevision.toLong(), callback)
        }

    suspend fun searchLibrary(requestId: String, query: String, limit: UInt) =
        submissions.submit { callback ->
            proxy.searchLibrary(requestId, query, limit.toInt(), callback)
        }

    suspend fun startLibraryRefresh(
        previewId: String,
        collectionId: String,
        expectedLibraryRevision: ULong,
        expectedWorkspaceRevision: ULong,
        sourceCount: UInt,
    ) = submissions.submit { callback ->
        proxy.startLibraryRefresh(
            previewId,
            collectionId,
            expectedLibraryRevision.toLong(),
            expectedWorkspaceRevision.toLong(),
            sourceCount.toInt(),
            callback,
        )
    }

    suspend fun saveLibraryFact(
        workspaceId: String,
        expectedWorkspaceRevision: ULong,
        factId: String,
        expectedLibraryRevision: ULong,
        expectedEntryRevision: ULong,
    ) = submissions.submit { callback ->
        proxy.saveLibraryFact(
            workspaceId,
            expectedWorkspaceRevision.toLong(),
            factId,
            expectedLibraryRevision.toLong(),
            expectedEntryRevision.toLong(),
            callback,
        )
    }

    suspend fun removeLibraryEntry(
        entryId: String,
        expectedLibraryRevision: ULong,
        expectedEntryRevision: ULong,
    ) = submissions.submit { callback ->
        proxy.removeLibraryEntry(
            entryId,
            expectedLibraryRevision.toLong(),
            expectedEntryRevision.toLong(),
            callback,
        )
    }

    suspend fun requestLibraryExport(
        requestId: String,
        expectedLibraryRevision: ULong,
        collectionId: String?,
        format: WorkspaceExportFormat,
    ) = submissions.submit { callback ->
        proxy.requestLibraryExport(
            requestId,
            expectedLibraryRevision.toLong(),
            collectionId,
            format.wire.toInt(),
            callback,
        )
    }

    suspend fun searchMemory(requestId: String, query: String, limit: UInt) =
        submissions.submit { callback ->
            proxy.searchMemory(requestId, query, limit.toInt(), callback)
        }

    suspend fun upsertMemory(
        memoryId: String?,
        statement: String,
        scopeKind: MemoryScopeKind,
        scopeWorkspace: MemoryWorkspaceView?,
        sensitivity: MemorySensitivity,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
        expiresAtEpochMillis: ULong,
    ) = submissions.submit { callback ->
        val projectedWorkspace = scopeWorkspace?.let { workspace ->
            org.chromium.taffy.core_api.mojom.MemoryWorkspaceView().apply {
                workspaceId = workspace.workspace_id
                displayName = workspace.display_name
            }
        }
        proxy.upsertMemory(
            memoryId,
            statement,
            scopeKind.wire.toInt(),
            projectedWorkspace,
            sensitivity.wire.toInt(),
            expectedMemoryRevision.toLong(),
            expectedRecordRevision.toLong(),
            expiresAtEpochMillis.toLong(),
            callback,
        )
    }

    suspend fun deleteMemory(
        memoryId: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ) = submissions.submit { callback ->
        proxy.deleteMemory(
            memoryId,
            expectedMemoryRevision.toLong(),
            expectedRecordRevision.toLong(),
            callback,
        )
    }

    override suspend fun upsertSavedDetail(
        expectedRevision: ULong,
        detailId: String?,
        givenName: String,
        familyName: String,
        email: String,
        phone: String,
        address: String,
        postcode: String,
        country: String,
    ) = submissions.submit { callback ->
        proxy.upsertSavedDetail(
            expectedRevision.toLong(),
            detailId,
            givenName,
            familyName,
            email,
            phone,
            address,
            postcode,
            country,
            callback,
        )
    }

    override suspend fun deleteSavedDetail(detailId: String, expectedRevision: ULong) =
        submissions.submit { callback ->
            proxy.deleteSavedDetail(detailId, expectedRevision.toLong(), callback)
        }

    override suspend fun deleteSavedSignIn(signInId: String, expectedRevision: ULong) =
        submissions.submit { callback ->
            proxy.deleteSavedSignIn(signInId, expectedRevision.toLong(), callback)
        }
}

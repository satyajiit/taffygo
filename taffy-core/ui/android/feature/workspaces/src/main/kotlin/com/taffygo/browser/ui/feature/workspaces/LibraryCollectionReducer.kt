// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * What screen SCR-502 shows.
 *
 * Refresh, export, and remove never look succeeded in the projection: those
 * flags come from the port, and Empty and Unavailable leave them off.
 */
internal fun projectLibraryCollection(
    snapshot: LibraryRepository.Snapshot,
    collectionId: String,
    query: String,
    canMutate: Boolean,
    canManage: Boolean = false,
): LibraryCollectionUiState = when (snapshot) {
    LibraryRepository.Snapshot.Loading -> LibraryCollectionUiState(
        loading = true,
        collectionId = collectionId,
        query = query,
        canMutate = canMutate,
        canManage = canManage,
    )
    LibraryRepository.Snapshot.Unavailable -> LibraryCollectionUiState(
        unavailable = true,
        collectionId = collectionId,
        query = query,
        canMutate = canMutate,
        canManage = canManage,
    )
    is LibraryRepository.Snapshot.Ready -> {
        val collection = snapshot.collections.firstOrNull { it.id == collectionId }
        if (collection == null) {
            LibraryCollectionUiState(
                missing = true,
                collectionId = collectionId,
                query = query,
                canMutate = canMutate,
                canManage = canManage,
            )
        } else {
            LibraryCollectionUiState(
                collectionId = collection.id,
                name = collection.name,
                revision = collection.revision,
                goal = collection.goal,
                deletionPreview = collection.deletionPreview,
                freshness = collection.freshness,
                conflictCount = collection.conflictCount,
                refreshPreview = collection.refreshPreview,
                refreshResult = collection.refreshResult,
                items = if (query.trim().isEmpty()) {
                    collection.items
                } else {
                    val itemIds = snapshot.search
                        ?.takeIf { it.query == query }
                        ?.itemIds
                        .orEmpty()
                    collection.items.filter { it.id in itemIds }
                },
                query = query,
                totalCount = collection.items.size,
                canMutate = canMutate,
                canManage = canManage && collection.canManage,
            )
        }
    }
}

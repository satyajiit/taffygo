// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * What screen SCR-503 shows.
 *
 * Related items and conflict claims appear only when the port named them.
 */
internal fun projectLibraryItem(
    snapshot: LibraryRepository.Snapshot,
    collectionId: String,
    itemId: String,
    canMutate: Boolean,
): LibraryItemUiState = when (snapshot) {
    LibraryRepository.Snapshot.Loading -> LibraryItemUiState(
        loading = true,
        collectionId = collectionId,
        itemId = itemId,
        canMutate = canMutate,
    )
    LibraryRepository.Snapshot.Unavailable -> LibraryItemUiState(
        unavailable = true,
        collectionId = collectionId,
        itemId = itemId,
        canMutate = canMutate,
    )
    is LibraryRepository.Snapshot.Ready -> {
        val item = snapshot.collections
            .firstOrNull { it.id == collectionId }
            ?.items
            ?.firstOrNull { it.id == itemId }
        if (item == null) {
            LibraryItemUiState(
                missing = true,
                collectionId = collectionId,
                itemId = itemId,
                canMutate = canMutate,
            )
        } else {
            LibraryItemUiState(
                collectionId = item.collectionId,
                itemId = item.id,
                title = item.title,
                body = item.body,
                sources = item.sources,
                capturedAt = item.capturedAt,
                freshness = item.freshness,
                hasConflict = item.hasConflict,
                conflictSummary = item.conflictSummary,
                related = item.related,
                canMutate = canMutate,
            )
        }
    }
}

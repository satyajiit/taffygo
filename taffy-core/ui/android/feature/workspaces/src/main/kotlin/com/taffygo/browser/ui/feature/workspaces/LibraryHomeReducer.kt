// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * What screen SCR-501 shows.
 *
 * Search matches a collection's name or any item it holds, because a kept fact
 * is often what the person remembers. Nothing is invented when the snapshot is
 * empty or unavailable.
 */
internal fun projectLibraryHome(
    snapshot: LibraryRepository.Snapshot,
    query: String,
): LibraryHomeUiState = when (snapshot) {
    LibraryRepository.Snapshot.Loading -> LibraryHomeUiState(loading = true, query = query)
    LibraryRepository.Snapshot.Unavailable ->
        LibraryHomeUiState(unavailable = true, query = query)
    is LibraryRepository.Snapshot.Ready -> {
        val matched = if (query.trim().isEmpty()) {
            snapshot.collections
        } else {
            val itemIds = snapshot.search
                ?.takeIf { it.query == query }
                ?.itemIds
                .orEmpty()
            snapshot.collections.filter { collection ->
                collection.items.any { it.id in itemIds }
            }
        }
        LibraryHomeUiState(
            collections = matched,
            query = query,
            totalCount = snapshot.collections.size,
        )
    }
}

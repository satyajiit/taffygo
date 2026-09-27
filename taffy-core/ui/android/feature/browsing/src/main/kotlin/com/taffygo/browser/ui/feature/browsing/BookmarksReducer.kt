// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * What screen SCR-202 shows.
 *
 * Search matches title and host. A non-empty query searches every folder, so
 * a typed name is not hidden behind the folder the person last opened. An
 * empty All folder is not a section — it is empty.
 */
internal fun projectBookmarks(
    snapshot: BookmarksSnapshot,
    query: String,
    currentFolderId: BookmarkFolder.Id? = null,
    editing: Bookmark? = null,
): BookmarksUiState {
    return when (snapshot) {
        BookmarksSnapshot.Loading -> BookmarksUiState(
            query = query,
            availability = BookmarksUiState.Availability.LOADING,
            currentFolderId = currentFolderId,
            editing = editing,
        )
        BookmarksSnapshot.Unavailable -> BookmarksUiState(
            query = query,
            availability = BookmarksUiState.Availability.UNAVAILABLE,
            currentFolderId = currentFolderId,
            editing = editing,
        )
        is BookmarksSnapshot.Ready -> {
            val trimmed = query.trim()
            val total = snapshot.folders.sumOf { it.bookmarks.size }
            val searched = snapshot.folders.map { folder ->
                folder.copy(
                    bookmarks = folder.bookmarks.filter { bookmark ->
                        trimmed.isEmpty() ||
                            bookmark.title.contains(trimmed, ignoreCase = true) ||
                            bookmark.host.contains(trimmed, ignoreCase = true)
                    },
                )
            }
            val scoped = if (trimmed.isNotEmpty() || currentFolderId == null) {
                searched
            } else {
                searched.map { folder ->
                    if (folder.id == currentFolderId) {
                        folder
                    } else {
                        folder.copy(bookmarks = emptyList())
                    }
                }
            }
            BookmarksUiState(
                query = query,
                availability = BookmarksUiState.Availability.READY,
                folders = scoped,
                totalCount = total,
                currentFolderId = currentFolderId,
                editing = editing?.let { current ->
                    snapshot.folders.asSequence()
                        .flatMap { it.bookmarks }
                        .firstOrNull { it.id == current.id }
                },
            )
        }
    }
}

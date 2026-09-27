// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.StateFlow

/**
 * Starred pages for screen SCR-202, and the writer Save page calls.
 *
 * Portable transfer is explicit and bounded. It never reaches history,
 * passwords, private tabs, or Taffy's Library and Memory stores.
 */
interface BookmarksRepository {
    /** The current folders, or the honest absence of a list. */
    val snapshot: StateFlow<BookmarksSnapshot>

    /** Open the exact address still bound to [id], or refuse a stale identifier. */
    suspend fun open(id: Bookmark.Id): Boolean

    /** Star a page. Returns the id the list will show. */
    suspend fun add(
        title: String,
        host: String,
        folderId: BookmarkFolder.Id = BookmarkFolder.Id.ALL,
    ): Bookmark.Id

    /** Rename a bookmark, or move it to another folder. */
    suspend fun edit(id: Bookmark.Id, title: String, folderId: BookmarkFolder.Id)

    /** Remove one star the person asked to drop. */
    suspend fun delete(id: Bookmark.Id)

    /** Read a bounded exact-address tree for a person-selected export destination. */
    suspend fun exportDocument(): BookmarkTransferDocument?

    /** Merge a decoded portable tree, skipping duplicates and refusing unsafe addresses. */
    suspend fun importDocument(
        document: BookmarkTransferDocument,
    ): BookmarkTransferDocument.ImportResult?
}

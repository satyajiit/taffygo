// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * A bookmark list this build cannot read.
 *
 * Writes are refused so Save page cannot claim a star that will not appear.
 */
class UnavailableBookmarksRepository : BookmarksRepository {
    override val snapshot: StateFlow<BookmarksSnapshot> =
        MutableStateFlow(BookmarksSnapshot.Unavailable).asStateFlow()

    override suspend fun open(id: Bookmark.Id): Boolean = false

    override suspend fun add(
        title: String,
        host: String,
        folderId: BookmarkFolder.Id,
    ): Bookmark.Id = Bookmark.Id("")

    override suspend fun edit(id: Bookmark.Id, title: String, folderId: BookmarkFolder.Id) = Unit

    override suspend fun delete(id: Bookmark.Id) = Unit

    override suspend fun exportDocument(): BookmarkTransferDocument? = null

    override suspend fun importDocument(
        document: BookmarkTransferDocument,
    ): BookmarkTransferDocument.ImportResult? = null
}

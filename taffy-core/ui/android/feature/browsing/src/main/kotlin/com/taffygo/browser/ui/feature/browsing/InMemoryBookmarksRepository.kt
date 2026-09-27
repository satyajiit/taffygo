// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import java.net.URI
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * Starred pages that last for this window.
 *
 * Starts empty, with the default All folder and nothing in it. Save page can
 * write here in a later change; until then the list is honest empty rather
 * than a sample of pages nobody starred.
 */
class InMemoryBookmarksRepository : BookmarksRepository {

    private val nextId = AtomicLong(1)

    private val state = MutableStateFlow<BookmarksSnapshot>(
        BookmarksSnapshot.Ready(listOf(emptyAllFolder())),
    )

    override val snapshot: StateFlow<BookmarksSnapshot> = state.asStateFlow()

    override suspend fun open(id: Bookmark.Id): Boolean =
        (state.value as? BookmarksSnapshot.Ready)?.folders?.bookmark(id) != null

    override suspend fun add(
        title: String,
        host: String,
        folderId: BookmarkFolder.Id,
    ): Bookmark.Id {
        val id = Bookmark.Id("bm_${nextId.getAndIncrement()}")
        val target = resolvedFolderId(folderId)
        val bookmark = Bookmark(
            id = id,
            title = title.trim().ifEmpty { host },
            host = host,
            folderId = target,
            address = "https://$host/",
        )
        state.update { current ->
            val ready = current as? BookmarksSnapshot.Ready ?: return@update current
            BookmarksSnapshot.Ready(ready.folders.withBookmark(bookmark, target))
        }
        return id
    }

    override suspend fun edit(id: Bookmark.Id, title: String, folderId: BookmarkFolder.Id) {
        val target = resolvedFolderId(folderId)
        state.update { current ->
            val ready = current as? BookmarksSnapshot.Ready ?: return@update current
            val existing = ready.folders.bookmark(id) ?: return@update current
            val updated = existing.copy(
                title = title.trim().ifEmpty { existing.title },
                folderId = target,
            )
            val without = ready.folders.map { folder ->
                folder.copy(bookmarks = folder.bookmarks.filterNot { it.id == id })
            }
            BookmarksSnapshot.Ready(without.withBookmark(updated, target))
        }
    }

    override suspend fun delete(id: Bookmark.Id) {
        state.update { current ->
            val ready = current as? BookmarksSnapshot.Ready ?: return@update current
            BookmarksSnapshot.Ready(
                ready.folders.map { folder ->
                    folder.copy(bookmarks = folder.bookmarks.filterNot { it.id == id })
                },
            )
        }
    }

    override suspend fun exportDocument(): BookmarkTransferDocument? {
        val ready = state.value as? BookmarksSnapshot.Ready ?: return null
        val all = ready.folders.firstOrNull { it.id == BookmarkFolder.Id.ALL }
        return BookmarkTransferDocument(
            bookmarks = all?.bookmarks.orEmpty().map { bookmark ->
                BookmarkTransferDocument.Entry(bookmark.title, bookmark.address)
            },
            folders = ready.folders
                .filter { it.id != BookmarkFolder.Id.ALL }
                .map { folder ->
                    BookmarkTransferDocument.Folder(
                        title = folder.name.ifBlank { folder.id.value },
                        bookmarks = folder.bookmarks.map { bookmark ->
                            BookmarkTransferDocument.Entry(bookmark.title, bookmark.address)
                        },
                    )
                },
        )
    }

    override suspend fun importDocument(
        document: BookmarkTransferDocument,
    ): BookmarkTransferDocument.ImportResult {
        val existing = (state.value as? BookmarksSnapshot.Ready)
            ?.folders
            .orEmpty()
            .asSequence()
            .flatMap { it.bookmarks }
            .map(Bookmark::address)
            .toMutableSet()
        var imported = 0
        var duplicates = 0
        var rejected = document.rejectedEntries

        fun merge(entry: BookmarkTransferDocument.Entry, folder: String?) {
            val parsed = runCatching { URI(entry.address) }.getOrNull()
            val scheme = parsed?.scheme?.lowercase()
            val host = parsed?.host?.lowercase()?.takeIf(String::isNotBlank)
            if (parsed == null || (scheme != "http" && scheme != "https") || parsed.userInfo != null || host == null) {
                rejected++
                return
            }
            val address = parsed.toASCIIString()
            if (!existing.add(address)) {
                duplicates++
                return
            }
            val folderId = folder?.let { BookmarkFolder.Id("import-${it.hashCode().toUInt()}") }
                ?: BookmarkFolder.Id.ALL
            val id = Bookmark.Id("bm_${nextId.getAndIncrement()}")
            val bookmark = Bookmark(
                id = id,
                title = entry.title.trim().ifEmpty { host },
                host = host,
                folderId = folderId,
                address = address,
            )
            state.update { current ->
                val ready = current as? BookmarksSnapshot.Ready ?: return@update current
                val folders = ready.folders.ensureFolder(folderId, folder.orEmpty())
                BookmarksSnapshot.Ready(folders.withBookmark(bookmark, folderId))
            }
            imported++
        }

        fun mergeFolder(folder: BookmarkTransferDocument.Folder, path: List<String>) {
            val next = path + folder.title
            val label = next.joinToString(" / ")
            folder.bookmarks.forEach { merge(it, label) }
            folder.folders.forEach { mergeFolder(it, next) }
        }

        document.bookmarks.forEach { merge(it, null) }
        document.folders.forEach { mergeFolder(it, emptyList()) }
        return BookmarkTransferDocument.ImportResult(imported, duplicates, rejected)
    }

    private fun resolvedFolderId(folderId: BookmarkFolder.Id): BookmarkFolder.Id =
        if (folderId.value.isBlank()) BookmarkFolder.Id.ALL else folderId

    private companion object {
        fun emptyAllFolder(): BookmarkFolder =
            BookmarkFolder(id = BookmarkFolder.Id.ALL, name = "", bookmarks = emptyList())

        fun List<BookmarkFolder>.bookmark(id: Bookmark.Id): Bookmark? =
            asSequence().flatMap { it.bookmarks }.firstOrNull { it.id == id }

        fun List<BookmarkFolder>.withBookmark(
            bookmark: Bookmark,
            folderId: BookmarkFolder.Id,
        ): List<BookmarkFolder> {
            val present = any { it.id == folderId }
            val folders = if (present) this else this + BookmarkFolder(folderId, name = "")
            return folders.map { folder ->
                if (folder.id != folderId) {
                    folder
                } else {
                    folder.copy(bookmarks = listOf(bookmark) + folder.bookmarks.filterNot { it.id == bookmark.id })
                }
            }
        }

        fun List<BookmarkFolder>.ensureFolder(
            folderId: BookmarkFolder.Id,
            name: String,
        ): List<BookmarkFolder> = if (any { it.id == folderId }) {
            this
        } else {
            this + BookmarkFolder(folderId, name)
        }

    }
}

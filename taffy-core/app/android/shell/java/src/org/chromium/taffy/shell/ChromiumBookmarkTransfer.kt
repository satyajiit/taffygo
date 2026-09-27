// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.browsing.BookmarkTransferDocument
import java.util.ArrayDeque
import org.chromium.chrome.browser.bookmarks.BookmarkModel
import org.chromium.components.bookmarks.BookmarkId
import org.chromium.components.bookmarks.BookmarkItem
import org.chromium.components.bookmarks.BookmarkType
import org.chromium.url.GURL

/** Bounded portable transfer behind Chromium's profile-owned bookmark model. */
internal class ChromiumBookmarkTransfer(private val model: BookmarkModel) {
    fun exportDocument(): BookmarkTransferDocument? {
        if (!model.isBookmarkModelLoaded) return null
        val fallbackRoot = model.defaultBookmarkFolder ?: return null
        val budget = ReadBudget()
        val folders = mutableListOf<BookmarkTransferDocument.Folder>()
        profileRoots(fallbackRoot).forEach { root ->
            if (!budget.takeFolder()) return@forEach
            val item = model.getBookmarkById(root) ?: return@forEach
            val content = readChildren(root, depth = 1, budget)
            if (content.bookmarks.isEmpty() && content.folders.isEmpty()) return@forEach
            folders += BookmarkTransferDocument.Folder(
                title = boundedLabel(item.title, DEFAULT_FOLDER_TITLE, MAX_FOLDER_TITLE_CHARS),
                bookmarks = content.bookmarks,
                folders = content.folders,
            )
        }
        if (budget.overflowed) return null
        return BookmarkTransferDocument(folders = folders)
    }

    fun importDocument(
        document: BookmarkTransferDocument,
    ): BookmarkTransferDocument.ImportResult? {
        if (!model.isBookmarkModelLoaded) return null
        val root = model.defaultBookmarkFolder ?: return null
        val existing = readExistingAddresses(profileRoots(root)) ?: return null
        val counts = ImportCounts(rejected = document.rejectedEntries.coerceAtLeast(0))
        document.bookmarks.forEach { importEntry(root, it, existing, counts) }

        val pending = ArrayDeque<PendingFolder>()
        document.folders.asReversed().forEach { pending.addLast(PendingFolder(root, it, 1)) }
        while (pending.isNotEmpty()) {
            val next = pending.removeLast()
            if (counts.seenFolders >= MAX_FOLDERS || next.depth > MAX_DEPTH) {
                counts.rejectTree(next.folder)
                continue
            }
            counts.seenFolders++
            val parent = findOrCreateFolder(next.parent, next.folder.title)
            if (parent == null) {
                counts.rejectTree(next.folder)
                continue
            }
            next.folder.bookmarks.forEach { importEntry(parent, it, existing, counts) }
            next.folder.folders.asReversed().forEach { child ->
                pending.addLast(PendingFolder(parent, child, next.depth + 1))
            }
        }
        return counts.result()
    }

    private fun readChildren(parent: BookmarkId, depth: Int, budget: ReadBudget): FolderContent {
        val bookmarks = mutableListOf<BookmarkTransferDocument.Entry>()
        val folders = mutableListOf<BookmarkTransferDocument.Folder>()
        for (childId in model.getChildIds(parent)) {
            if (!budget.takeNode()) break
            val item = model.getBookmarkById(childId) ?: continue
            if (!item.isPortableUserItem()) continue
            if (item.isFolder) {
                if (depth >= MAX_DEPTH || !budget.takeFolder()) {
                    budget.overflowed = true
                    continue
                }
                val nested = readChildren(childId, depth + 1, budget)
                folders += BookmarkTransferDocument.Folder(
                    title = boundedLabel(item.title, DEFAULT_FOLDER_TITLE, MAX_FOLDER_TITLE_CHARS),
                    bookmarks = nested.bookmarks,
                    folders = nested.folders,
                )
            } else {
                val address = exactWebAddress(item.url) ?: continue
                bookmarks += BookmarkTransferDocument.Entry(
                    title = boundedLabel(item.title, item.url.host, MAX_BOOKMARK_TITLE_CHARS),
                    address = address,
                )
            }
        }
        return FolderContent(bookmarks, folders)
    }

    private fun readExistingAddresses(roots: List<BookmarkId>): MutableSet<String>? {
        val addresses = mutableSetOf<String>()
        val pending = ArrayDeque<Pair<BookmarkId, Int>>()
        roots.forEach { pending.add(it to 0) }
        var seen = 0
        while (pending.isNotEmpty()) {
            val (folder, depth) = pending.removeLast()
            if (depth > MAX_DEPTH) return null
            for (childId in model.getChildIds(folder)) {
                if (++seen > MAX_NODES) return null
                val item = model.getBookmarkById(childId) ?: continue
                if (!item.isPortableUserItem()) continue
                if (item.isFolder) {
                    pending.add(childId to depth + 1)
                } else {
                    exactWebAddress(item.url)?.let(addresses::add)
                }
            }
        }
        return addresses
    }

    private fun profileRoots(fallback: BookmarkId): List<BookmarkId> =
        model.topLevelFolderIds.ifEmpty { listOf(fallback) }

    private fun importEntry(
        parent: BookmarkId,
        entry: BookmarkTransferDocument.Entry,
        existing: MutableSet<String>,
        counts: ImportCounts,
    ) {
        if (counts.seenEntries++ >= MAX_ENTRIES) {
            counts.rejected++
            return
        }
        val url = GURL(entry.address)
        val address = exactWebAddress(url)
        if (address == null) {
            counts.rejected++
            return
        }
        if (address in existing) {
            counts.duplicates++
            return
        }
        val title = boundedLabel(entry.title, url.host, MAX_BOOKMARK_TITLE_CHARS)
        val added = model.addBookmark(parent, model.getChildCount(parent), title, url)
        if (added == null) {
            counts.rejected++
        } else {
            existing += address
            counts.imported++
        }
    }

    private fun findOrCreateFolder(parent: BookmarkId, title: String): BookmarkId? {
        val label = boundedLabel(title, DEFAULT_FOLDER_TITLE, MAX_FOLDER_TITLE_CHARS)
        model.getChildIds(parent).forEach { childId ->
            val item = model.getBookmarkById(childId) ?: return@forEach
            if (item.isPortableUserItem() && item.isFolder && item.title == label) return childId
        }
        return model.addFolder(parent, model.getChildCount(parent), label)
    }

    private fun exactWebAddress(url: GURL): String? = url.validSpecOrEmpty.takeIf {
        url.isValid &&
            (url.scheme == "http" || url.scheme == "https") &&
            url.host.isNotBlank() &&
            url.username.isEmpty() &&
            url.password.isEmpty()
    }

    private fun BookmarkItem.isPortableUserItem(): Boolean =
        id.type == BookmarkType.NORMAL && isEditable

    private data class FolderContent(
        val bookmarks: List<BookmarkTransferDocument.Entry>,
        val folders: List<BookmarkTransferDocument.Folder>,
    )

    private data class PendingFolder(
        val parent: BookmarkId,
        val folder: BookmarkTransferDocument.Folder,
        val depth: Int,
    )

    private class ReadBudget {
        var nodes = 0
        var folders = 0
        var overflowed = false

        fun takeNode(): Boolean = (++nodes <= MAX_NODES).also { if (!it) overflowed = true }

        fun takeFolder(): Boolean = (++folders <= MAX_FOLDERS).also { if (!it) overflowed = true }
    }

    private class ImportCounts(var rejected: Int) {
        var imported = 0
        var duplicates = 0
        var seenEntries = 0
        var seenFolders = 0

        fun rejectTree(folder: BookmarkTransferDocument.Folder) {
            rejected = (rejected.toLong() + countEntries(folder)).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
        }

        fun result() = BookmarkTransferDocument.ImportResult(imported, duplicates, rejected)

        private fun countEntries(root: BookmarkTransferDocument.Folder): Long {
            var count = 0L
            val pending = ArrayDeque<BookmarkTransferDocument.Folder>()
            pending.add(root)
            while (pending.isNotEmpty() && count < Int.MAX_VALUE) {
                val folder = pending.removeLast()
                count += folder.bookmarks.size
                folder.folders.forEach(pending::add)
            }
            return count
        }
    }

    private companion object {
        const val MAX_NODES = 5_512
        const val MAX_ENTRIES = 5_000
        const val MAX_FOLDERS = 512
        const val MAX_DEPTH = 32
        const val MAX_BOOKMARK_TITLE_CHARS = 512
        const val MAX_FOLDER_TITLE_CHARS = 256
        const val DEFAULT_FOLDER_TITLE = "Imported bookmarks"

        fun boundedLabel(value: String, fallback: String, limit: Int): String =
            value.trim().ifEmpty { fallback }.take(limit)
    }
}

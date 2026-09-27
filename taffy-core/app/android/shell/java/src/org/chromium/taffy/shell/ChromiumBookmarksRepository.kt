// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.os.Handler
import android.os.Looper
import com.taffygo.browser.ui.feature.browsing.Bookmark
import com.taffygo.browser.ui.feature.browsing.BookmarkFolder
import com.taffygo.browser.ui.feature.browsing.BookmarkTransferDocument
import com.taffygo.browser.ui.feature.browsing.BookmarksRepository
import com.taffygo.browser.ui.feature.browsing.BookmarksSnapshot
import com.taffygo.browser.ui.feature.browsing.BookmarksWriter
import java.io.Closeable
import java.util.ArrayDeque
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.bookmarks.BookmarkModel
import org.chromium.chrome.browser.bookmarks.BookmarkModelObserver
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.components.bookmarks.BookmarkId
import org.chromium.components.bookmarks.BookmarkItem
import org.chromium.components.bookmarks.BookmarkType
import org.chromium.taffy.host.runAllTeardownOperations
import org.chromium.url.GURL

/** Window projection and writer over Chromium's profile-owned bookmark model. */
class ChromiumBookmarksRepository(
    profile: Profile,
    private val selector: TabModelSelector,
    private val browser: ChromiumBrowserMediator,
    handler: Handler = Handler(Looper.getMainLooper()),
) : BookmarkModelObserver(), BookmarksRepository, BookmarksWriter, Destroyable, Closeable {

    private val model = BookmarkModel.getForProfile(profile)
    private val transfer = ChromiumBookmarkTransfer(model)
    private val state = MutableStateFlow<BookmarksSnapshot>(BookmarksSnapshot.Loading)
    private var liveBookmarks = emptyMap<Bookmark.Id, BookmarkId>()
    private var liveFolders = emptyMap<BookmarkFolder.Id, BookmarkId>()
    private var destroyed = false
    private val projectionRefresh = CoalescedRefresh(
        schedule = handler::post,
        cancel = handler::removeCallbacks,
        refresh = ::refreshNow,
    )

    override val snapshot: StateFlow<BookmarksSnapshot> = state.asStateFlow()

    override val isAvailable: Boolean
        get() = !destroyed && model.isBookmarkModelLoaded && model.defaultBookmarkFolder != null

    init {
        model.addObserver(this)
        if (model.isBookmarkModelLoaded) refreshNow()
    }

    override suspend fun open(id: Bookmark.Id): Boolean = onUiThread {
        val nativeId = liveBookmarks[id] ?: return@onUiThread false
        val item = model.getBookmarkById(nativeId) ?: return@onUiThread false
        val address = exactAddress(item.url) ?: return@onUiThread false
        browser.taskTabs.openStoredPage(address)
    }

    override suspend fun add(
        title: String,
        host: String,
        folderId: BookmarkFolder.Id,
    ): Bookmark.Id = onUiThread { addCurrentPage(title, folderId, expectedHost = host) }

    override suspend fun save(title: String, address: String, folderId: String): Boolean =
        onUiThread {
            val requested = GURL(address)
            if (exactAddress(requested) != address) return@onUiThread false
            addCurrentPage(
                title,
                BookmarkFolder.Id(folderId.ifBlank { BookmarkFolder.Id.ALL.value }),
                expectedAddress = address,
            ).value.isNotBlank()
        }

    private fun addCurrentPage(
        title: String,
        folderId: BookmarkFolder.Id,
        expectedHost: String? = null,
        expectedAddress: String? = null,
    ): Bookmark.Id {
        if (!isAvailable) return EMPTY_BOOKMARK_ID
        val tab = selector.currentTab ?: return EMPTY_BOOKMARK_ID
        if (tab.isDestroyed || tab.isOffTheRecord) return EMPTY_BOOKMARK_ID
        val page = tab.webContents?.lastCommittedUrl ?: tab.url
        val address = exactAddress(page) ?: return EMPTY_BOOKMARK_ID
        if (expectedAddress != null && address != expectedAddress) return EMPTY_BOOKMARK_ID
        if (expectedHost != null && (expectedHost.isBlank() || page.host != expectedHost)) {
            return EMPTY_BOOKMARK_ID
        }
        val parent = targetFolder(folderId) ?: return EMPTY_BOOKMARK_ID
        val label = title.trim().ifEmpty { page.host }
        val added = model.addBookmark(parent, model.getChildCount(parent), label, page)
            ?: return EMPTY_BOOKMARK_ID
        projectionRefresh.request()
        return bookmarkId(added)
    }

    override suspend fun edit(
        id: Bookmark.Id,
        title: String,
        folderId: BookmarkFolder.Id,
    ) = onUiThread {
        val nativeId = liveBookmarks[id] ?: return@onUiThread
        val item = model.getBookmarkById(nativeId) ?: return@onUiThread
        if (!item.isEditable) return@onUiThread
        val parent = targetFolder(folderId) ?: return@onUiThread
        val label = title.trim().ifEmpty { item.title }
        model.setBookmarkTitle(nativeId, label)
        if (item.parentId != parent) model.moveBookmark(nativeId, parent, model.getChildCount(parent))
        projectionRefresh.request()
    }

    override suspend fun delete(id: Bookmark.Id) = onUiThread {
        val nativeId = liveBookmarks[id] ?: return@onUiThread
        val item = model.getBookmarkById(nativeId) ?: return@onUiThread
        if (!item.isEditable) return@onUiThread
        model.deleteBookmarks(nativeId)
        projectionRefresh.request()
    }

    override suspend fun exportDocument(): BookmarkTransferDocument? = onUiThread {
        if (destroyed) null else transfer.exportDocument()
    }

    override suspend fun importDocument(
        document: BookmarkTransferDocument,
    ): BookmarkTransferDocument.ImportResult? = onUiThread {
        if (destroyed) return@onUiThread null
        transfer.importDocument(document)?.also { result ->
            if (result.changed) projectionRefresh.request()
        }
    }

    override fun bookmarkModelChanged() {
        if (!destroyed && model.isBookmarkModelLoaded) projectionRefresh.request()
    }

    override fun destroy() {
        if (destroyed) return
        destroyed = true
        try {
            runAllTeardownOperations(
                listOf(
                    projectionRefresh::close,
                    { model.removeObserver(this) },
                ),
            )
        } finally {
            liveBookmarks = emptyMap()
            liveFolders = emptyMap()
        }
    }

    override fun close() = destroy()

    private fun refreshNow() {
        if (destroyed || !model.isBookmarkModelLoaded) return
        val bookmarkBindings = linkedMapOf<Bookmark.Id, BookmarkId>()
        val folderBindings = linkedMapOf<BookmarkFolder.Id, BookmarkId>()
        val buckets = linkedMapOf<BookmarkFolder.Id, FolderBucket>()
        buckets[BookmarkFolder.Id.ALL] = FolderBucket(name = "")
        model.defaultBookmarkFolder?.let { folderBindings[BookmarkFolder.Id.ALL] = it }

        val pending = ArrayDeque<PendingFolder>()
        model.topLevelFolderIds.forEach { pending.add(PendingFolder(it, BookmarkFolder.Id.ALL, 0)) }
        val visited = mutableSetOf<String>()
        var seen = 0
        while (pending.isNotEmpty() && seen < MAX_NODES) {
            val folder = pending.removeFirst()
            if (folder.depth > MAX_DEPTH || !visited.add(folder.nativeId.toString())) continue
            for (childId in model.getChildIds(folder.nativeId)) {
                if (seen >= MAX_NODES) break
                seen += 1
                val item = model.getBookmarkById(childId) ?: continue
                if (item.isFolder) {
                    val projected = projectedFolder(item, folder.projectedId, buckets, folderBindings)
                    pending.add(PendingFolder(childId, projected, folder.depth + 1))
                    continue
                }
                val bookmark = projectedBookmark(item, folder.projectedId) ?: continue
                if (bookmarkBindings.putIfAbsent(bookmark.id, childId) != null) continue
                buckets.getOrPut(folder.projectedId) { FolderBucket(name = "") }.bookmarks += bookmark
            }
        }

        liveBookmarks = bookmarkBindings
        liveFolders = folderBindings
        state.value = BookmarksSnapshot.Ready(
            buckets.map { (id, bucket) -> BookmarkFolder(id, bucket.name, bucket.bookmarks) },
            complete = seen < MAX_NODES && pending.isEmpty(),
        )
    }

    private fun projectedFolder(
        item: BookmarkItem,
        parent: BookmarkFolder.Id,
        buckets: MutableMap<BookmarkFolder.Id, FolderBucket>,
        bindings: MutableMap<BookmarkFolder.Id, BookmarkId>,
    ): BookmarkFolder.Id {
        if (item.id.type != BookmarkType.NORMAL || !item.isEditable) return parent
        val id = folderId(item.id)
        bindings[id] = item.id
        buckets.putIfAbsent(id, FolderBucket(item.title))
        return id
    }

    private fun projectedBookmark(item: BookmarkItem, folderId: BookmarkFolder.Id): Bookmark? {
        if (item.id.type != BookmarkType.NORMAL || !item.isEditable) return null
        exactAddress(item.url) ?: return null
        val host = item.url.host.takeIf(String::isNotBlank) ?: return null
        return Bookmark(
            id = bookmarkId(item.id),
            title = item.title,
            host = host,
            folderId = folderId,
            address = item.url.validSpecOrEmpty,
        )
    }

    private fun targetFolder(id: BookmarkFolder.Id): BookmarkId? {
        val nativeId =
            (if (id == BookmarkFolder.Id.ALL) model.defaultBookmarkFolder else liveFolders[id])
                ?: return null
        val item = model.getBookmarkById(nativeId) ?: return null
        return nativeId.takeIf { it.type == BookmarkType.NORMAL && item.isFolder }
    }

    private fun exactAddress(url: GURL): String? = url.spec.takeIf {
        url.isValid &&
            (url.scheme == "http" || url.scheme == "https") &&
            url.username.isEmpty() &&
            url.password.isEmpty()
    }

    private suspend fun <T> onUiThread(block: suspend () -> T): T =
        withContext(Dispatchers.Main.immediate) { block() }

    private data class PendingFolder(
        val nativeId: BookmarkId,
        val projectedId: BookmarkFolder.Id,
        val depth: Int,
    )

    private data class FolderBucket(
        val name: String,
        val bookmarks: MutableList<Bookmark> = mutableListOf(),
    )

    private companion object {
        val EMPTY_BOOKMARK_ID = Bookmark.Id("")
        const val MAX_NODES = 2_000
        const val MAX_DEPTH = 32

        fun bookmarkId(id: BookmarkId): Bookmark.Id = Bookmark.Id("chromium-bookmark-${id}")
        fun folderId(id: BookmarkId): BookmarkFolder.Id = BookmarkFolder.Id("chromium-folder-${id}")
    }
}

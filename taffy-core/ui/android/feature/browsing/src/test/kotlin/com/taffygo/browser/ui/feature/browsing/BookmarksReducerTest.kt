// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-202's projection. Empty is not unavailable, a search miss is not
 * empty, and a folder filter never hides a typed name.
 */
class BookmarksReducerTest {

    @Test
    fun `ready with no stars is honest empty`() {
        val snapshot = BookmarksSnapshot.Ready(
            listOf(BookmarkFolder(BookmarkFolder.Id.ALL, name = "")),
        )
        val state = projectBookmarks(snapshot, "")

        assertTrue(state.isEmpty)
        assertFalse(state.hasNoMatches)
        assertFalse(state.isUnavailable)
        assertEquals(0, state.totalCount)
        assertTrue(state.canOpen)
    }

    @Test
    fun `unavailable is not empty and cannot open a page`() {
        val state = projectBookmarks(BookmarksSnapshot.Unavailable, "")

        assertTrue(state.isUnavailable)
        assertFalse(state.isEmpty)
        assertFalse(state.canOpen)
    }

    @Test
    fun `loading is a named wait`() {
        val state = projectBookmarks(BookmarksSnapshot.Loading, "")

        assertTrue(state.isLoading)
        assertFalse(state.isEmpty)
    }

    @Test
    fun `a search that matches nothing is not empty`() {
        val snapshot = BookmarksSnapshot.Ready(listOf(folder(bookmark("bm_1", "Retention policy"))))
        val nothingAtAll = projectBookmarks(
            BookmarksSnapshot.Ready(listOf(BookmarkFolder(BookmarkFolder.Id.ALL, name = ""))),
            "",
        )
        val nothingMatching = projectBookmarks(snapshot, "no such page")

        assertTrue(nothingAtAll.isEmpty)
        assertFalse(nothingMatching.isEmpty)
        assertTrue(nothingMatching.hasNoMatches)
        assertEquals(1, nothingMatching.totalCount)
        assertEquals(0, nothingMatching.visibleCount)
    }

    @Test
    fun `the query matches title or host across folders`() {
        val news = BookmarkFolder.Id("news")
        val snapshot = BookmarksSnapshot.Ready(
            listOf(
                folder(bookmark("bm_1", "Retention policy", host = "docs.example.test")),
                BookmarkFolder(
                    news,
                    name = "News",
                    bookmarks = listOf(bookmark("bm_2", "Product listing", "shop.example.test", news)),
                ),
            ),
        )

        val byHost = projectBookmarks(snapshot, "SHOP.", currentFolderId = BookmarkFolder.Id.ALL)
        assertEquals(listOf("bm_2"), byHost.folders.flatMap { it.bookmarks }.map { it.id.value })

        val byTitle = projectBookmarks(snapshot, "retention")
        assertEquals(listOf("bm_1"), byTitle.folders.flatMap { it.bookmarks }.map { it.id.value })
    }

    @Test
    fun `an empty query in a folder shows only that folder`() {
        val news = BookmarkFolder.Id("news")
        val snapshot = BookmarksSnapshot.Ready(
            listOf(
                folder(bookmark("bm_1", "Retention policy")),
                BookmarkFolder(
                    news,
                    name = "News",
                    bookmarks = listOf(bookmark("bm_2", "Headline", "news.example.test", news)),
                ),
            ),
        )

        val state = projectBookmarks(snapshot, "", currentFolderId = news)

        assertEquals(listOf("bm_2"), state.folders.flatMap { it.bookmarks }.map { it.id.value })
        assertEquals(news, state.currentFolderId)
        assertEquals(2, state.totalCount)
        assertEquals(1, state.visibleCount)
    }

    @Test
    fun `editing tracks the live bookmark, or clears if it was deleted`() {
        val bookmark = bookmark("bm_1", "Retention policy")
        val snapshot = BookmarksSnapshot.Ready(listOf(folder(bookmark)))

        val present = projectBookmarks(snapshot, "", editing = bookmark)
        assertEquals(bookmark.id, present.editing?.id)

        val gone = projectBookmarks(
            BookmarksSnapshot.Ready(listOf(BookmarkFolder(BookmarkFolder.Id.ALL, name = ""))),
            "",
            editing = bookmark,
        )
        assertNull(gone.editing)
    }

    private fun bookmark(
        id: String,
        title: String,
        host: String = "docs.example.test",
        folderId: BookmarkFolder.Id = BookmarkFolder.Id.ALL,
    ) = Bookmark(Bookmark.Id(id), title, host, folderId)

    private fun folder(vararg bookmarks: Bookmark) = BookmarkFolder(
        id = BookmarkFolder.Id.ALL,
        name = "",
        bookmarks = bookmarks.toList(),
    )
}

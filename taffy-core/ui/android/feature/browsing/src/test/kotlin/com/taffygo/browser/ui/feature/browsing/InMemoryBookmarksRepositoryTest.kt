// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The in-memory store Save page will write, empty until something is starred. */
class InMemoryBookmarksRepositoryTest {

    @Test
    fun `starts with the All folder and no stars`() = runTest {
        val repository = InMemoryBookmarksRepository()
        val ready = repository.snapshot.value as BookmarksSnapshot.Ready

        assertEquals(listOf(BookmarkFolder.Id.ALL), ready.folders.map { it.id })
        assertTrue(ready.folders.single().bookmarks.isEmpty())
    }

    @Test
    fun `add then edit then delete is the list the screen would show`() = runTest {
        val repository = InMemoryBookmarksRepository()
        val id = repository.add("Old title", "docs.example.test")

        assertEquals(
            "Old title",
            (repository.snapshot.value as BookmarksSnapshot.Ready)
                .folders.single().bookmarks.single().title,
        )

        repository.edit(id, "Retention policy", BookmarkFolder.Id.ALL)
        assertEquals(
            "Retention policy",
            (repository.snapshot.value as BookmarksSnapshot.Ready)
                .folders.single().bookmarks.single().title,
        )

        repository.delete(id)
        assertTrue(
            (repository.snapshot.value as BookmarksSnapshot.Ready)
                .folders.single().bookmarks.isEmpty(),
        )
    }

    @Test
    fun `portable merge preserves folder names and skips exact duplicates`() = runTest {
        val repository = InMemoryBookmarksRepository()
        repository.add("Existing", "docs.example.test")

        val result = repository.importDocument(
            BookmarkTransferDocument(
                bookmarks = listOf(
                    BookmarkTransferDocument.Entry("Again", "https://docs.example.test/"),
                ),
                folders = listOf(
                    BookmarkTransferDocument.Folder(
                        "Reading",
                        bookmarks = listOf(
                            BookmarkTransferDocument.Entry("New", "https://news.example.test/a"),
                        ),
                    ),
                ),
            ),
        )

        assertEquals(
            BookmarkTransferDocument.ImportResult(imported = 1, duplicates = 1, rejected = 0),
            result,
        )
        val exported = requireNotNull(repository.exportDocument())
        assertEquals(2, exported.entryCount)
        assertEquals("Reading", exported.folders.single().title)
        assertEquals("https://news.example.test/a", exported.folders.single().bookmarks.single().address)
    }
}

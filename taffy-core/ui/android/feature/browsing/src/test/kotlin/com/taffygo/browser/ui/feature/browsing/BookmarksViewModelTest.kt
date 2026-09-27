// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Screen SCR-202's commands: open, edit, delete, folders, and honest empty. */
@OptIn(ExperimentalCoroutinesApi::class)
class BookmarksViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `an in-memory store starts empty, not with invented stars`() = runTest(dispatcher) {
        val viewModel = viewModel()

        assertTrue(viewModel.state.value.isEmpty)
        assertEquals(0, viewModel.state.value.totalCount)
        assertTrue(viewModel.state.value.canOpen)
    }

    @Test
    fun `unavailable bookmarks do not open a tab`() = runTest(dispatcher) {
        val browser = PagesTestBrowser()
        val navigator = PagesTestNavigator()
        val viewModel = viewModel(
            bookmarks = UnavailableBookmarksRepository(),
            browser = browser,
        )

        viewModel.onIntent(BookmarksIntent.Open(Bookmark.Id("bm_1")), navigator)
        runCurrent()

        assertTrue(viewModel.state.value.isUnavailable)
        assertTrue(browser.opened.isEmpty())
        assertTrue(navigator.visited.isEmpty())
    }

    @Test
    fun `open delegates the opaque bookmark and leaves Bookmarks behind`() = runTest(dispatcher) {
        val browser = PagesTestBrowser()
        val navigator = PagesTestNavigator()
        val bookmarks = InMemoryBookmarksRepository()
        bookmarks.add("Retention policy", "docs.example.test")
        val id = (bookmarks.snapshot.value as BookmarksSnapshot.Ready)
            .folders.single().bookmarks.single().id
        val viewModel = viewModel(bookmarks = bookmarks, browser = browser)

        viewModel.onIntent(BookmarksIntent.Open(id), navigator)
        runCurrent()

        assertTrue(browser.opened.isEmpty())
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
    }

    @Test
    fun `delete removes the star the person confirmed`() = runTest(dispatcher) {
        val bookmarks = InMemoryBookmarksRepository()
        val id = bookmarks.add("Retention policy", "docs.example.test")
        val viewModel = viewModel(bookmarks = bookmarks)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BookmarksIntent.Delete(id), PagesTestNavigator())
        runCurrent()

        assertTrue(viewModel.state.value.isEmpty)
    }

    @Test
    fun `save edit renames the bookmark`() = runTest(dispatcher) {
        val bookmarks = InMemoryBookmarksRepository()
        val id = bookmarks.add("Old title", "docs.example.test")
        val viewModel = viewModel(bookmarks = bookmarks)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BookmarksIntent.Edit(id), PagesTestNavigator())
        viewModel.onIntent(
            BookmarksIntent.SaveEdit("Retention policy", BookmarkFolder.Id.ALL),
            PagesTestNavigator(),
        )
        runCurrent()

        val ready = bookmarks.snapshot.value as BookmarksSnapshot.Ready
        assertEquals("Retention policy", ready.folders.single().bookmarks.single().title)
        assertNull(viewModel.state.value.editing)
    }

    @Test
    fun `import reports merged duplicate and refused addresses honestly`() = runTest(dispatcher) {
        val bookmarks = InMemoryBookmarksRepository()
        bookmarks.add("Already saved", "docs.example.test")
        val viewModel = viewModel(bookmarks = bookmarks)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(BookmarksIntent.RequestImport, PagesTestNavigator())
        assertTrue(viewModel.importDestinationSelected())
        viewModel.importDocument(
            BookmarkTransferDocument(
                bookmarks = listOf(
                    BookmarkTransferDocument.Entry("Duplicate", "https://docs.example.test/"),
                    BookmarkTransferDocument.Entry("New", "https://new.example.test/path"),
                    BookmarkTransferDocument.Entry("Unsafe", "javascript:alert(1)"),
                ),
            ),
        )
        runCurrent()

        assertEquals(BookmarksUiState.TransferStatus.IMPORTED, viewModel.state.value.transferStatus)
        assertEquals(
            BookmarkTransferDocument.ImportResult(imported = 1, duplicates = 1, rejected = 1),
            viewModel.state.value.importResult,
        )
        assertEquals(2, viewModel.state.value.totalCount)
    }

    @Test
    fun `export writes the exact live tree and publishes success`() = runTest(dispatcher) {
        val bookmarks = InMemoryBookmarksRepository()
        bookmarks.add("Retention policy", "docs.example.test")
        val viewModel = viewModel(bookmarks = bookmarks)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        var written: BookmarkTransferDocument? = null

        viewModel.onIntent(BookmarksIntent.RequestExport, PagesTestNavigator())
        viewModel.writeExport { document ->
            written = document
            true
        }
        runCurrent()

        assertEquals("https://docs.example.test/", written?.bookmarks?.single()?.address)
        assertEquals(BookmarksUiState.TransferStatus.EXPORTED, viewModel.state.value.transferStatus)
    }

    @Test
    fun `stale picker results cannot start a transfer`() = runTest(dispatcher) {
        val viewModel = viewModel()

        assertFalse(viewModel.importDestinationSelected())
        viewModel.importDocument(BookmarkTransferDocument())
        viewModel.writeExport { true }
        runCurrent()

        assertEquals(BookmarksUiState.TransferStatus.IDLE, viewModel.state.value.transferStatus)
    }

    @Test
    fun `dismiss from a folder returns to the list, not the previous screen`() =
        runTest(dispatcher) {
            val navigator = PagesTestNavigator()
            val bookmarks = InMemoryBookmarksRepository()
            bookmarks.add("Headline", "news.example.test", BookmarkFolder.Id("news"))
            val viewModel = viewModel(bookmarks = bookmarks)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BookmarksIntent.OpenFolder(BookmarkFolder.Id("news")), navigator)
            runCurrent()
            assertEquals(BookmarkFolder.Id("news"), viewModel.state.value.currentFolderId)

            viewModel.onIntent(BookmarksIntent.Dismiss, navigator)
            runCurrent()

            assertNull(viewModel.state.value.currentFolderId)
            assertEquals(0, navigator.backs)
        }

    @Test
    fun `dismiss at the root leaves the screen`() = runTest(dispatcher) {
        val navigator = PagesTestNavigator()
        val viewModel = viewModel()

        viewModel.onIntent(BookmarksIntent.Dismiss, navigator)

        assertEquals(1, navigator.backs)
    }

    @Test
    fun `onShown records the catalog identifier`() = runTest(dispatcher) {
        val analytics = PagesTestAnalytics()
        val viewModel = viewModel(analytics = analytics)

        viewModel.onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.Bookmarks.screenId)),
            analytics.events,
        )
    }

    private fun viewModel(
        bookmarks: BookmarksRepository = InMemoryBookmarksRepository(),
        browser: PagesTestBrowser = PagesTestBrowser(),
        analytics: PagesTestAnalytics = PagesTestAnalytics(),
    ) = BookmarksViewModel(
        bookmarks = bookmarks,
        browser = browser,
        analytics = analytics,
        savedState = SavedStateHandle(),
    )
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.content.Intent
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-202 — Bookmarks.
 *
 * Empty teaches Bookmarks versus Library versus Memory. Delete, import, and
 * export are labelled controls rather than gestures alone.
 */
class BookmarksSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<BookmarksIntent>()

    @Test
    fun theScreenAndSearchExist() {
        show(PreviewStates.bookmarks)

        compose.onNodeWithTag(TaffyDestination.Bookmarks.screenId).assertExists()
        compose.onNodeWithTag(BOOKMARKS_SEARCH_TEST_TAG).assertExists()
        compose.onNodeWithTag(BOOKMARKS_IMPORT_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(BOOKMARKS_EXPORT_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag("${BOOKMARK_TEST_TAG_PREFIX}bm_1").assertExists()
        compose.onNodeWithTag("${BOOKMARK_EDIT_ROW_TEST_TAG_PREFIX}bm_1")
            .assertExists()
            .assertHasClickAction()
        compose.onNodeWithTag("${BOOKMARK_DELETE_TEST_TAG_PREFIX}bm_1")
            .assertExists()
            .assertHasClickAction()
    }

    @Test
    fun deleteIsAButtonAndConfirmSendsTheId() {
        show(PreviewStates.bookmarks)

        compose.onNodeWithTag("${BOOKMARK_DELETE_TEST_TAG_PREFIX}bm_1").performClick()
        compose.onNodeWithTag(BOOKMARKS_CONFIRM_TEST_TAG).assertExists()
        compose.onNodeWithTag("${BOOKMARKS_CONFIRM_TEST_TAG}_confirm").performClick()

        assertEquals(listOf(BookmarksIntent.Delete(Bookmark.Id("bm_1"))), intents)
    }

    @Test
    fun editOpensTheEditor() {
        show(PreviewStates.bookmarks)

        compose.onNodeWithTag("${BOOKMARK_EDIT_ROW_TEST_TAG_PREFIX}bm_1").performClick()

        assertEquals(listOf(BookmarksIntent.Edit(Bookmark.Id("bm_1"))), intents)
    }

    @Test
    fun openingABookmarkSendsThatIdentifier() {
        show(PreviewStates.bookmarks)

        compose.onNodeWithTag("${BOOKMARK_TEST_TAG_PREFIX}bm_1").performClick()

        assertEquals(listOf(BookmarksIntent.Open(Bookmark.Id("bm_1"))), intents)
    }

    @Test
    fun emptyTeachesBookmarksAreNotLibraryOrMemory() {
        show(BookmarksUiState())

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_pages_bookmarks_empty_title))
            .assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_pages_bookmarks_empty_body))
            .assertExists()
    }

    @Test
    fun unavailableDoesNotInventStars() {
        show(PreviewStates.bookmarksUnavailable)

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag("${BOOKMARK_TEST_TAG_PREFIX}bm_1").assertDoesNotExist()
    }

    @Test
    fun importAndExportAreExplicitAndThereIsNoReadingList() {
        show(PreviewStates.bookmarks)

        compose.onNodeWithTag(BOOKMARKS_IMPORT_TEST_TAG).performClick()
        compose.onNodeWithTag(BOOKMARKS_EXPORT_TEST_TAG).performClick()

        assertEquals(
            listOf(BookmarksIntent.RequestImport, BookmarksIntent.RequestExport),
            intents,
        )
        compose.onNodeWithText("Reading list").assertDoesNotExist()
    }

    @Test
    fun transferUsesAndroidsOpenAndCreateDocumentSurfaces() {
        val open = OpenBookmarkDocument().createIntent(context, Unit)
        val create = CreateBookmarkDocument().createIntent(context, Unit)

        assertEquals(Intent.ACTION_OPEN_DOCUMENT, open.action)
        assertEquals(Intent.ACTION_CREATE_DOCUMENT, create.action)
        assertEquals(BOOKMARK_DOCUMENT_MIME_TYPE, open.type)
        assertEquals(BOOKMARK_DOCUMENT_MIME_TYPE, create.type)
        assertEquals(BOOKMARK_DOCUMENT_FILE_NAME, create.getStringExtra(Intent.EXTRA_TITLE))
    }

    private fun show(state: BookmarksUiState) {
        intents.clear()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BookmarksContent(state = state, onIntent = { intents += it })
            }
        }
    }
}

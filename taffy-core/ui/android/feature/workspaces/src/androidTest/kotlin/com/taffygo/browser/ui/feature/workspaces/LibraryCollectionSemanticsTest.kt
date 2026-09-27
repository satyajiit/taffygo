// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-502 — items in one collection.
 *
 * Refresh previews every page and stays inert until the person confirms.
 */
class LibraryCollectionSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<LibraryCollectionIntent>()

    @Test
    fun openingAnItemSendsItsIdentifier() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryCollectionContent(
                    state = LibraryPreviewStates.collection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${LIBRARY_ITEM_TEST_TAG_PREFIX}item_conflict")
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .performClick()

        assertEquals(
            listOf(LibraryCollectionIntent.OpenItem("item_conflict")),
            intents,
        )
    }

    @Test
    fun mutateActionsStayOffWhenThePortCannotChangeAnything() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryCollectionContent(
                    state = LibraryPreviewStates.collection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIBRARY_COLLECTION_REFRESH_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(LIBRARY_COLLECTION_EXPORT_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(LIBRARY_COLLECTION_REMOVE_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_collection_disconnected),
        ).assertExists()
    }

    @Test
    fun refreshShowsExactWorkAndOnlyConfirmationEmitsApproval() {
        val preview = LibraryRepository.RefreshPreview(
            previewId = "a".repeat(64),
            collectionId = "col_home",
            libraryRevision = 7uL,
            workspaceRevision = 3uL,
            navigationCount = 2u,
            observationCount = 2u,
            sources = listOf(
                LibraryRepository.RefreshSource("source-1", "Listing", "shop.example.test"),
                LibraryRepository.RefreshSource("source-2", "Manual", "docs.example.test"),
            ),
        )
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryCollectionContent(
                    state = LibraryPreviewStates.collection.copy(
                        canMutate = true,
                        refreshPreview = preview,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIBRARY_COLLECTION_REFRESH_TEST_TAG)
            .assertIsEnabled()
            .performClick()
        compose.onNodeWithTag(LIBRARY_REFRESH_DIALOG_TEST_TAG).assertExists()
        compose.onNodeWithText("Listing — shop.example.test").assertExists()
        compose.onNodeWithText("Manual — docs.example.test").assertExists()
        assertEquals(emptyList<LibraryCollectionIntent>(), intents)

        compose.onNodeWithTag(LIBRARY_REFRESH_CONFIRM_TEST_TAG).performClick()

        assertEquals(listOf(LibraryCollectionIntent.ApproveRefresh(preview)), intents)
    }

    @Test
    fun anEmptyCollectionTeachesKeepThisNotBookmarks() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryCollectionContent(
                    state = LibraryPreviewStates.collectionEmpty,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_collection_empty_body),
        ).assertExists()
        compose.onNodeWithTag(LIBRARY_COLLECTION_LIST_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLargeCollectionComposesOnlyItemsInTheViewport() {
        val items = (0 until 1_024).map { index ->
            LibraryRepository.Item(
                id = "item_$index",
                collectionId = "large_collection",
                title = "Saved item $index",
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryCollectionContent(
                    state = LibraryCollectionUiState(
                        collectionId = "large_collection",
                        name = "Large collection",
                        items = items,
                        totalCount = items.size,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${LIBRARY_ITEM_TEST_TAG_PREFIX}item_1023").assertDoesNotExist()
        // Search field, then one individually keyed Library item per row.
        compose.onNodeWithTag(LIBRARY_COLLECTION_LIST_TEST_TAG)
            .performScrollToIndex(items.size)
        compose.onNodeWithTag("${LIBRARY_ITEM_TEST_TAG_PREFIX}item_1023").assertExists()
        compose.onNodeWithTag("${LIBRARY_ITEM_TEST_TAG_PREFIX}item_0").assertDoesNotExist()
    }
}

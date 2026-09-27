// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-501 — collections of things the person asked Taffy to keep.
 *
 * Empty teaching names Bookmarks, Library, and Memory so the three piles stay
 * distinct. A conflict is a word and a glyph, not a colour on its own.
 */
class LibraryHomeSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<LibraryHomeIntent>()

    @Test
    fun emptyTeachingNamesBookmarksLibraryAndMemory() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryPreviewStates.homeEmpty,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.LibraryHome.screenId).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_home_empty_title),
        ).assertExists()
        val body = context.getString(R.string.taffy_library_home_empty_body)
        compose.onNodeWithText(body).assertExists()
    }

    @Test
    fun aCollectionRowIsAtLeastFortyEightDpAndOpensItsId() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryPreviewStates.homePopulated,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${LIBRARY_COLLECTION_TEST_TAG_PREFIX}col_home")
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .performClick()

        assertEquals(
            listOf(LibraryHomeIntent.OpenCollection("col_home")),
            intents,
        )
    }

    @Test
    fun aConflictIsNamedInTheRowDescription() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryPreviewStates.homePopulated,
                    onIntent = { intents += it },
                )
            }
        }

        val count = context.resources.getQuantityString(R.plurals.taffy_library_item_count, 2, 2)
        val supporting = context.getString(
            R.string.taffy_library_home_supporting,
            count,
            "Checked 3 weeks ago",
        )
        val conflicts = context.resources.getQuantityString(
            R.plurals.taffy_library_conflict_count,
            1,
            1,
        )
        val expected = context.getString(
            R.string.taffy_library_home_description_conflict,
            "Home project",
            supporting,
            conflicts,
        )
        compose.onNodeWithTag("${LIBRARY_COLLECTION_TEST_TAG_PREFIX}col_home")
            .assertContentDescriptionEquals(expected)
    }

    @Test
    fun typingInTheSearchSendsTheQuery() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryPreviewStates.homePopulated,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIBRARY_HOME_SEARCH_TEST_TAG).performTextReplacement("travel")

        assertEquals(listOf(LibraryHomeIntent.QueryChanged("travel")), intents)
    }

    @Test
    fun theSearchFieldOffersSearchOnTheKeyboard() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryHomeUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIBRARY_HOME_SEARCH_TEST_TAG).assert(
            SemanticsMatcher.expectValue(SemanticsProperties.ImeAction, ImeAction.Search),
        )
    }

    @Test
    fun unavailableIsNotAnEmptyLibrary() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryPreviewStates.homeUnavailable,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(
            context.getString(R.string.taffy_library_unavailable_title),
        ).assertExists()
        compose.onNodeWithTag(LIBRARY_HOME_LIST_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun loadingExposesADescribedSkeleton() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                LibraryHomeContent(
                    state = LibraryPreviewStates.homeLoading,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIBRARY_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(LIBRARY_HOME_LIST_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLargeLibraryComposesOnlyCollectionsInTheViewport() {
        val collections = (0 until 1_000).map { index ->
            LibraryRepository.Collection(
                id = "collection_$index",
                name = "Collection $index",
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryHomeContent(
                    state = LibraryHomeUiState(
                        collections = collections,
                        totalCount = collections.size,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${LIBRARY_COLLECTION_TEST_TAG_PREFIX}collection_999")
            .assertDoesNotExist()
        // Search field, then one individually keyed collection per row.
        compose.onNodeWithTag(LIBRARY_HOME_LIST_TEST_TAG)
            .performScrollToIndex(collections.size)
        compose.onNodeWithTag("${LIBRARY_COLLECTION_TEST_TAG_PREFIX}collection_999")
            .assertExists()
        compose.onNodeWithTag("${LIBRARY_COLLECTION_TEST_TAG_PREFIX}collection_0")
            .assertDoesNotExist()
    }
}

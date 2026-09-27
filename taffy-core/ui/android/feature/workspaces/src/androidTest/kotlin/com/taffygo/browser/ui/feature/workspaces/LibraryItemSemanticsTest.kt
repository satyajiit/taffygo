// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-503 — one kept item.
 *
 * A conflict is a sentence, not a colour. Remove stays off while the port
 * cannot delete anything.
 */
class LibraryItemSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<LibraryItemIntent>()

    @Test
    fun aConflictNamesTheDisagreement() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryItemContent(
                    state = LibraryPreviewStates.item,
                    onIntent = { intents += it },
                )
            }
        }

        val expected = context.getString(
            R.string.taffy_library_item_description_conflict,
            context.getString(R.string.taffy_library_item_conflict),
            "Two years on one page, one year on the other.",
        )
        compose.onNodeWithTag(LIBRARY_ITEM_CONFLICT_TEST_TAG)
            .assertContentDescriptionEquals(expected)
        compose.onNodeWithText("The listing says the warranty is two years.").assertExists()
    }

    @Test
    fun aRelatedItemOpensItsIdentifier() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryItemContent(
                    state = LibraryPreviewStates.item,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${LIBRARY_RELATED_TEST_TAG_PREFIX}item_plain")
            .assertHasClickAction()
            .performClick()

        assertEquals(
            listOf(LibraryItemIntent.OpenRelated("col_home", "item_plain")),
            intents,
        )
    }

    @Test
    fun removeStaysOffWhenThePortCannotChangeAnything() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryItemContent(
                    state = LibraryPreviewStates.item,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LIBRARY_ITEM_REMOVE_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_item_disconnected),
        ).assertExists()
    }

    @Test
    fun aMissingItemIsNotFilledIn() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                LibraryItemContent(
                    state = LibraryItemUiState(missing = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_item_missing_title),
        ).assertExists()
        compose.onNodeWithTag(LIBRARY_ITEM_CONFLICT_TEST_TAG).assertDoesNotExist()
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-504 — Keep this.
 *
 * The Keep control stays off while the port cannot add anything, so the
 * screen never claims Library gained an item.
 */
class KeepThisSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<KeepThisIntent>()

    @Test
    fun keepStaysOffWhenThePortCannotAddAnything() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                KeepThisContent(
                    state = LibraryPreviewStates.keepThis,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.KeepThis.screenId).assertExists()
        compose.onNodeWithTag(KEEP_THIS_ACTION_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_keep_disconnected),
        ).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_library_keep_body),
        ).assertExists()
    }

    @Test
    fun choosingACollectionAndAKindSendsThoseIntents() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                KeepThisContent(
                    state = LibraryPreviewStates.keepThisEmpty.copy(
                        collections = LibraryPreviewStates.keepThis.collections,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${KEEP_THIS_COLLECTION_TEST_TAG_PREFIX}col_home")
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .performClick()
        compose.onNodeWithTag("${KEEP_THIS_KIND_TEST_TAG_PREFIX}page_extract")
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .performClick()

        assertEquals(
            listOf(
                KeepThisIntent.SelectCollection("col_home"),
                KeepThisIntent.SelectKind(KeepThisKind.PAGE_EXTRACT),
            ),
            intents,
        )
    }

    @Test
    fun emptyCollectionsSayThereIsNowhereToLand() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                KeepThisContent(
                    state = LibraryPreviewStates.keepThisEmpty,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(
            context.getString(R.string.taffy_library_keep_no_collections),
        ).assertExists()
        compose.onNodeWithTag(KEEP_THIS_COLLECTIONS_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(KEEP_THIS_KINDS_TEST_TAG).assertExists()
        compose.onNodeWithTag(KEEP_THIS_ACTION_TEST_TAG).assertIsNotEnabled()
    }
}

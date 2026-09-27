// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Screen SCR-413 — metadata list, masked TalkBack, no live-region dump. */
class SavedSignInsSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<SavedSignInsIntent>()

    @Test
    fun unavailableIsHonestAndThePromiseIsOnScreen() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SavedSignInsContent(state = SavedSignInsUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.SavedSignIns.screenId).assertExists()
        compose.onNodeWithTag(SIGN_INS_PROMISE_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(SIGN_INS_LIST_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun detailHasMetadataButNoPasswordOrCopyAction() {
        val record = SavedSignInsRepository.Record(
            id = "s1",
            site = "croma.com",
            username = "you@email.example",
            lastUsedEpochMillis = 1_780_000_000_000L,
        )
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SavedSignInsContent(
                    state = SavedSignInsUiState(
                        availability = YouSurfaceAvailability.READY,
                        records = listOf(record),
                        opened = record,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SIGN_IN_USERNAME_TEST_TAG).assertExists()
        compose.onNodeWithTag("saved_sign_in_password").assertDoesNotExist()
        compose.onNodeWithTag("saved_sign_in_show").assertDoesNotExist()
        compose.onNodeWithTag("saved_sign_in_copy_password").assertDoesNotExist()
        assertEquals(emptyList<SavedSignInsIntent>(), intents)
    }

    @Test
    fun aLargePasswordStoreComposesOnlyVisibleMetadataRows() {
        val records = (0 until 1_000).map { index ->
            SavedSignInsRepository.Record(
                id = "saved_$index",
                site = "site-$index.example.test",
                username = "person-$index@example.test",
                lastUsedEpochMillis = 1_780_000_000_000L - index,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SavedSignInsContent(
                    state = SavedSignInsUiState(
                        availability = YouSurfaceAvailability.READY,
                        records = records,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${SIGN_INS_ROW_TEST_TAG_PREFIX}saved_999").assertDoesNotExist()
        // Promise, search field, then one individually keyed row per record.
        compose.onNodeWithTag(SIGN_INS_LIST_TEST_TAG).performScrollToIndex(records.size + 1)
        compose.onNodeWithTag("${SIGN_INS_ROW_TEST_TAG_PREFIX}saved_999").assertExists()
        compose.onNodeWithTag("${SIGN_INS_ROW_TEST_TAG_PREFIX}saved_0").assertDoesNotExist()
    }
}

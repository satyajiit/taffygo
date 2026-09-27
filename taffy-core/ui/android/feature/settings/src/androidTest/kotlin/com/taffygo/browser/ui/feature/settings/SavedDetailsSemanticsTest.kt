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

/** Screen SCR-414 — promise, empty, editor. */
class SavedDetailsSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<SavedDetailsIntent>()

    @Test
    fun emptyOffersAddAndKeepsThePromise() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SavedDetailsContent(
                    state = SavedDetailsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.SavedDetails.screenId).assertExists()
        compose.onNodeWithTag(DETAILS_PROMISE_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(DETAILS_EMPTY_ADD_TEST_TAG).performClick()
        assertEquals(listOf(SavedDetailsIntent.Add), intents)
    }

    @Test
    fun aLargeSavedDetailsStoreComposesOnlyVisiblePeople() {
        val people = (0 until 1_000).map { index ->
            SavedDetailsRepository.Person(
                id = "person_$index",
                givenName = "Person",
                familyName = "$index",
                email = "person-$index@example.test",
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SavedDetailsContent(
                    state = SavedDetailsUiState(people = people),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${DETAILS_ROW_TEST_TAG_PREFIX}person_999").assertDoesNotExist()
        // Promise, then one individually keyed person per row.
        compose.onNodeWithTag(DETAILS_LIST_TEST_TAG).performScrollToIndex(people.size)
        compose.onNodeWithTag("${DETAILS_ROW_TEST_TAG_PREFIX}person_999").assertExists()
        compose.onNodeWithTag("${DETAILS_ROW_TEST_TAG_PREFIX}person_0").assertDoesNotExist()
    }
}

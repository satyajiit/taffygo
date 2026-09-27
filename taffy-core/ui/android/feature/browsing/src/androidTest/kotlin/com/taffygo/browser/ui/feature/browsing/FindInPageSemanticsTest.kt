// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Find in page (SCR-106): field, count, next, prev, close. */
class FindInPageSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<FindInPageIntent>()

    @Test
    fun anUnavailableEngineShowsZeroOfZeroAndKeepsNextDisabled() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                FindInPageOverlay(
                    state = FindInPageUiState(
                        open = true,
                        query = "policy",
                        available = false,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(FIND_OVERLAY_TEST_TAG).assertExists()
        compose.onNodeWithTag(FIND_FIELD_TEST_TAG).assertExists()
        compose.onNodeWithTag(FIND_COUNT_TEST_TAG).assertExists()
        compose.onNodeWithTag(FIND_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(FIND_NEXT_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(FIND_PREVIOUS_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(FIND_CLOSE_TEST_TAG)
            .assertHeightIsEqualTo(48.dp)
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf<FindInPageIntent>(FindInPageIntent.Close), intents)
    }

    @Test
    fun anEmptyQueryHidesTheCount() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                FindInPageOverlay(
                    state = FindInPageUiState(open = true, available = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(FIND_COUNT_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FIND_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FIND_NEXT_TEST_TAG).assertIsNotEnabled()
    }
}

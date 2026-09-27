// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Save page (SCR-811): honest disabled Save when no writer exists. */
class SavePageSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<SavePageIntent>()

    @Test
    fun anEmptyWriterKeepsSaveDisabledAndSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                SavePageSheet(
                    state = SavePageUiState(
                        title = "Retention policy",
                        host = "docs.example.test",
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SAVE_PAGE_TEST_TAG).assertExists()
        compose.onNodeWithTag(SAVE_PAGE_BODY_TEST_TAG).assertExists()
        compose.onNodeWithTag(SAVE_PAGE_SAVE_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(SAVE_PAGE_CANCEL_TEST_TAG)
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf<SavePageIntent>(SavePageIntent.Dismiss), intents)
    }

    @Test
    fun aPrivateTabRefusesInWords() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                SavePageSheet(
                    state = SavePageUiState(
                        title = "Price history",
                        host = "prices.example.test",
                        isPrivate = true,
                        canSave = true,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SAVE_PAGE_BODY_TEST_TAG).assertExists()
        compose.onNodeWithTag(SAVE_PAGE_SAVE_TEST_TAG).assertIsNotEnabled()
    }
}

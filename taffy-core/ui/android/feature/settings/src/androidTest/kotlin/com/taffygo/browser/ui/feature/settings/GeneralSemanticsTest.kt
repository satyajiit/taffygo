// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class GeneralSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<GeneralIntent>()

    @Test
    fun rowsAreOnScreenAndSearchAndAppearanceOpen() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                GeneralContent(state = GeneralUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.General.screenId).assertExists()
        compose.onNodeWithTag(GENERAL_SEARCH_ENGINE_TEST_TAG).assertExists()
        compose.onNodeWithTag(GENERAL_DOWNLOADS_TEST_TAG).assertExists()
        compose.onNodeWithTag(GENERAL_TEXT_SIZE_TEST_TAG).assertExists()
        compose.onNodeWithTag(GENERAL_SEARCH_ENGINE_TEST_TAG).performClick()
        compose.onNodeWithTag(GENERAL_TEXT_SIZE_TEST_TAG).performClick()
        assertEquals(
            listOf(GeneralIntent.ChooseSearchEngine, GeneralIntent.OpenAppearance),
            intents,
        )
    }

    @Test
    fun downloadLocationFailureHasAnExplicitVisibleState() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                GeneralContent(
                    state = GeneralUiState(
                        downloadLocationFailure = DownloadLocationFailure.WRITE_FAILED,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(GENERAL_DOWNLOAD_LOCATION_FAILURE_TEST_TAG).assertExists()
    }
}

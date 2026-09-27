// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Test

class ClearBrowsingDataSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun unavailableKeepsRangesAndDisablesConfirm() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ClearBrowsingDataContent(
                    state = ClearBrowsingDataUiState(),
                    onIntent = {},
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.ClearBrowsingData.screenId).assertExists()
        compose.onNodeWithTag(CLEAR_RANGE_LIST_TEST_TAG).assertExists()
        compose.onNodeWithTag(CLEAR_WORKSPACES_TEST_TAG).assertExists()
        compose.onNodeWithTag(CLEAR_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(CLEAR_CONFIRM_TEST_TAG).assertIsNotEnabled()
    }

    @Test
    fun partialFailureIsVisibleWithoutClaimingCompletion() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                ClearBrowsingDataContent(
                    state = ClearBrowsingDataUiState(
                        available = true,
                        supportedClasses = setOf(ClearBrowsingDataUiState.DataClass.HISTORY),
                        failed = true,
                    ),
                    onIntent = {},
                )
            }
        }

        compose.onNodeWithTag(CLEAR_FAILED_TEST_TAG).assertExists()
        compose.onNodeWithTag(CLEAR_UNAVAILABLE_TEST_TAG).assertDoesNotExist()
    }
}

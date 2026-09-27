// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * The start page is not drawn until the Python library is installed.
 *
 * Both frames — an empty tab on SCR-101 and the chooser on SCR-102 — show the
 * same preparing state, and neither offers the address box while they wait.
 */
class StartPagePreparingSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun anEmptyTabWaitsInsteadOfDrawingTheStartPage() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainPreparing,
                    onIntent = {},
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(PREPARING_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(START_GREETING_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(START_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PREPARING_PROGRESS_TEST_TAG).assertExists()
    }

    @Test
    fun theNewTabChooserHidesTheAddressBoxUntilTheLibraryIsReady() {
        val intents = mutableListOf<NewTabIntent>()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                NewTabContent(
                    state = NewTabUiState(
                        startPageGate = PreviewStates.browserMainPreparing.startPageGate,
                    ),
                    onIntent = { intents += it },
                    composer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(PREPARING_TEST_TAG).assertExists()
        compose.onNodeWithTag(NEW_TAB_ADDRESS_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(NEW_TAB_ACTION_ROW_TEST_TAG).assertExists()
        assertEquals(emptyList<NewTabIntent>(), intents)
    }

    @Test
    fun aRetryableHoldOffersToTryAgain() {
        val intents = mutableListOf<BrowserMainIntent>()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainPreparing.copy(
                        startPageGate = PreviewStates.browserMainPreparing.startPageGate.copy(
                            hold = TaffyPartHold.WRONG_CONTENTS,
                        ),
                    ),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(PREPARING_RETRY_TEST_TAG).assertExists().assertHasClickAction()
            .performClick()
        assertEquals(listOf(BrowserMainIntent.RetryPageTools), intents)
        compose.onNodeWithTag(PREPARING_TEST_TAG).assertExists()
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextReplacement
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-307 — the correction sheet.
 *
 * What the page said stays on the screen next to what the user typed. A sheet
 * that replaced the page's value would destroy the evidence the workspace
 * exists to keep, so this test asserts both are present at once.
 */
class FactCorrectionSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<FactCorrectionIntent>()

    @Test
    fun thePageValueIsShownBesideTheUsersOwn() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText("62,999").assertExists()
        compose.onNodeWithTag(VALUE_TEST_TAG).assertExists()
        compose.onNodeWithTag(NOTICE_TEST_TAG).assertExists()
    }

    @Test
    fun typingSendsTheValueAndNothingIsSavedYet() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(VALUE_TEST_TAG).performTextReplacement("61,499")

        assertEquals(listOf(FactCorrectionIntent.ValueChanged("61,499")), intents)
    }

    @Test
    fun thereIsNothingToSaveUntilSomethingChanged() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SAVE_TEST_TAG).assertIsNotEnabled()
    }

    @Test
    fun aChangedValueCanBeSaved() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection.copy(enteredValue = "61,499"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SAVE_TEST_TAG).assertIsEnabled().performClick()

        assertEquals(listOf(FactCorrectionIntent.Save), intents)
    }

    /**
     * The keyboard's Done key and the Save button are one control under two
     * glyphs. This is the half that used to be missing: the field declared no
     * action at all, so the key was whatever the platform defaulted to and it
     * did nothing. It now sends the same intent Save sends.
     */
    @Test
    fun theKeyboardsDoneKeySavesTheSameWaySaveDoes() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection.copy(enteredValue = "61,499"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(VALUE_TEST_TAG).performImeAction()

        assertEquals(listOf(FactCorrectionIntent.Save), intents)
    }

    /**
     * And it agrees with Save about *when*. A key that could save what the
     * button refuses to save would be the second code path this wiring exists
     * to avoid, so with nothing changed the key saves nothing.
     */
    @Test
    fun theDoneKeySavesNothingWhileSaveIsDisabled() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SAVE_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(VALUE_TEST_TAG).performImeAction()

        assertEquals(emptyList<FactCorrectionIntent>(), intents)
    }

    @Test
    fun leavingWithoutRecordingAnythingIsAlwaysAvailable() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = WorkspacePreviewStates.factCorrection,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(CANCEL_TEST_TAG).assertIsEnabled().performClick()

        assertEquals(listOf(FactCorrectionIntent.Cancel), intents)
    }

    @Test
    fun aFactThatNoLongerResolvesSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                FactCorrectionContent(
                    state = FactCorrectionUiState(missing = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(VALUE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FACT_LOADING_TEST_TAG).assertDoesNotExist()
        assertEquals(context.getString(R.string.taffy_fact_correction_missing_title).isNotBlank(), true)
    }

    @Test
    fun aLoadingFactShowsSkeletonsInsteadOfGone() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                FactCorrectionContent(
                    state = FactCorrectionUiState(loading = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(FACT_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(VALUE_TEST_TAG).assertDoesNotExist()
    }
}

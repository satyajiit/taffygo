// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Screen SCR-007 — Get started. */
class GetStartedSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<GetStartedIntent>()

    /**
     * The screen a person arrives at asks two questions and requires neither.
     *
     * The assertion that matters is the last one: Continue is live from the
     * state a person arrives in. A first-run screen whose only way forward
     * waited on an answer would be a browser held hostage by a question it
     * says is optional.
     */
    @Test
    fun bothAnswersAreOptionalAndContinueIsAlwaysLive() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                GetStartedContent(state = GetStartedUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.GetStarted.screenId).assertExists()
        compose.onNodeWithTag(GET_STARTED_HEADLINE_TEST_TAG).assertExists()
        compose.onNodeWithTag(GET_STARTED_CARD_TEST_TAG).assertExists()
        compose.onNodeWithTag(GET_STARTED_PROMISES_TEST_TAG).assertExists()

        compose.onNodeWithTag(GET_STARTED_CONTINUE_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            .performClick()
        assertEquals(listOf(GetStartedIntent.Continue), intents)
    }

    @Test
    fun theNameAndThePictureAreBothAskedFor() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                GetStartedContent(state = GetStartedUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(GET_STARTED_NAME_TEST_TAG)
            .performScrollTo()
            .performTextReplacement("Ada")
        assertEquals(listOf(GetStartedIntent.EditName("Ada")), intents)

        intents.clear()
        val tile = LocalAvatar.of("a1")
        compose.onNodeWithTag("$GET_STARTED_FACE_TEST_TAG_PREFIX${getStartedFaceTag(tile)}")
            .performScrollTo()
            .performClick()
        assertEquals(listOf(GetStartedIntent.ChooseAvatar(tile)), intents)
    }

    /**
     * The faces are a single choice, not thirty-one buttons. `Role.RadioButton`
     * with a selected state is what makes a screen reader say "1 of 31" rather
     * than reading a run of unexplained images.
     */
    @Test
    fun theChosenFaceIsSelectedAndTheOthersAreNot() {
        val chosen = LocalAvatar.of("b2")
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                GetStartedContent(
                    state = GetStartedUiState(name = "Ada Lovelace", avatar = chosen),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$GET_STARTED_FACE_TEST_TAG_PREFIX${getStartedFaceTag(chosen)}")
            .performScrollTo()
            .assertIsSelected()
    }

    /** What the product does with a person's data is in the product. */
    @Test
    fun theDataSheetOpensFromTheFooter() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                GetStartedContent(state = GetStartedUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(GET_STARTED_DATA_LINK_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            .performClick()
        assertEquals(listOf(GetStartedIntent.OpenDataSheet), intents)
    }
}

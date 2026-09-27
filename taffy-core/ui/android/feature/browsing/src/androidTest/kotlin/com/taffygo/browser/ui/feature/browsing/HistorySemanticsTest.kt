// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.isHeading
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-201 — History.
 *
 * Empty is a sentence, not a blank. Delete is a labelled button, not swipe
 * alone. Clear is a header action. Day groups are headings.
 */
class HistorySemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<HistoryIntent>()

    @Test
    fun theScreenAndSearchExist() {
        show(PreviewStates.history)

        compose.onNodeWithTag(TaffyDestination.History.screenId).assertExists()
        compose.onNodeWithTag(HISTORY_SEARCH_TEST_TAG).assertExists()
        compose.onNodeWithTag(HISTORY_CLEAR_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(HISTORY_CAPTION_TEST_TAG).assertExists()
    }

    @Test
    fun dayGroupsAreHeadingsAndEachVisitIsARow() {
        show(PreviewStates.history)

        val today = context.getString(R.string.taffy_pages_history_today)
        compose.onNodeWithText(today).assert(isHeading())
        compose.onNodeWithTag("${HISTORY_VISIT_TEST_TAG_PREFIX}hv_1").assertExists()
        compose.onNodeWithTag("${HISTORY_VISIT_TEST_TAG_PREFIX}hv_2").assertExists()
        compose.onNodeWithTag("${HISTORY_VISIT_TEST_TAG_PREFIX}hv_3").assertExists()
        compose.onNodeWithTag("${HISTORY_DELETE_TEST_TAG_PREFIX}hv_1")
            .assertExists()
            .assertHasClickAction()
    }

    @Test
    fun clearSendsClear() {
        show(PreviewStates.history)

        compose.onNodeWithTag(HISTORY_CLEAR_TEST_TAG).performClick()

        assertEquals(listOf(HistoryIntent.Clear), intents)
    }

    @Test
    fun deleteIsAButtonAndConfirmSendsTheVisitId() {
        show(PreviewStates.history)

        compose.onNodeWithTag("${HISTORY_DELETE_TEST_TAG_PREFIX}hv_1").performClick()
        compose.onNodeWithTag(HISTORY_CONFIRM_TEST_TAG).assertExists()
        compose.onNodeWithTag("${HISTORY_CONFIRM_TEST_TAG}_confirm").performClick()

        assertEquals(listOf(HistoryIntent.Delete(HistoryVisit.Id("hv_1"))), intents)
    }

    @Test
    fun openingAVisitSendsThatIdentifier() {
        show(PreviewStates.history)

        compose.onNodeWithTag("${HISTORY_VISIT_TEST_TAG_PREFIX}hv_1").performClick()

        assertEquals(listOf(HistoryIntent.Open(HistoryVisit.Id("hv_1"))), intents)
    }

    @Test
    fun emptyIsAStateTheScreenRenders() {
        show(HistoryUiState())

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_pages_history_empty_title))
            .assertExists()
    }

    @Test
    fun unavailableDoesNotInventVisits() {
        show(PreviewStates.historyUnavailable)

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag("${HISTORY_VISIT_TEST_TAG_PREFIX}hv_1").assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.taffy_pages_history_unavailable_title))
            .assertExists()
    }

    @Test
    fun loadingNamesTheFirstSkeleton() {
        show(PreviewStates.historyLoading)

        compose.onNodeWithTag(HISTORY_LOADING_TEST_TAG).assertExists()
        compose.onNode(
            SemanticsMatcher.expectValue(
                SemanticsProperties.ContentDescription,
                listOf(context.getString(R.string.taffy_pages_history_loading)),
            ),
        ).assertExists()
    }

    private fun show(state: HistoryUiState) {
        intents.clear()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                HistoryContent(state = state, onIntent = { intents += it })
            }
        }
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Test

/**
 * Screens SCR-101 and SCR-102 — the bottom row has three shapes, and which one
 * it is is a fact about the task rather than about the tab.
 *
 * Two things that used to be true and are the reason this file exists. The
 * start page kept its four-slot dock for the whole of a task, so a person who
 * typed a request into the box watched the bar stay exactly as it had been
 * while Taffy worked — the pill only ever appeared over a page. And the slot
 * the pill goes in is a fixed [ActionRowHeight] that clips, so a bar that grew
 * taller than a pill was sliced with nothing failing anywhere; the assertion
 * that catches that is a height, not a tag.
 *
 * The third shape came later and for a measured reason (decision 0141): while
 * Taffy is driving, back, forward and tabs are refused anyway, and the width
 * they hold is the width the pill's one sentence does not have. They close in
 * place rather than the row being swapped, so these cases assert that they are
 * gone from the tree once closed — a control that has merely shrunk to nothing
 * is still a control to a screen reader, and that is what the assertion is for.
 */
class BrowserAssistantRowSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun aTaskUnderWayTakesTheWholeRowForThePill() {
        showStartPage(hasTask = true, taskEnded = false)

        compose.onNodeWithTag(NEW_TAB_ASSISTANT_ROW_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(ASSISTANT_SLOT_TEST_TAG).assertExists().assertIsDisplayed()
        // Every other control is refused while Taffy drives, so none is drawn:
        // a disabled target in the position nearest the thumb is worse than an
        // absent one, and the pixels are the sentence's.
        listOf(
            TABS_TEST_TAG,
            BACK_TEST_TAG,
            FORWARD_TEST_TAG,
            NEW_TAB_DOWNLOADS_TEST_TAG,
            NEW_TAB_WORKSPACES_TEST_TAG,
            NEW_TAB_SETTINGS_TEST_TAG,
        ).forEach { tag -> compose.onNodeWithTag(tag).assertDoesNotExist() }
    }

    /**
     * A task that has ended keeps the pill and gets the controls back.
     *
     * On the start page that is the pill beside Tabs rather than the dock: the
     * pill still has its last line to say, and the three dock slots it displaced
     * are one hop behind the one that stays.
     */
    @Test
    fun aFinishedTaskKeepsThePillAndGivesTabsBack() {
        showStartPage(hasTask = true, taskEnded = true)

        compose.onNodeWithTag(NEW_TAB_ASSISTANT_ROW_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(ASSISTANT_SLOT_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(TABS_TEST_TAG).assertExists().assertIsDisplayed()

        compose.onNodeWithTag(NEW_TAB_ACTION_ROW_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theDockComesBackWholeWhenThereIsNoTask() {
        showStartPage(hasTask = false, taskEnded = false)

        compose.onNodeWithTag(NEW_TAB_ACTION_ROW_TEST_TAG).assertExists().assertIsDisplayed()
        listOf(
            NEW_TAB_DOWNLOADS_TEST_TAG,
            NEW_TAB_WORKSPACES_TEST_TAG,
            TABS_TEST_TAG,
            NEW_TAB_SETTINGS_TEST_TAG,
        ).forEach { tag -> compose.onNodeWithTag(tag).assertExists() }
        compose.onNodeWithTag(NEW_TAB_ASSISTANT_ROW_TEST_TAG).assertDoesNotExist()
    }

    /**
     * Over a page, the same rule, and the same controls back at the end of it.
     *
     * Asserted on the page row as well as the start row because the two rows
     * are chosen in one `when` and a rule that held on only one of them is
     * exactly the drift that put a Back and a Forward on a blank tab.
     */
    @Test
    fun aPageGetsItsHistoryControlsBackWhenTheTaskEnds() {
        val ended = mutableStateOf(false)
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(
                        takeover = TakeoverUiState(hasTask = true, taskEnded = ended.value),
                    ),
                    onIntent = {},
                    startComposer = previewStartComposer(),
                    assistantBar = { StandInPill() },
                )
            }
        }

        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertExists()
        compose.onNodeWithTag(BACK_TEST_TAG).assertDoesNotExist()

        ended.value = true
        compose.waitForIdle()
        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertExists()
        compose.onNodeWithTag(BACK_TEST_TAG).assertExists()
        compose.onNodeWithTag(FORWARD_TEST_TAG).assertExists()
        compose.onNodeWithTag(TABS_TEST_TAG).assertExists()
        compose.onNodeWithTag(ASSISTANT_SLOT_TEST_TAG).assertExists()
    }

    /**
     * The slot is exactly one pill tall, on every row that has one.
     *
     * It wraps what it is given, so this is the height of whatever the
     * assistant feature put there — and the row around it is a fixed
     * [ActionRowHeight] that clips, so anything taller is drawn sliced with
     * nothing failing. Asserted on the slot rather than on the bar because the
     * slot is the half this module owns: the contract is "one pill tall", and
     * this is where it is kept.
     */
    @Test
    fun theAssistantSlotIsExactlyOnePillTallOnEveryRowThatHasOne() {
        // One composition, switched between the rows: the rule allows one
        // `setContent` per test, and what is under test is that every row that
        // holds the slot keeps the same contract with it.
        val onAPage = mutableStateOf(false)
        val ended = mutableStateOf(false)
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                val base =
                    if (onAPage.value) PreviewStates.browserMain else PreviewStates.browserMainEmptyTab
                BrowserMainContent(
                    state = base.copy(
                        takeover = TakeoverUiState(hasTask = true, taskEnded = ended.value),
                    ),
                    onIntent = {},
                    startComposer = previewStartComposer(),
                    assistantBar = { StandInPill() },
                )
            }
        }

        listOf(false to false, false to true, true to false, true to true).forEach { (page, done) ->
            onAPage.value = page
            ended.value = done
            compose.waitForIdle()
            compose.onNodeWithTag(ASSISTANT_SLOT_TEST_TAG).assertHeightIsEqualTo(PillHeight)
        }
    }

    private fun showStartPage(hasTask: Boolean, taskEnded: Boolean) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab.copy(
                        takeover = TakeoverUiState(hasTask = hasTask, taskEnded = taskEnded),
                    ),
                    onIntent = {},
                    startComposer = previewStartComposer(),
                    assistantBar = { StandInPill() },
                )
            }
        }
    }

    /**
     * A pill of the right height and nothing else.
     *
     * The real one belongs to the assistant feature, which this module does not
     * depend on; what this file is about is the row, and a stand-in of the
     * pill's own height is exactly the contract the row has with whatever fills
     * the slot.
     */
    @Composable
    private fun StandInPill() {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(PillHeight)
                .testTag(STAND_IN_PILL_TEST_TAG),
        ) {
            Text("Done — 3 sources, 1 conflict")
        }
    }

    private companion object {
        /** `TaffyAssistantPill`'s one height, in every state it has. */
        val PillHeight = 56.dp
        const val STAND_IN_PILL_TEST_TAG = "stand_in_pill"
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG
import com.taffygo.browser.ui.core.ui.TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.controlTestTag
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-303 — the task view, in the states a person actually meets.
 *
 * The controls stay in a fixed footer while details scroll; the state is a word
 * rather than a colour, and stopped is never dressed up as finished.
 */
class TaskViewSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<TaskViewIntent>()

    private fun scrollTo(tag: String) {
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag(tag))
    }

    @Test
    fun theHeaderCarriesTheStateWordAndTheFooterCarriesTheControls() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskRunning,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.TaskView.screenId).assertExists()
        compose.onNodeWithTag(TASK_HEADER_TEST_TAG).assertExists()
        val running = context.getString(
            StatusPresentation.of(AssistantPreviewStates.taskRunning.state!!).labelRes,
        )
        compose
            .onNode(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, running))
            .assertExists()
        AssistantPreviewStates.taskRunning.controls.forEach { control ->
            compose.onNodeWithTag(controlTestTag(control)).assertExists().assertHasClickAction()
        }
    }

    @Test
    fun theHistorySourcesAndFindingsRemainReachable() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskRunning,
                    onIntent = { intents += it },
                )
            }
        }

        listOf(TIMELINE_TEST_TAG, SOURCES_TEST_TAG, OUTPUT_TEST_TAG).forEach { tag ->
            scrollTo(tag)
            compose.onNodeWithTag(tag).assertExists()
        }
        scrollTo("${TIMELINE_ENTRY_TEST_TAG_PREFIX}1")
        compose.onNodeWithTag("${TIMELINE_ENTRY_TEST_TAG_PREFIX}1").assertExists()
        scrollTo("${SOURCE_TEST_TAG_PREFIX}docs.example.test")
        compose.onNodeWithTag("${SOURCE_TEST_TAG_PREFIX}docs.example.test").assertExists()
        scrollTo("${FACT_TEST_TAG_PREFIX}f1")
        compose.onNodeWithTag("${FACT_TEST_TAG_PREFIX}f1").assertExists()
    }

    @Test
    fun aFailedTaskNamesTheProviderAndOffersSetup() {
        var opened = 0
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskFailedProvider,
                    onIntent = { intents += it },
                    onOpenSetup = { opened++ },
                )
            }
        }

        compose.onNodeWithTag(TASK_FAILURE_TEST_TAG)
            .assertTextEquals(context.getString(R.string.taffy_assistant_failed_provider))
        compose.onNodeWithTag(TASK_SETUP_TEST_TAG).assertExists()
        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG).performClick()
        assertEquals(1, opened)
        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG).performClick()

        assertEquals(listOf<TaskViewIntent>(TaskViewIntent.DismissSetup), intents)
    }

    @Test
    fun notNowHidesTheOfferAndKeepsTheReason() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskFailedProvider.copy(setupDismissed = true),
                    onIntent = { intents += it },
                    onOpenSetup = {},
                )
            }
        }

        compose.onNodeWithTag(TASK_SETUP_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TASK_FAILURE_TEST_TAG).assertExists()
    }

    @Test
    fun aFinishedTaskOffersRememberThisOnce() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskPartlyDone,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(REMEMBER_THIS_CARD_TEST_TAG).assertExists()
        compose.onNodeWithTag(REMEMBER_THIS_STATEMENT_TEST_TAG).assertExists()
        compose.onNodeWithTag(REMEMBER_THIS_NOT_NOW_TEST_TAG).performClick()

        assertEquals(listOf(TaskViewIntent.DismissRememberThis), intents)
    }

    @Test
    fun aRunningTaskDoesNotOfferRememberThis() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskRunning.copy(
                        rememberThis = AssistantPreviewStates.rememberThis,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(REMEMBER_THIS_CARD_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun anUnsavedFinishedResultOffersOneExplicitWorkspaceSave() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskPartlyDone.copy(
                        canSaveWorkspace = true,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SAVE_WORKSPACE_TEST_TAG).performClick()
        assertEquals(listOf(TaskViewIntent.SaveWorkspace), intents)
    }

    @Test
    fun aFinishedTaskShowsNoControls() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskPartlyDone,
                    onIntent = { intents += it },
                )
            }
        }

        TaskControl.entries.forEach { control ->
            compose.onNodeWithTag(controlTestTag(control)).assertDoesNotExist()
        }
        val partlyDone = context.getString(
            StatusPresentation.of(AssistantPreviewStates.taskPartlyDone.state!!).labelRes,
        )
        compose
            .onNode(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, partlyDone))
            .assertExists()
    }

    /**
     * The defect, on a screen. A task started in a build with nothing behind
     * the executor seam drew "Running" over "Nothing has happened yet" and left
     * a person to work the rest out by waiting. Both places a person looks now
     * carry the account: the line under the state chip, and the timeline where
     * the steps would have been. The generic empty state is gone with it,
     * because "Nothing has happened yet" promises that something will.
     */
    @Test
    fun aTaskNothingIsDrivingSaysSoBesideTheStateAndInTheTimeline() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskNotDriven,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(NOT_DRIVEN_TEST_TAG).assertExists()
        scrollTo(TIMELINE_NOTICE_TEST_TAG)
        compose.onNodeWithTag(TIMELINE_NOTICE_TEST_TAG).assertExists()
        compose
            .onNodeWithText(context.getString(R.string.taffy_task_view_timeline_empty_title))
            .assertDoesNotExist()
        // The task is not hidden either: its state and its controls stay
        // exactly where they were.
        compose.onNodeWithTag(TASK_HEADER_TEST_TAG).assertExists()
        compose.onNodeWithTag(controlTestTag(TaskControl.STOP)).assertExists()
    }

    @Test
    fun anApprovalNamesTheOriginAndOffersBothAnswers() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskRunning.copy(
                        approvalActionId = "action-preview",
                        approvalCount = 2,
                        approvalHost = "shop.example.test",
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(APPROVAL_TEST_TAG).assertExists()
        compose.onNodeWithTag(APPROVE_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(DENY_TEST_TAG).assertExists().performClick()

        assertEquals(listOf(TaskViewIntent.DenyAction), intents)
    }

    @Test
    fun theMaximumWorkspaceOutputComposesFactsOnlyInsideTheViewport() {
        val template = AssistantPreviewStates.taskRunning.facts.first()
        val facts = (0 until 256).map { index ->
            template.copy(
                id = FactId("large_$index"),
                field = "Field $index",
                value = "Value $index",
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskRunning.copy(facts = facts),
                    onIntent = { intents += it },
                )
            }
        }

        val last = "${FACT_TEST_TAG_PREFIX}large_255"
        compose.onNodeWithTag(last).assertDoesNotExist()
        scrollTo(last)
        compose.onNodeWithTag(last).assertExists()
        compose.onNodeWithTag("${FACT_TEST_TAG_PREFIX}large_0").assertDoesNotExist()
    }
}

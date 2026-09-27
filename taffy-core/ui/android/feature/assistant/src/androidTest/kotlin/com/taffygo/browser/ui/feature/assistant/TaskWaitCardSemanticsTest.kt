// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** The one card on the page for the one wait a task has open. */
class TaskWaitCardSemanticsTest {
    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<AssistantBarIntent>()

    private val waiting = AssistantBarUiState(
        taskId = "task-1",
        taskRevision = 3u,
        state = TaskDisplayState.WAITING_FOR_YOU,
        controls = listOf(TaskControl.STOP),
    )

    @Test
    fun aHandoverNamesTheSiteAndHandsThePageBackToTaffy() {
        val state = waiting.copy(
            hasHandover = true,
            latestStep = TaskTimelineEntry(
                sequence = 4,
                kind = TaskTimelineKind.READ_PAGE,
                host = "uidai.gov.in",
            ),
        )
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskWaitCardContent(state = state, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(HANDOVER_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_task_wait_handover_title, "uidai.gov.in"),
        ).assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_task_wait_finish)).assertExists()
        compose.onNodeWithTag(HANDOVER_DONE_TEST_TAG).assertExists().performClick()
        compose.onNodeWithTag(HANDOVER_STOP_TEST_TAG).assertExists().performClick()

        assertEquals(
            listOf(AssistantBarIntent.CompleteHandover, AssistantBarIntent.Control(TaskControl.STOP)),
            intents,
        )
    }

    @Test
    fun aHandoverWithNoSiteNamedSaysThisPage() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                TaskWaitCardContent(
                    state = waiting.copy(hasHandover = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_task_wait_handover_title_here))
            .assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_task_wait_finish)).assertExists()
    }

    @Test
    fun anAskOffersAFieldAndSendsTheTypedAnswer() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskWaitCardContent(
                    state = waiting.copy(hasAsk = true, askPrompt = "Which plan did you mean?"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(ASK_TEST_TAG).assertExists()
        compose.onNodeWithText("Which plan did you mean?").assertExists()
        compose.onNodeWithTag(ASK_SEND_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(ASK_FIELD_TEST_TAG).performTextInput("the blue one")
        compose.onNodeWithTag(ASK_SEND_TEST_TAG).assertExists().performClick()

        assertEquals(listOf(AssistantBarIntent.Answer("the blue one")), intents)
    }

    @Test
    fun aCodeShapedAnswerIsRefusedAtTheCard() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskWaitCardContent(state = waiting.copy(hasAsk = true), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(ASK_FIELD_TEST_TAG).performTextInput("482 913")

        compose.onNodeWithTag(ASK_CREDENTIAL_TEST_TAG).assertExists()
        compose.onNodeWithTag(ASK_SEND_TEST_TAG).assertIsNotEnabled()
    }
}

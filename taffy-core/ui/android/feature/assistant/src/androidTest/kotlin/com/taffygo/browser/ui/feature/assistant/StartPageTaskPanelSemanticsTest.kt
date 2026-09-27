// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Taffy working under the start page's box, and the two doors once it has stopped. */
class StartPageTaskPanelSemanticsTest {
    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<AssistantBarIntent>()
    private val pressed = mutableListOf<String>()

    private val running = AssistantBarUiState(
        taskId = "task-1",
        taskRevision = 2u,
        state = TaskDisplayState.RUNNING,
        readiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
    )

    private fun show(state: AssistantBarUiState, canOpenSetup: Boolean = true) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                StartPageTaskPanelContent(
                    state = state,
                    taskId = "task-1",
                    goal = "download my aadhaar",
                    onIntent = { intents += it },
                    onTryAgain = { pressed += "try-again" },
                    onLeave = { pressed += "home" },
                    onOpenSetup = if (canOpenSetup) ({ pressed += "set-up" }) else null,
                )
            }
        }
    }

    @Test
    fun aTaskUnderWayShowsTheWordsAndNoDoors() {
        show(running)

        compose.onNodeWithTag(START_PAGE_TASK_GOAL_TEST_TAG).assertExists()
        compose.onNodeWithText("download my aadhaar").assertExists()
        compose.onNodeWithTag(START_PAGE_TASK_TRY_AGAIN_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(START_PAGE_TASK_HOME_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aFailedTaskOffersTheSameRequestAgainAndHome() {
        show(
            running.copy(
                state = TaskDisplayState.FAILED,
                failure = TaskFailureReason.PROVIDER_UNAVAILABLE,
            ),
        )

        compose.onNodeWithText(context.getString(R.string.taffy_assistant_failed_provider))
            .assertExists()
        compose.onNodeWithTag(START_PAGE_TASK_TRY_AGAIN_TEST_TAG).performClick()
        compose.onNodeWithTag(START_PAGE_TASK_HOME_TEST_TAG).performClick()

        assertEquals(listOf("try-again", "home"), pressed)
    }

    @Test
    fun aFailedTaskWithNothingSetUpOffersSetUpInsteadOfTheSameRequest() {
        show(
            running.copy(
                state = TaskDisplayState.FAILED,
                failure = TaskFailureReason.PROVIDER_UNAVAILABLE,
                readiness = TaffyReadiness.NotSetUp,
            ),
        )

        compose.onNodeWithTag(START_PAGE_TASK_TRY_AGAIN_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(START_PAGE_TASK_SETUP_TEST_TAG).performClick()
        compose.onNodeWithTag(START_PAGE_TASK_HOME_TEST_TAG).assertExists()

        assertEquals(listOf("set-up"), pressed)
    }

    @Test
    fun aFinishedTaskOffersItsResultsFirst() {
        show(running.copy(state = TaskDisplayState.DONE, sourcesRead = 2))

        compose.onNodeWithTag(START_PAGE_TASK_RESULTS_TEST_TAG).performClick()
        compose.onNodeWithTag(START_PAGE_TASK_HOME_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_PAGE_TASK_TRY_AGAIN_TEST_TAG).assertDoesNotExist()

        assertEquals(listOf(AssistantBarIntent.OpenResults), intents)
    }

    @Test
    fun aStoppedTaskOffersTheSameRequestAgain() {
        show(running.copy(state = TaskDisplayState.STOPPED))

        compose.onNodeWithTag(START_PAGE_TASK_TRY_AGAIN_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_PAGE_TASK_RESULTS_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(START_PAGE_TASK_HOME_TEST_TAG).assertExists()
    }
}

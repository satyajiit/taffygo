// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class TaskReadAloudSemanticsTest {
    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<TaskViewIntent>()

    @Test
    fun finalAnswerOffersPersonStartedReadAloud() {
        show(ReadAloudUiState.Idle)
        scrollToReadAloud()

        compose.onNodeWithTag(READ_ALOUD_BUTTON_TEST_TAG).performClick()

        assertEquals(listOf(TaskViewIntent.ReadAnswerAloud), intents)
    }

    @Test
    fun activePlaybackHasAVisibleStatusAndStopControl() {
        show(ReadAloudUiState.Speaking)
        scrollToReadAloud()

        compose.onNodeWithTag(READ_ALOUD_STATUS_TEST_TAG).assertExists()
        compose.onNodeWithTag(READ_ALOUD_BUTTON_TEST_TAG).performClick()

        assertEquals(listOf(TaskViewIntent.StopReadAloud), intents)
    }

    private fun show(playback: ReadAloudUiState) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = AssistantPreviewStates.taskPartlyDone.copy(
                        state = TaskDisplayState.DONE,
                        liveAnswer = TaskAnswerProjection(
                            segments = listOf("The visible final answer."),
                            isStreaming = false,
                            isIncomplete = false,
                            isTruncated = false,
                        ),
                        readAloud = playback,
                    ),
                    onIntent = { intents += it },
                )
            }
        }
    }

    private fun scrollToReadAloud() {
        compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(
            hasTestTag(READ_ALOUD_BUTTON_TEST_TAG),
        )
    }
}

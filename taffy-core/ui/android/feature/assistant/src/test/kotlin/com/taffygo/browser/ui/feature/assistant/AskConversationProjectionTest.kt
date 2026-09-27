// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskConversationProjection
import com.taffygo.browser.ui.core.task.TaskExchange
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import taffy.core_api.TaskPhase

/** What the Ask overlay's conversation panel draws for a task, from the repository's state. */
class AskConversationProjectionTest {

    @Test
    fun `before the first delta the conversation is the goal with nothing under it`() {
        val drawn = askConversation(repository(task = null), "task-1", "what is this page for?")

        assertEquals(listOf(TaskExchange("what is this page for?", null)), drawn.exchanges)
    }

    @Test
    fun `the followed task's conversation is drawn as the core folded it`() {
        val folded = TaskConversationProjection(
            listOf(
                TaskExchange("what is this page for?", answer("A retention policy.")),
                TaskExchange("and the fee?", null),
            ),
        )
        val drawn = askConversation(
            repository(task("task-1").copy(conversation = folded)),
            "task-1",
            "what is this page for?",
        )

        assertEquals(folded, drawn)
    }

    @Test
    fun `another task's conversation is never drawn under this task's goal`() {
        val other = task("task-2").copy(
            conversation = TaskConversationProjection(listOf(TaskExchange("other", answer("x")))),
        )

        val drawn = askConversation(repository(other), "task-1", "mine")

        assertEquals("mine", drawn.exchanges.single().question)
        assertNull(drawn.exchanges.single().answer)
    }

    private fun repository(task: TaskProjection?) =
        TaskRepositoryState(CoreUiAvailability.READY, 1u, task)

    private fun task(id: String) = TaskProjection(
        id = id,
        revision = 1u,
        phase = TaskPhase.RUNNING,
        goal = "goal",
        template = TaskTemplate.SUMMARIZE_EVIDENCE,
        progressBasisPoints = 0u,
        statusMessageKey = "task.planning",
        failure = null,
        pendingAction = null,
    )

    private fun answer(text: String) = TaskAnswerProjection(
        segments = listOf(text),
        isStreaming = false,
        isIncomplete = false,
        isTruncated = false,
    )
}

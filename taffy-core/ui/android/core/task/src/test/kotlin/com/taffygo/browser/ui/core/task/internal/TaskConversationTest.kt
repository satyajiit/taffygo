// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The fold from calls and follow-ups to exchanges (decision 0137): the goal
 * is the first question, each follow-up the next, and the calls between two
 * marks are one answer.
 */
internal class TaskConversationTest {
    @Test
    fun `the goal alone is one exchange with no answer yet`() {
        val conversation = conversationOf("what is this page for?", emptyList(), emptyList())

        val exchange = conversation.exchanges.single()
        assertEquals("what is this page for?", exchange.question)
        assertNull(exchange.answer)
        assertNull(conversation.latestAnswer)
    }

    @Test
    fun `two calls before any follow-up are one answer read in order`() {
        val conversation = conversationOf(
            goal = "summarize it",
            calls = listOf(call("Opening it."), call("Here is the answer.", streaming = true)),
            questions = emptyList(),
        )

        val answer = requireNotNull(conversation.exchanges.single().answer)
        assertEquals(listOf("Opening it.", "\n\n", "Here is the answer."), answer.segments)
        assertTrue(answer.isStreaming)
        assertFalse(answer.isIncomplete)
    }

    @Test
    fun `a follow-up splits the calls at the mark it was asked after`() {
        val conversation = conversationOf(
            goal = "summarize it",
            calls = listOf(call("First answer."), call("Second answer.")),
            questions = listOf(AskedQuestion("task-1", "and the fee?", afterCalls = 1)),
        )

        assertEquals(2, conversation.exchanges.size)
        assertEquals("summarize it", conversation.exchanges[0].question)
        assertEquals(listOf("First answer."), conversation.exchanges[0].answer?.segments)
        assertEquals("and the fee?", conversation.exchanges[1].question)
        assertEquals(listOf("Second answer."), conversation.exchanges[1].answer?.segments)
        assertEquals(conversation.exchanges[1].answer, conversation.latestAnswer)
    }

    @Test
    fun `a follow-up not yet answered stands with no answer`() {
        val conversation = conversationOf(
            goal = "summarize it",
            calls = listOf(call("First answer.")),
            questions = listOf(AskedQuestion("task-1", "and the fee?", afterCalls = 1)),
        )

        assertNull(conversation.exchanges[1].answer)
        assertNull(conversation.latestAnswer)
    }

    @Test
    fun `a mark past the calls that exist is clamped rather than trusted`() {
        val conversation = conversationOf(
            goal = "summarize it",
            calls = listOf(call("only")),
            questions = listOf(AskedQuestion("task-1", "more?", afterCalls = 9)),
        )

        assertEquals(listOf("only"), conversation.exchanges[0].answer?.segments)
        assertNull(conversation.exchanges[1].answer)
    }

    @Test
    fun `a merged answer is incomplete or truncated if any of its calls was`() {
        val merged = requireNotNull(
            conversationOf(
                goal = "g",
                calls = listOf(call("a", incomplete = true), call("b", truncated = true)),
                questions = emptyList(),
            ).exchanges.single().answer,
        )

        assertTrue(merged.isIncomplete)
        assertTrue(merged.isTruncated)
        assertFalse(merged.isStreaming)
    }
}

private fun call(
    text: String,
    streaming: Boolean = false,
    incomplete: Boolean = false,
    truncated: Boolean = false,
) = TaskAnswerProjection(
    segments = listOf(text),
    isStreaming = streaming,
    isIncomplete = incomplete,
    isTruncated = truncated,
)

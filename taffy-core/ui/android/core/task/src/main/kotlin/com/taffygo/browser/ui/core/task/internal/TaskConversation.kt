// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskConversationProjection
import com.taffygo.browser.ui.core.task.TaskExchange

/**
 * One follow-up the surfaces sent, and where in the answer stream it landed.
 *
 * [afterCalls] is how many model calls had produced visible text for the task
 * when the question went out. It is what pairs an answer to its question: the
 * calls before that mark answered what came before, and the calls after it
 * answer this. Counting calls rather than reading a call identity off the
 * command keeps the pairing a fact this layer can observe on its own — the
 * command's admission carries no such identity, and the first delta of the
 * reply is the first moment one exists.
 */
internal data class AskedQuestion(
    val taskId: String,
    val text: String,
    val afterCalls: Int,
)

/**
 * The conversation, folded from the task's goal, the calls that streamed
 * visible text, and the follow-ups asked in between.
 *
 * The goal is the first question; every follow-up is the next. The calls
 * between two marks are one answer, read in order, so a turn that spoke twice
 * before the person asked again is still one reply to one question.
 */
internal fun conversationOf(
    goal: String,
    calls: List<TaskAnswerProjection>,
    questions: List<AskedQuestion>,
): TaskConversationProjection {
    val texts = listOf(goal) + questions.map { it.text }
    val marks = listOf(0) + questions.map { it.afterCalls.coerceIn(0, calls.size) }
    val exchanges = texts.indices.map { index ->
        val from = marks[index]
        val to = marks.getOrElse(index + 1) { calls.size }.coerceAtLeast(from)
        TaskExchange(question = texts[index], answer = mergeCalls(calls.subList(from, to)))
    }
    return TaskConversationProjection(exchanges)
}

/**
 * Several calls as one answer: their segments in order, a paragraph break
 * between two calls that each said something, streaming while the last still
 * is, and marked incomplete or truncated if any of them was.
 *
 * Each call is one step the model narrated, and it ends its sentence without
 * the space the next one would need: joined bare, an errand's activity read
 * "result.Looking for a Download Aadhaar control" on a phone.
 */
private fun mergeCalls(calls: List<TaskAnswerProjection>): TaskAnswerProjection? {
    if (calls.isEmpty()) return null
    if (calls.size == 1) return calls.single()
    return TaskAnswerProjection(
        segments = buildList {
            for (call in calls) {
                if (call.segments.isEmpty()) continue
                if (isNotEmpty()) add(BETWEEN_CALLS)
                addAll(call.segments)
            }
        },
        isStreaming = calls.last().isStreaming,
        isIncomplete = calls.any { it.isIncomplete },
        isTruncated = calls.any { it.isTruncated },
    )
}

private const val BETWEEN_CALLS = "\n\n"

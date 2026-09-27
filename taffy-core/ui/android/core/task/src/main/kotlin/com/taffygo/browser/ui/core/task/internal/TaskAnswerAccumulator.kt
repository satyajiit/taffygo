// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.api.TaskAnswerReport
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import taffy.core_api.MAX_TASK_ANSWER_RESIDENCY_BYTES

/**
 * Sequence-checks and bounds one task's process-resident visible answers, one
 * projection per model call.
 *
 * A call is the unit the wire speaks in — every delta names the call it belongs
 * to — and it is the unit the conversation is folded from: the calls that
 * streamed after a person's question are the answer to it. Folding every call
 * into one text, as this once did, made a follow-up's reply indistinguishable
 * from the reply before it.
 *
 * The mutable tail is intentional: token-sized deltas must not copy the entire
 * answer on every event. Once full, a render segment becomes an immutable
 * string that every later [snapshot] reuses by identity, and a call that has
 * ended is sealed as one immutable projection. Snapshot cost therefore depends
 * on the bounded tail of the open call, not on total answer length. The
 * residency byte ceiling is one ceiling for the whole task, across its calls.
 */
internal class TaskAnswerAccumulator {
    private var taskId: String? = null
    private val sealedCalls = mutableListOf<TaskAnswerProjection>()
    private var open: OpenCall? = null
    private var textBytes = 0
    private var hasEvent = false
    private var pendingIncomplete = false

    fun accept(report: TaskAnswerReport) {
        hasEvent = true
        if (!report.hasValidShape()) {
            // A malformed event is charged to the call it interrupted, or to
            // the next one when no call is still speaking: it is a gap in the
            // stream either way, and a gap is never silently closed over. A
            // call whose terminal has arrived is finished, and a gap after it
            // is the next call's.
            val current = open?.takeIf { it.streaming }
            if (current != null) current.incomplete = true else pendingIncomplete = true
            return
        }
        if (taskId != report.taskId) resetForTask(report.taskId)
        val call = open?.takeIf { it.callId == report.callId } ?: beginCall(report.callId)
        call.accept(report)
    }

    fun snapshot(): AccumulatedTaskAnswer? {
        val id = taskId ?: return null
        if (!hasEvent) return null
        return AccumulatedTaskAnswer(
            taskId = id,
            calls = sealedCalls.toList() + listOfNotNull(open?.projection()),
        )
    }

    private fun beginCall(callId: String): OpenCall {
        open?.let { previous ->
            // A call the next one overtook never closed: its text stands,
            // marked incomplete, because nothing said it was finished.
            if (previous.streaming) previous.incomplete = true
            previous.streaming = false
            sealedCalls += previous.projection()
        }
        val call = OpenCall(callId)
        if (pendingIncomplete) call.incomplete = true
        pendingIncomplete = false
        open = call
        return call
    }

    private fun resetForTask(nextTaskId: String) {
        sealedCalls.clear()
        open = null
        textBytes = 0
        taskId = nextTaskId
        pendingIncomplete = false
    }

    /** One model call's text as it arrives, sealed into a projection when the next begins. */
    private inner class OpenCall(val callId: String) {
        private val sealedSegments = mutableListOf<String>()
        private val tail = StringBuilder()
        private var expectedSequence: UInt = 0u
        private var discarding = false
        var streaming = false
        var incomplete = false
        private var truncated = false

        fun accept(report: TaskAnswerReport) {
            if (report.sequence != expectedSequence) {
                incomplete = true
                discarding = true
            }
            if (!discarding && report.sequence == expectedSequence) {
                if (report.terminal) {
                    streaming = false
                    if (!report.complete) incomplete = true
                } else {
                    append(requireNotNull(report.text))
                    streaming = true
                }
            }
            if (report.sequence == UInt.MAX_VALUE) {
                incomplete = true
                discarding = true
            } else {
                expectedSequence = report.sequence + 1u
            }
            if (report.terminal) {
                streaming = false
                discarding = false
            }
        }

        fun projection(): TaskAnswerProjection = TaskAnswerProjection(
            segments = buildList(sealedSegments.size + 1) {
                addAll(sealedSegments)
                if (tail.isNotEmpty()) add(tail.toString())
            },
            isStreaming = streaming,
            isIncomplete = incomplete,
            isTruncated = truncated,
        )

        private fun append(delta: String) {
            val remaining = MAX_TASK_ANSWER_RESIDENCY_BYTES - textBytes
            val bytes = utf8BytesAtMost(delta, remaining)
            if (bytes == null) {
                truncated = true
                return
            }
            appendVisible(delta)
            textBytes += bytes
        }

        private fun appendVisible(value: String) {
            var offset = 0
            while (offset < value.length) {
                if (tail.length == TASK_ANSWER_SEGMENT_CHARACTERS) sealTail()
                val available = TASK_ANSWER_SEGMENT_CHARACTERS - tail.length
                var copied = minOf(available, value.length - offset)
                val end = offset + copied
                if (end < value.length &&
                    value[end - 1].isHighSurrogate() &&
                    value[end].isLowSurrogate()
                ) {
                    if (copied == 1 && tail.isNotEmpty()) {
                        sealTail()
                        continue
                    }
                    copied -= 1
                }
                tail.append(value, offset, offset + copied)
                offset += copied
            }
        }

        private fun sealTail() {
            if (tail.isEmpty()) return
            sealedSegments += tail.toString()
            tail.clear()
        }
    }
}

/** The calls of one task, in the order they spoke, inseparably paired with the task. */
internal data class AccumulatedTaskAnswer(
    val taskId: String,
    val calls: List<TaskAnswerProjection>,
)

/** Counts UTF-8 bytes without allocating a throwaway encoded token buffer. */
private fun utf8BytesAtMost(text: String, maximum: Int): Int? {
    var bytes = 0
    var index = 0
    while (index < text.length) {
        val code = text[index].code
        val width = when {
            code < 0x80 -> 1
            code < 0x800 -> 2
            text[index].isHighSurrogate() &&
                index + 1 < text.length &&
                text[index + 1].isLowSurrogate() -> {
                index++
                4
            }
            // A lone surrogate is encoded as a replacement character.
            else -> 3
        }
        if (bytes > maximum - width) return null
        bytes += width
        index++
    }
    return bytes
}

internal const val TASK_ANSWER_SEGMENT_CHARACTERS = 8_192

private fun TaskAnswerReport.hasValidShape(): Boolean =
    taskId.isNotEmpty() &&
        callId.isNotEmpty() &&
        (terminal == (text == null)) &&
        (!complete || terminal) &&
        (text == null || text.orEmpty().isNotEmpty())

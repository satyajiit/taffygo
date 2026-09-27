// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.api.TaskAnswerReport
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.MAX_TASK_ANSWER_RESIDENCY_BYTES

internal class TaskAnswerAccumulatorTest {
    @Test
    fun everyCallIsItsOwnProjectionInTheOrderItSpoke() {
        val accumulator = TaskAnswerAccumulator()

        accumulator.accept(delta("task-1", "call-1", 0u, "Opening it."))
        accumulator.accept(terminal("task-1", "call-1", 1u))
        accumulator.accept(delta("task-1", "call-2", 0u, "Here is the answer."))

        val calls = requireNotNull(accumulator.snapshot()).calls
        assertEquals(listOf("Opening it.", "Here is the answer."), calls.map(::text))
        assertFalse(calls[0].isStreaming)
        assertTrue(calls[1].isStreaming)
        assertTrue(calls.none { it.isIncomplete || it.isTruncated })
    }

    @Test
    fun aCallOvertakenBeforeItsTerminalIsSealedIncomplete() {
        val accumulator = TaskAnswerAccumulator()

        accumulator.accept(delta("task-1", "call-1", 0u, "never finished"))
        accumulator.accept(delta("task-1", "call-2", 0u, "next"))

        val calls = requireNotNull(accumulator.snapshot()).calls
        assertEquals(2, calls.size)
        assertTrue(calls[0].isIncomplete)
        assertFalse(calls[0].isStreaming)
        assertFalse(calls[1].isIncomplete)
    }

    @Test
    fun sequenceGapNeverSplicesUnknownTextIntoTheAnswer() {
        val accumulator = TaskAnswerAccumulator()

        accumulator.accept(delta("task-1", "call-1", 0u, "kept"))
        accumulator.accept(delta("task-1", "call-1", 2u, "must-not-appear"))
        accumulator.accept(terminal("task-1", "call-1", 3u))

        val answer = only(accumulator)
        assertEquals("kept", text(answer))
        assertTrue(answer.isIncomplete)
        assertFalse(answer.isStreaming)
    }

    @Test
    fun anotherTaskCannotInheritThePreviousTasksProse() {
        val accumulator = TaskAnswerAccumulator()
        accumulator.accept(delta("task-1", "call-1", 0u, "first"))
        accumulator.accept(delta("task-2", "call-1", 0u, "second"))

        val snapshot = requireNotNull(accumulator.snapshot())
        assertEquals("task-2", snapshot.taskId)
        assertEquals(listOf("second"), snapshot.calls.map(::text))
        assertFalse(snapshot.calls.single().isIncomplete)
    }

    @Test
    fun malformedEventCannotRelabelThePreviousTasksProse() {
        val accumulator = TaskAnswerAccumulator()
        accumulator.accept(delta("task-1", "call-1", 0u, "first task only"))
        accumulator.accept(delta("task-2", "", 0u, "invalid"))

        val snapshot = requireNotNull(accumulator.snapshot())
        assertEquals("task-1", snapshot.taskId)
        assertEquals("first task only", text(snapshot.calls.single()))
        assertTrue(snapshot.calls.single().isIncomplete)
    }

    @Test
    fun malformedEventBetweenCallsIsChargedToTheCallThatFollows() {
        val accumulator = TaskAnswerAccumulator()
        accumulator.accept(delta("task-1", "call-1", 0u, "done"))
        accumulator.accept(terminal("task-1", "call-1", 1u))
        accumulator.accept(delta("task-1", "", 0u, "invalid"))
        accumulator.accept(delta("task-1", "call-2", 0u, "next"))

        val calls = requireNotNull(accumulator.snapshot()).calls
        assertFalse(calls[0].isIncomplete)
        assertTrue(calls[1].isIncomplete)
    }

    @Test
    fun residencyCeilingIsOneCeilingForTheWholeTask() {
        val accumulator = TaskAnswerAccumulator()
        val full = "x".repeat(MAX_TASK_ANSWER_RESIDENCY_BYTES)
        accumulator.accept(delta("task-1", "call-1", 0u, full))
        accumulator.accept(terminal("task-1", "call-1", 1u))
        accumulator.accept(delta("task-1", "call-2", 0u, "y"))

        val calls = requireNotNull(accumulator.snapshot()).calls
        assertEquals(MAX_TASK_ANSWER_RESIDENCY_BYTES, calls.sumOf { text(it).length })
        assertFalse(calls[0].isTruncated)
        assertTrue(calls[1].isTruncated)
    }

    @Test
    fun residencyCeilingCountsMultibyteTextWithoutSplittingTheDelta() {
        val accumulator = TaskAnswerAccumulator()
        val full = "😀".repeat(MAX_TASK_ANSWER_RESIDENCY_BYTES / 4)
        accumulator.accept(delta("task-1", "call-1", 0u, full))
        accumulator.accept(delta("task-1", "call-1", 1u, "x"))

        val answer = only(accumulator)
        assertEquals(
            MAX_TASK_ANSWER_RESIDENCY_BYTES,
            answer.segments.sumOf { it.encodeToByteArray().size },
        )
        assertTrue(answer.isTruncated)
    }

    @Test
    fun sealedRenderSegmentsAreBoundedAndReusedAcrossSnapshots() {
        val accumulator = TaskAnswerAccumulator()
        val prefix = "x".repeat(TASK_ANSWER_SEGMENT_CHARACTERS * 2 + 1)
        accumulator.accept(delta("task-1", "call-1", 0u, prefix))
        val before = only(accumulator)

        accumulator.accept(delta("task-1", "call-1", 1u, "tail"))
        val after = only(accumulator)

        assertEquals(prefix + "tail", text(after))
        assertTrue(after.segments.all { it.length <= TASK_ANSWER_SEGMENT_CHARACTERS })
        assertSame(before.segments[0], after.segments[0])
        assertSame(before.segments[1], after.segments[1])
    }

    @Test
    fun aSealedCallIsReusedByIdentityAcrossSnapshots() {
        val accumulator = TaskAnswerAccumulator()
        accumulator.accept(delta("task-1", "call-1", 0u, "first"))
        accumulator.accept(terminal("task-1", "call-1", 1u))
        accumulator.accept(delta("task-1", "call-2", 0u, "sec"))
        val before = requireNotNull(accumulator.snapshot()).calls

        accumulator.accept(delta("task-1", "call-2", 1u, "ond"))
        val after = requireNotNull(accumulator.snapshot()).calls

        assertSame(before[0], after[0])
        assertEquals("second", text(after[1]))
    }

    @Test
    fun renderBoundaryNeverSplitsASurrogatePair() {
        val accumulator = TaskAnswerAccumulator()
        val answer = "x".repeat(TASK_ANSWER_SEGMENT_CHARACTERS - 1) + "😀" + "tail"
        accumulator.accept(delta("task-1", "call-1", 0u, answer))

        val projected = only(accumulator)
        assertEquals(answer, text(projected))
        projected.segments.forEach { segment ->
            assertFalse(segment.last().isHighSurrogate())
            assertFalse(segment.first().isLowSurrogate())
        }
    }

    @Test
    fun unreadableTerminalLabelsRetainedPartialTextIncomplete() {
        val accumulator = TaskAnswerAccumulator()
        accumulator.accept(delta("task-1", "call-1", 0u, "partial"))
        accumulator.accept(terminal("task-1", "call-1", 1u, complete = false))

        val answer = only(accumulator)
        assertEquals("partial", text(answer))
        assertTrue(answer.isIncomplete)
        assertFalse(answer.isStreaming)
    }
}

private fun only(accumulator: TaskAnswerAccumulator): TaskAnswerProjection =
    requireNotNull(accumulator.snapshot()).calls.single()

private fun text(projection: TaskAnswerProjection): String = projection.segments.joinToString("")

private fun delta(task: String, call: String, sequence: UInt, text: String) =
    TaskAnswerReport(task, call, sequence, text, terminal = false, complete = false)

private fun terminal(task: String, call: String, sequence: UInt, complete: Boolean = true) =
    TaskAnswerReport(task, call, sequence, text = null, terminal = true, complete = complete)

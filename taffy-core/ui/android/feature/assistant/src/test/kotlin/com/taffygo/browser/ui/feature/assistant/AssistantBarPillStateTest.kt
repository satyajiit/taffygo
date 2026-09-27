// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.TaffyAssistantPillState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/**
 * The bar's one projection, and the reason it has to be total.
 *
 * It used to answer null for the four final states and for a task nothing is
 * driving, and null meant "this one is drawn some other way" — a status chip, a
 * line and a button in a column, handed to an action row that is a fixed height
 * and clips. Nothing failed anywhere: the bar's own tests render it with no
 * height at all, and the phone showed a sliced card. A total function is what
 * removes the place a new task state could fall through to.
 */
class AssistantBarPillStateTest {

    @Test
    fun `every bar state the machine can produce has a pill to draw it`() {
        val displays = listOf(null) + TaskDisplayState.entries
        val notices = listOf(null) + TaskNotice.entries
        val controlSets = listOf(emptyList(), listOf(TaskControl.RESUME), TaskControl.entries)
        displays.forEach { display ->
            notices.forEach { notice ->
                controlSets.forEach { controls ->
                    val state = AssistantBarUiState(
                        state = display,
                        notice = notice,
                        controls = controls,
                    )
                    // The assertion is that this returns at all: the type is
                    // non-null, so a state with no arm is a compile error and a
                    // state with the wrong arm is one of the cases below.
                    pillStateOf(state)
                }
            }
        }
    }

    @Test
    fun `a task nothing is driving is held rather than running`() {
        val state = AssistantBarUiState(
            state = TaskDisplayState.RUNNING,
            notice = TaskNotice.CORE_UNAVAILABLE,
            controls = listOf(TaskControl.PAUSE, TaskControl.STOP),
        )

        // Not RUNNING: the running pill draws a growing rail, and the line has
        // just said nothing is driving this task.
        assertEquals(TaffyAssistantPillState.HELD, pillStateOf(state))
        assertNotEquals(TaffyAssistantPillState.RUNNING, pillStateOf(state))
    }

    @Test
    fun `a held task offers Resume only where the revision admits it`() {
        val admitted = AssistantBarUiState(
            state = TaskDisplayState.PAUSED,
            controls = listOf(TaskControl.RESUME, TaskControl.STOP),
        )
        val refused = admitted.copy(controls = listOf(TaskControl.STOP))

        assertEquals(TaffyAssistantPillState.PAUSED, pillStateOf(admitted))
        assertEquals(TaffyAssistantPillState.HELD, pillStateOf(refused))
    }

    @Test
    fun `a finished task keeps its own state rather than falling back to a card`() {
        val expected = mapOf(
            TaskDisplayState.DONE to TaffyAssistantPillState.DONE,
            TaskDisplayState.PARTLY_DONE to TaffyAssistantPillState.PARTLY_DONE,
            TaskDisplayState.STOPPED to TaffyAssistantPillState.STOPPED,
            TaskDisplayState.FAILED to TaffyAssistantPillState.FAILED,
        )

        expected.forEach { (display, pill) ->
            assertEquals(pill, pillStateOf(AssistantBarUiState(state = display)))
        }
        // And a notice cannot take a final state's word away, because a
        // finished task's line is exactly what happened to it.
        expected.forEach { (display, pill) ->
            val noticed = AssistantBarUiState(state = display, notice = TaskNotice.CORE_UNAVAILABLE)
            assertEquals(pill, pillStateOf(noticed))
        }
    }

    @Test
    fun `no task at all is the idle invitation`() {
        assertEquals(TaffyAssistantPillState.IDLE, pillStateOf(AssistantBarUiState()))
        assertEquals(
            TaffyAssistantPillState.IDLE,
            pillStateOf(AssistantBarUiState(listening = true)),
        )
    }
}

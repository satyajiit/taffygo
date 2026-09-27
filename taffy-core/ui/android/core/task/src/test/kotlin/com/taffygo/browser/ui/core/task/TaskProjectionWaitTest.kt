// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.task.internal.admits
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * Which door a waiting task is held at, and why a form is no longer read off a
 * message key.
 *
 * The key is what the browser would *say*; the request identity is what is
 * *true*. The two disagree whenever the core has a better sentence to print,
 * which is precisely when a person is waiting to be asked for something and the
 * screen is silent.
 */
class TaskProjectionWaitTest {

    @Test
    fun `a task waiting on nothing is at no door`() {
        val projection = projection(statusMessageKey = "task.acting")

        assertNull(projection.wait)
        assertFalse(projection.hasHandover)
        assertFalse(projection.hasAsk)
        assertFalse(projection.hasInputRequest)
    }

    @Test
    fun `a form is read off the contract field rather than off a message key`() {
        val projection = projection(
            statusMessageKey = "task.waiting_for_field_values",
            inputRequest = "req-1",
        )

        assertEquals(TaskProjection.Wait.FIELD_VALUES, projection.wait)
        assertTrue(projection.hasInputRequest)
        assertFalse(projection.hasAsk)
    }

    @Test
    fun `a form still counts when the browser chose to say something else`() {
        // The core's precedence prints one sentence for a task holding several
        // things open. A projection that compared the key to a literal would
        // read this as no form at all, and the sheet would never open.
        val projection = projection(
            statusMessageKey = "task.waiting_for_permission",
            inputRequest = "req-1",
        )

        assertEquals(TaskProjection.Wait.FIELD_VALUES, projection.wait)
        assertTrue(projection.hasInputRequest)
    }

    @Test
    fun `a message key that says form but carries no identity opens nothing`() {
        // There is nothing to spend a value against, so there is no form to
        // draw — and saying otherwise would put a sheet on screen whose submit
        // could only fail.
        val projection = projection(statusMessageKey = "task.waiting_for_field_values")

        assertNull(projection.wait)
        assertFalse(projection.hasInputRequest)
    }

    @Test
    fun `a handover wins over a form, because a sheet cannot answer it`() {
        val projection = projection(
            statusMessageKey = "task.waiting_for_handover",
            inputRequest = "req-1",
        )

        assertEquals(TaskProjection.Wait.HANDOVER, projection.wait)
        assertTrue(projection.hasHandover)
        assertFalse(projection.hasInputRequest)
    }

    @Test
    fun `an ask is still an ask, and is not one while a form is open`() {
        assertTrue(projection(statusMessageKey = "task.waiting_for_input").hasAsk)
        assertFalse(
            projection(
                statusMessageKey = "task.waiting_for_input",
                inputRequest = "req-1",
            ).hasAsk,
        )
    }

    @Test
    fun `at most one door is ever open`() {
        val keys = listOf(
            "task.waiting_for_handover",
            "task.waiting_for_input",
            "task.waiting_for_field_values",
            "task.acting",
            null,
        )
        val identities = listOf(null, "req-1")

        for (key in keys) {
            for (identity in identities) {
                val projection = projection(statusMessageKey = key, inputRequest = identity)
                val open = listOf(
                    projection.hasHandover,
                    projection.hasAsk,
                    projection.hasInputRequest,
                ).count { it }

                assertTrue("$key/$identity opened $open doors", open <= 1)
            }
        }
    }

    @Test
    fun `control admission requires the exact task and projected control`() {
        val state = TaskRepositoryState(
            availability = CoreUiAvailability.READY,
            generation = 7uL,
            task = projection(allowedControls = listOf(TaskControl.RESUME, TaskControl.STOP)),
        )

        assertTrue(state.admits("task-1", TaskControl.RESUME))
        assertTrue(state.admits("task-1", TaskControl.STOP))
        assertFalse(state.admits("task-1", TaskControl.PAUSE))
        assertFalse(state.admits("stale-task", TaskControl.STOP))
        assertFalse(state.copy(task = null).admits("task-1", TaskControl.STOP))
        assertTrue(state.admits("task-1", 1uL, TaskControl.RESUME))
        assertFalse(state.admits("task-1", 2uL, TaskControl.RESUME))
    }

    private fun projection(
        statusMessageKey: String? = null,
        inputRequest: String? = null,
        allowedControls: List<TaskControl> = emptyList(),
    ) = TaskProjection(
        id = "task-1",
        revision = 1uL,
        phase = TaskPhase.WAITING_FOR_USER,
        goal = "collect the document",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 0u,
        statusMessageKey = statusMessageKey,
        failure = null,
        pendingAction = null,
        inputRequest = inputRequest,
        allowedControls = allowedControls,
    )
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TaskChallengeKind
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskInputField
import com.taffygo.browser.ui.core.model.TaskInputRequest
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * What the browsing surface claims while a task is driving it, and the two
 * claims it must refuse to make.
 *
 * The frame is a statement that Taffy is working in this window; the cut-out is
 * a statement about a position on the page in front of the person. Both are
 * wrong in ways nobody would report as a bug — a frame left up after a task
 * ended reads as a browser that never gives the page back, and a cut-out drawn
 * over the wrong page points at whatever happens to be there.
 */
class TakeoverProjectionTest {

    @Test
    fun `no task is no frame and no cut-out`() {
        val state = projectTakeover(repository(task = null), null, "portal.example.test")

        assertFalse(state.active)
        assertFalse(state.showsFrame)
        assertFalse(state.showsHighlight)
        assertFalse(state.taskEnded)
    }

    /**
     * The claim the chrome row reads, which is deliberately wider than the one
     * the frame reads.
     *
     * `active` goes false the moment work stops, and every phase below that is
     * false is a phase where a task still has something to say — so a row that
     * read `active` would put the four-slot dock back under a pill that was
     * still saying "Done — 3 sources, 1 conflict".
     */
    @Test
    fun `every phase with a task to speak for keeps the chrome row's claim`() {
        TaskPhase.entries.forEach { phase ->
            val state = projectTakeover(repository(task(phase)), null, "portal.example.test")
            val expected = task(phase).displayState != null
            assertEquals("$phase", expected, state.hasTask)
        }
    }

    /**
     * The second half of the row's claim, and the one that gives the controls
     * back.
     *
     * A task that has ended still has its last line to say, so [hasTask] stays
     * true; what changes is that the page is the person's again, so back,
     * forward and tabs return to the row around the pill.
     */
    @Test
    fun `only a finished task hands the chrome row its controls back`() {
        TaskPhase.entries.forEach { phase ->
            val state = projectTakeover(repository(task(phase)), null, "portal.example.test")
            val display = task(phase).displayState
            assertEquals("$phase", display?.isFinal == true, state.taskEnded)
        }
    }

    @Test
    fun `no task is no claim for the chrome row either`() {
        val state = projectTakeover(repository(task = null), null, "portal.example.test")
        assertFalse(state.hasTask)
        assertFalse(state.taskEnded)
    }

    @Test
    fun `a task under way puts the frame on the page in front`() {
        val state = projectTakeover(
            repository(task(TaskPhase.RUNNING)),
            null,
            "portal.example.test",
        )

        assertTrue(state.active)
        assertTrue(state.showsFrame)
        // Still going, so the chrome row is the pill alone: this is the fact
        // that takes back, forward and tabs off it (decision 0141).
        assertFalse(state.taskEnded)
        assertFalse(state.waitingForYou)
        assertFalse(state.canTakeOver)
    }

    @Test
    fun `take over is shown only when this exact revision admits it`() {
        val state = projectTakeover(
            repository(task(TaskPhase.RUNNING, listOf(TaskControl.TAKE_OVER))),
            null,
            "portal.example.test",
        )

        assertTrue(state.active)
        assertTrue(state.canTakeOver)
    }

    @Test
    fun `a finished task takes the frame off in the same breath`() {
        listOf(
            TaskPhase.COMPLETED,
            TaskPhase.PARTIAL,
            TaskPhase.CANCELLED,
            TaskPhase.FAILED,
            TaskPhase.OUTCOME_UNKNOWN,
            TaskPhase.IDLE,
        ).forEach { phase ->
            val state = projectTakeover(repository(task(phase)), null, "portal.example.test")

            assertFalse("$phase kept the frame up", state.showsFrame)
        }
    }

    @Test
    fun `a task held on the person says so on the band`() {
        val state = projectTakeover(
            repository(task(TaskPhase.WAITING_FOR_USER)),
            null,
            "portal.example.test",
        )

        assertTrue(state.active)
        assertTrue(state.waitingForYou)
    }

    @Test
    fun `a page Taffy is working stops taking touches`() {
        val state = projectTakeover(
            repository(task(TaskPhase.RUNNING)),
            null,
            "portal.example.test",
        )

        assertTrue(state.showsLock)
    }

    @Test
    fun `a task waiting on the person gives the page back`() {
        val state = projectTakeover(
            repository(task(TaskPhase.WAITING_FOR_USER)),
            null,
            "portal.example.test",
        )

        assertTrue(state.active)
        assertTrue(state.waitingForYou)
        assertFalse(state.showsLock)
    }

    @Test
    fun `a marked widget is a hand-back, so the hold is off it`() {
        val state = projectTakeover(
            repository(task(TaskPhase.WAITING_FOR_USER)),
            interactiveRequest("portal.example.test"),
            "portal.example.test",
        )

        assertTrue(state.showsHighlight)
        assertFalse(state.showsLock)
    }

    @Test
    fun `a task that has ended stops holding the page`() {
        listOf(
            TaskPhase.COMPLETED,
            TaskPhase.PARTIAL,
            TaskPhase.CANCELLED,
            TaskPhase.FAILED,
        ).forEach { phase ->
            val state = projectTakeover(repository(task(phase)), null, "portal.example.test")

            assertFalse("$phase still holds the page", state.showsLock)
        }
    }

    @Test
    fun `a widget on this page is cut out of the shade`() {
        val state = projectTakeover(
            repository(task(TaskPhase.WAITING_FOR_USER)),
            interactiveRequest("portal.example.test"),
            "portal.example.test",
        )

        assertTrue(state.showsHighlight)
        assertEquals(0.2f, requireNotNull(state.highlight).leftFraction, 0f)
    }

    @Test
    fun `a widget on another page is not cut out of this one`() {
        val state = projectTakeover(
            repository(task(TaskPhase.WAITING_FOR_USER)),
            interactiveRequest("bank.example.test"),
            "portal.example.test",
        )

        assertTrue(state.active)
        assertNull(state.highlight)
        assertFalse(state.showsHighlight)
    }

    @Test
    fun `a form is not a place on the page, so it cuts nothing out`() {
        val typed = TaskInputRequest(
            requestId = "req-2",
            host = "portal.example.test",
            fields = listOf(TaskInputField(id = "reference", label = "Reference")),
        )

        val state = projectTakeover(
            repository(task(TaskPhase.WAITING_FOR_USER)),
            typed,
            "portal.example.test",
        )

        assertNull(state.highlight)
    }

    private fun interactiveRequest(host: String) = TaskInputRequest(
        requestId = "req-1",
        host = host,
        fields = listOf(
            TaskInputField(
                id = "widget",
                label = "Confirm you are a person",
                challenge = TaskChallengeKind.INTERACTIVE_CHALLENGE,
                highlight = TaskInputField.Highlight(0.2f, 0.3f, 0.8f, 0.45f),
            ),
        ),
    )

    private fun repository(task: TaskProjection?) = TaskRepositoryState(
        availability = CoreUiAvailability.READY,
        generation = 1uL,
        task = task,
    )

    private fun task(
        phase: TaskPhase,
        controls: List<TaskControl> = emptyList(),
    ) = TaskProjection(
        id = "task-1",
        revision = 1uL,
        phase = phase,
        goal = "get the document",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 0u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
        allowedControls = controls,
    )
}

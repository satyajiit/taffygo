// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.model.TaskChallengeKind
import com.taffygo.browser.ui.core.model.TaskInputField
import com.taffygo.browser.ui.core.model.TaskInputRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The form sheet's two pure halves, and the three rules they carry.
 *
 * Dismissing discards nothing; a redescription of the same request does not
 * empty a field somebody is half way through; and nothing about this state ever
 * prints what was typed.
 */
class TaskInputReducerTest {

    private val request = TaskInputRequest(
        requestId = "req-1",
        host = "portal.example.test",
        fields = listOf(
            TaskInputField(id = "reference", label = "Reference"),
            TaskInputField(
                id = "code",
                label = "Code",
                sensitive = true,
                challenge = TaskChallengeKind.ONE_TIME_CODE,
            ),
        ),
    )

    @Test
    fun `a form nobody has seen opens itself`() {
        val state = projectTaskInput(request, available = true)

        assertTrue(state.open)
        assertEquals("req-1", state.requestId)
        assertEquals("portal.example.test", state.host)
        assertEquals(listOf("reference", "code"), state.rows.map { it.id })
        assertFalse(state.interactive)
    }

    @Test
    fun `no request and no vault are both nothing to draw`() {
        assertFalse(projectTaskInput(null, available = true).hasRequest)
        assertFalse(projectTaskInput(request, available = false).hasRequest)
    }

    @Test
    fun `dismissing discards nothing and does not close the request`() {
        val typed = reduceTaskInput(
            projectTaskInput(request, available = true),
            TaskInputIntent.ValueChanged("reference", "AB-1234"),
        )
        val dismissed = reduceTaskInput(typed, TaskInputIntent.Dismiss)

        assertFalse(dismissed.open)
        assertTrue(dismissed.hasRequest)
        assertEquals("AB-1234", dismissed.rows.first { it.id == "reference" }.value)

        val reopened = reduceTaskInput(dismissed, TaskInputIntent.Reopen)
        assertTrue(reopened.open)
        assertEquals("AB-1234", reopened.rows.first { it.id == "reference" }.value)
    }

    @Test
    fun `a dismissed form stays dismissed when the browser describes it again`() {
        val dismissed = reduceTaskInput(
            projectTaskInput(request, available = true),
            TaskInputIntent.Dismiss,
        )
        val redescribed = projectTaskInput(request, available = true, previous = dismissed)

        assertFalse(redescribed.open)
        assertTrue(redescribed.hasRequest)
    }

    @Test
    fun `redescribing the same request keeps what is half typed`() {
        val typed = reduceTaskInput(
            projectTaskInput(request, available = true),
            TaskInputIntent.ValueChanged("code", "1234"),
        )
        // The same request with a relabelled row: the browser re-described it,
        // and the person is still typing into it.
        val again = request.copy(
            fields = request.fields.map {
                if (it.id == "code") TaskInputField(id = it.id, label = "One-time code") else it
            },
        )

        val projected = projectTaskInput(again, available = true, previous = typed)

        assertEquals("1234", projected.rows.first { it.id == "code" }.value)
        assertEquals("One-time code", projected.rows.first { it.id == "code" }.label)
    }

    @Test
    fun `a different request never inherits the last one's answers`() {
        val typed = reduceTaskInput(
            projectTaskInput(request, available = true),
            TaskInputIntent.ValueChanged("reference", "AB-1234"),
        )
        val next = request.copy(requestId = "req-2")

        val projected = projectTaskInput(next, available = true, previous = typed)

        assertTrue(projected.open)
        assertTrue(projected.rows.all { it.value.isEmpty() })
    }

    @Test
    fun `review is live only once every described row has an answer`() {
        val opened = projectTaskInput(request, available = true)
        assertFalse(opened.canReview)

        val one = reduceTaskInput(opened, TaskInputIntent.ValueChanged("reference", "AB-1234"))
        assertFalse(one.canReview)

        val both = reduceTaskInput(one, TaskInputIntent.ValueChanged("code", "123456"))
        assertTrue(both.canReview)
        val review = reduceTaskInput(both, TaskInputIntent.Submit)
        assertTrue(review.reviewing)
        assertTrue(review.canConfirm)
        assertFalse(review.copy(submitting = true).canConfirm)
    }

    @Test
    fun `editing after review requires the exact values to be reviewed again`() {
        val typed = request.fields.fold(projectTaskInput(request, available = true)) { state, field ->
            reduceTaskInput(state, TaskInputIntent.ValueChanged(field.id, "value-${field.id}"))
        }
        val review = reduceTaskInput(typed, TaskInputIntent.Submit)
        assertTrue(review.reviewing)

        val changed = reduceTaskInput(
            review,
            TaskInputIntent.ValueChanged("reference", "different"),
        )
        assertFalse(changed.reviewing)
        assertFalse(changed.canConfirm)
    }

    @Test
    fun `an edit clears the last refusal`() {
        val refused = projectTaskInput(request, available = true)
            .copy(failure = FailureReason.INVALID_REQUEST)

        val edited = reduceTaskInput(
            refused,
            TaskInputIntent.ValueChanged("reference", "AB-1234"),
        )

        assertNull(edited.failure)
    }

    @Test
    fun `a widget on the page has nothing to type into and its own control`() {
        val interactive = TaskInputRequest(
            requestId = "req-9",
            host = "portal.example.test",
            fields = listOf(
                TaskInputField(
                    id = "widget",
                    label = "Confirm you are a person",
                    challenge = TaskChallengeKind.INTERACTIVE_CHALLENGE,
                ),
            ),
        )

        val state = projectTaskInput(interactive, available = true)

        assertTrue(state.interactive)
        assertTrue(state.rows.isEmpty())
        assertFalse(state.canReview)
        assertTrue(state.canCompleteInteractive)
    }

    @Test
    fun `nothing about this state ever prints what was typed`() {
        val typed = reduceTaskInput(
            projectTaskInput(request, available = true),
            TaskInputIntent.ValueChanged("code", "927451"),
        )

        val printed = typed.toString() + typed.rows.joinToString { it.toString() }

        assertFalse(printed.contains("927451"))
        assertTrue(printed.contains("req-1"))
    }
}

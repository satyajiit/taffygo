// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The closed half of a form: which kinds exist, what a request is allowed to
 * be, and what a page is not trusted to say about where its widget is.
 *
 * The other half of the rule — one row this product cannot name refuses the
 * whole request — is where the description is classified, in `:core:task`, and
 * is held by `TaskInputProjectionTest` there.
 */
class TaskInputRequestTest {

    @Test
    fun `every declared kind round-trips through its compiled-in name`() {
        TaskChallengeKind.entries.forEach { kind ->
            assertEquals(kind, TaskChallengeKind.fromWire(kind.wireName))
        }
        assertEquals(4, TaskChallengeKind.entries.size)
    }

    @Test
    fun `a name this product does not know classifies as nothing`() {
        assertNull(TaskChallengeKind.fromWire("biometric_scan"))
        assertNull(TaskChallengeKind.fromWire(""))
        // Case matters: the wire names are compiled-in identifiers, not prose.
        assertNull(TaskChallengeKind.fromWire("ONE_TIME_CODE"))
    }

    @Test
    fun `a request keeps every row in the order it was described`() {
        val request = TaskInputRequest.of(
            requestId = "req-1",
            host = "example.test",
            fields = listOf(
                field(id = "a"),
                field(id = "b", challenge = TaskChallengeKind.ONE_TIME_CODE, sensitive = true),
            ),
        )

        assertEquals(listOf("a", "b"), request?.fields?.map { it.id })
        assertEquals(TaskChallengeKind.NONE, request?.fields?.get(0)?.challenge)
        assertTrue(request?.fields?.get(1)?.sensitive == true)
    }

    @Test
    fun `a request with nothing to ask for or nowhere to send it is not a request`() {
        assertNull(TaskInputRequest.of("req-1", "example.test", emptyList()))
        assertNull(TaskInputRequest.of("", "example.test", listOf(field())))
        assertNull(TaskInputRequest.of("req-1", "  ", listOf(field())))
    }

    @Test
    fun `an interactive challenge is a different surface and carries no typed row`() {
        val request = requireNotNull(
            TaskInputRequest.of(
                requestId = "req-1",
                host = "example.test",
                fields = listOf(
                    field(
                        id = "widget",
                        challenge = TaskChallengeKind.INTERACTIVE_CHALLENGE,
                        highlight = TaskInputField.Highlight(0.1f, 0.2f, 0.6f, 0.5f),
                    ),
                ),
            ),
        )
        val highlight = requireNotNull(request.interactiveHighlight)

        assertTrue(request.isInteractiveChallenge)
        assertTrue(request.typedFields.isEmpty())
        assertEquals(0.1f, highlight.leftFraction, TOLERANCE)
        assertEquals(0.5f, highlight.bottomFraction, TOLERANCE)
    }

    @Test
    fun `a highlight the page described out of bounds is clamped rather than trusted`() {
        val highlight = TaskInputField.Highlight(-4f, -1f, 90f, 12f)

        assertEquals(0f, highlight.leftFraction, TOLERANCE)
        assertEquals(0f, highlight.topFraction, TOLERANCE)
        assertEquals(1f, highlight.rightFraction, TOLERANCE)
        assertEquals(1f, highlight.bottomFraction, TOLERANCE)
        assertFalse(highlight.isEmpty)
    }

    @Test
    fun `an inverted highlight encloses nothing rather than drawing backwards`() {
        val highlight = TaskInputField.Highlight(0.8f, 0.9f, 0.2f, 0.1f)

        assertEquals(0.8f, highlight.leftFraction, TOLERANCE)
        assertEquals(0.8f, highlight.rightFraction, TOLERANCE)
        assertTrue(highlight.isEmpty)
    }

    @Test
    fun `a highlight nothing described leaves the overlay nothing to draw`() {
        val request = requireNotNull(
            TaskInputRequest.of(
                requestId = "req-1",
                host = "example.test",
                fields = listOf(
                    field(id = "widget", challenge = TaskChallengeKind.INTERACTIVE_CHALLENGE),
                ),
            ),
        )

        assertTrue(request.isInteractiveChallenge)
        assertNull(request.interactiveHighlight)
    }

    @Test
    fun `the same picture arriving twice compares equal`() {
        val first = field(challenge = TaskChallengeKind.IMAGE_CHALLENGE, image = byteArrayOf(1, 2))
        val second = field(challenge = TaskChallengeKind.IMAGE_CHALLENGE, image = byteArrayOf(1, 2))

        assertEquals(first, second)
        assertEquals(first.hashCode(), second.hashCode())
        assertEquals(
            TaskInputRequest.of("req-1", "example.test", listOf(first)),
            TaskInputRequest.of("req-1", "example.test", listOf(second)),
        )
    }

    @Test
    fun `a field never prints what a person might have typed`() {
        val printed = field(id = "code", sensitive = true).toString()

        assertEquals("TaskInputField(id=code, challenge=NONE)", printed)
    }

    private fun field(
        id: String = "a",
        challenge: TaskChallengeKind = TaskChallengeKind.NONE,
        sensitive: Boolean = false,
        image: ByteArray? = null,
        highlight: TaskInputField.Highlight? = null,
    ) = TaskInputField(
        id = id,
        label = id,
        sensitive = sensitive,
        challenge = challenge,
        challengeImage = image,
        highlight = highlight,
    )

    private companion object {
        /** Exact fractions only, so this is a comparison rather than a margin. */
        const val TOLERANCE = 0f
    }
}

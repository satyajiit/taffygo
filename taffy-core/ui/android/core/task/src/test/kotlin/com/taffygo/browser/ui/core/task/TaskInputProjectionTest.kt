// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.api.TaskInputClient
import com.taffygo.browser.ui.core.model.TaskChallengeKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The rule this seam exists for: a described row this product cannot name
 * refuses the whole form.
 *
 * Written as a test rather than as a comment because the alternative failure is
 * silent. A projection that dropped the unrecognised row would produce a form a
 * person can fill in and a site will reject, with nothing on the screen able to
 * say which part was never asked for.
 */
class TaskInputProjectionTest {

    @Test
    fun `nothing described is nothing to draw`() {
        assertNull(taskInputRequestFrom(null))
    }

    @Test
    fun `a described form becomes a request with every row in order`() {
        val request = taskInputRequestFrom(
            described(
                field("account", "none"),
                field("code", "one_time_code", sensitive = true),
            ),
        )

        assertNotNull(request)
        assertEquals(listOf("account", "code"), request?.fields?.map { it.id })
        assertEquals(TaskChallengeKind.NONE, request?.fields?.get(0)?.challenge)
        assertEquals(TaskChallengeKind.ONE_TIME_CODE, request?.fields?.get(1)?.challenge)
        assertTrue(request?.fields?.get(1)?.sensitive == true)
    }

    @Test
    fun `one row this product cannot name refuses the whole request`() {
        val request = taskInputRequestFrom(
            described(
                field("account", "none"),
                field("scan", "retina_scan"),
                field("code", "one_time_code"),
            ),
        )

        assertNull(request)
    }

    @Test
    fun `a form with nothing in it or nowhere to send it is refused`() {
        assertNull(taskInputRequestFrom(described()))
        assertNull(taskInputRequestFrom(described(field("a", "none"), requestId = "")))
        assertNull(taskInputRequestFrom(described(field("a", "none"), host = " ")))
    }

    @Test
    fun `a picture is carried as the bytes the browser already fetched`() {
        val request = taskInputRequestFrom(
            described(field("picture", "image_challenge", image = byteArrayOf(7, 8, 9))),
        )

        assertEquals(
            listOf<Byte>(7, 8, 9),
            request?.fields?.get(0)?.challengeImage?.toList(),
        )
    }

    @Test
    fun `a widget with no described place leaves the overlay nothing to draw`() {
        val request = taskInputRequestFrom(described(field("widget", "interactive_challenge")))

        assertTrue(request?.isInteractiveChallenge == true)
        assertNull(request?.interactiveHighlight)
        assertNull(request?.fields?.get(0)?.highlight)
    }

    @Test
    fun `a described place becomes the normalized rectangle the overlay reads`() {
        val request = taskInputRequestFrom(
            described(
                TaskInputClient.Described.Field(
                    id = "widget",
                    label = "widget",
                    challenge = "interactive_challenge",
                    highlightLeft = 0.25f,
                    highlightTop = 0.5f,
                    highlightRight = 0.75f,
                    highlightBottom = 0.6f,
                ),
            ),
        )
        val highlight = requireNotNull(request?.interactiveHighlight)

        assertEquals(0.25f, highlight.leftFraction, 0f)
        assertEquals(0.5f, highlight.topFraction, 0f)
        assertEquals(0.75f, highlight.rightFraction, 0f)
        assertEquals(0.6f, highlight.bottomFraction, 0f)
    }

    private fun described(
        vararg fields: TaskInputClient.Described.Field,
        requestId: String = "req-1",
        host: String = "example.test",
    ) = TaskInputClient.Described(requestId, host, fields.toList())

    private fun field(
        id: String,
        challenge: String,
        sensitive: Boolean = false,
        image: ByteArray? = null,
    ) = TaskInputClient.Described.Field(
        id = id,
        label = id,
        challenge = challenge,
        sensitive = sensitive,
        challengeImage = image,
    )
}

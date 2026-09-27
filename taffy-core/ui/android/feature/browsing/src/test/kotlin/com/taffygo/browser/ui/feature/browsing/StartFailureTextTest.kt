// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.toTaskStartFailure
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/**
 * What a refused start says under the box (decision 0231).
 *
 * A start the browser refused because every open tab was one Taffy had
 * opened reached the person as "Taffy could not read this request. Change it
 * and try again." No change to the request could fix that. These cases hold
 * the sentence to the reason, from the reason the transport hands up to the
 * string the line draws.
 */
class StartFailureTextTest {
    @Test
    fun `a page in none of the person's own tabs asks them to open it in a new tab`() {
        assertEquals(
            R.string.taffy_task_start_failed_source_not_open,
            startFailureText(FailureReason.SOURCE_NOT_OPEN.toTaskStartFailure()),
        )
    }

    @Test
    fun `a site two of the person's tabs show asks them to close the extra ones`() {
        assertEquals(
            R.string.taffy_task_start_failed_source_ambiguous,
            startFailureText(FailureReason.SOURCE_AMBIGUOUS.toTaskStartFailure()),
        )
    }

    @Test
    fun `not exactly one window in front asks them to close the other window`() {
        assertEquals(
            R.string.taffy_task_start_failed_window_unavailable,
            startFailureText(FailureReason.WINDOW_UNAVAILABLE.toTaskStartFailure()),
        )
    }

    @Test
    fun `only a request that could not be read says it could not be read`() {
        assertEquals(
            R.string.taffy_task_start_failed_invalid_request,
            startFailureText(FailureReason.INVALID_REQUEST.toTaskStartFailure()),
        )
        TaskStartFailure.entries
            .filter { it != TaskStartFailure.INVALID_REQUEST }
            .forEach { failure ->
                assertNotEquals(
                    "$failure borrowed the sentence for a request that could not be read",
                    R.string.taffy_task_start_failed_invalid_request,
                    startFailureText(failure),
                )
            }
    }

    @Test
    fun `every start failure has a sentence of its own`() {
        val sentences = TaskStartFailure.entries.map(::startFailureText)

        assertEquals(sentences.size, sentences.toSet().size)
    }
}

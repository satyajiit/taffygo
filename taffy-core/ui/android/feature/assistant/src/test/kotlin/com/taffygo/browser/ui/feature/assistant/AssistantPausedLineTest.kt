// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * What the bar says while a task is paused.
 *
 * A task that is still settling into its pause projects as the same phase as
 * one that has settled — decision 0150 left that distinction alone because
 * with its rule a task no longer stays settling. It still passes through, and
 * while it does the counts are the ones it held before it began stopping. On a
 * phone that read "Paused — 1 of 0 pages read": a denominator that was never
 * real, over a task that was not asking for anything.
 */
class AssistantPausedLineTest {
    private fun paused(
        statusMessageKey: String? = "task.paused",
        hasHandover: Boolean = false,
    ) = AssistantBarUiState(
        state = TaskDisplayState.PAUSED,
        statusMessageKey = statusMessageKey,
        hasHandover = hasHandover,
    )

    /**
     * Whether the count is printed as "N of M", which is decided here and not
     * in the composable so a host test can hold it.
     *
     * "Paused — 3 of 1 page read" was on a phone. An errand has no plan and
     * projects none; this is the second half, for any count that outgrows the
     * plan it is printed against — it has shown the plan was not what the task
     * read from, and a fraction over one is a number the product does not hold.
     */
    @Test
    fun `a count is printed against a plan only while it fits inside one`() {
        fun counted(read: Int, planned: Int) =
            AssistantBarUiState(
                state = TaskDisplayState.PAUSED,
                sourcesRead = read,
                sourcesPlanned = planned,
            ).countsReadAgainstPlan

        assertTrue(counted(read = 2, planned = 4))
        assertTrue(counted(read = 0, planned = 4))
        assertTrue(counted(read = 4, planned = 4))
        assertFalse(counted(read = 3, planned = 1))
        assertFalse(counted(read = 3, planned = 0))
        assertFalse(counted(read = 0, planned = 0))
    }

    @Test
    fun `a pause that is still settling says so rather than printing a count`() {
        assertEquals(
            R.string.taffy_assistant_pausing,
            pausedLine(paused(statusMessageKey = "task.pausing")),
        )
    }

    @Test
    fun `a settled pause with pages to count prints none of its own`() {
        assertNull(pausedLine(paused()))
    }

    @Test
    fun `a handed back page outranks a pause that is still settling`() {
        assertEquals(
            R.string.taffy_assistant_handover,
            pausedLine(paused(statusMessageKey = "task.pausing", hasHandover = true)),
        )
    }

    @Test
    fun `a provider limit names itself`() {
        assertEquals(
            R.string.taffy_assistant_paused_limit,
            pausedLine(paused(statusMessageKey = "task.paused_provider_limit")),
        )
    }

    @Test
    fun `being offline names itself`() {
        assertEquals(
            R.string.taffy_assistant_paused_offline,
            pausedLine(paused(statusMessageKey = "task.paused_offline")),
        )
    }

    /**
     * A busy provider is not the person's limit being reached.
     *
     * The two were one line, so an HTTP 5xx — the provider's own capacity,
     * which says nothing about an account — told somebody their limit was
     * reached and offered to switch models. On a phone that sentence appeared
     * over a plan with usage left (decision 0219).
     */
    @Test
    fun `a busy provider is not reported as the person's limit`() {
        val line = pausedLine(paused(statusMessageKey = "task.paused_provider_busy"))
        assertEquals(R.string.taffy_assistant_paused_busy, line)
        assertNotEquals(R.string.taffy_assistant_paused_limit, line)
    }

    /**
     * A lost answer is its own line and not the offline one. The device may
     * have a working network and the provider may already have billed for a
     * reply nobody received, so "you're offline" would be a guess presented as
     * a fact (decision 0217).
     */
    @Test
    fun `an answer that never arrived is not reported as being offline`() {
        val line = pausedLine(paused(statusMessageKey = "task.paused_no_answer"))
        assertEquals(R.string.taffy_assistant_paused_no_answer, line)
        assertNotEquals(R.string.taffy_assistant_paused_offline, line)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * What the bar says while a task is waiting on the person.
 *
 * The tab-approval plural used to be the fall-through arm, so a task waiting
 * for a field value printed its count anyway: a phone read "Taffy needs your
 * OK to open 0 more tabs" above a sheet asking for an Aadhaar number and a
 * captcha.
 */
class AssistantWaitingLineTest {
    private fun waiting(
        hasHandover: Boolean = false,
        hasInputRequest: Boolean = false,
        hasAsk: Boolean = false,
        approvalCount: Int = 0,
    ) = AssistantBarUiState(
        state = TaskDisplayState.WAITING_FOR_YOU,
        hasHandover = hasHandover,
        hasInputRequest = hasInputRequest,
        hasAsk = hasAsk,
        approvalCount = approvalCount,
    )

    @Test
    fun `a task waiting on a field value says so`() {
        assertEquals(
            R.string.taffy_assistant_input,
            waitingLine(waiting(hasInputRequest = true)),
        )
    }

    @Test
    fun `a task waiting on a field value it also asked about says so`() {
        assertEquals(
            R.string.taffy_assistant_input,
            waitingLine(waiting(hasInputRequest = true, hasAsk = true)),
        )
    }

    @Test
    fun `a wait with nothing to count does not print one`() {
        assertEquals(
            R.string.taffy_assistant_waiting_start,
            waitingLine(waiting()),
        )
    }

    @Test
    fun `an approval that covers something is the count`() {
        assertNull(waitingLine(waiting(approvalCount = 2)))
    }

    @Test
    fun `a handed back page outranks every other wait`() {
        assertEquals(
            R.string.taffy_assistant_handover,
            waitingLine(
                waiting(hasHandover = true, hasInputRequest = true, approvalCount = 3),
            ),
        )
    }
}

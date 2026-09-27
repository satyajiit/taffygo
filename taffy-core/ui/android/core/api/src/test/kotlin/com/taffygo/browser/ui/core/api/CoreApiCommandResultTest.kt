// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import java.util.concurrent.CancellationException
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** The transport seam is total at every Android repository boundary. */
class CoreApiCommandResultTest {
    @Test
    fun `acceptance returns success`() = runTest {
        assertEquals(TaffyResult.Success(Unit), submitCoreApiCommand {})
    }

    @Test
    fun `every typed refusal keeps its exact reason`() = runTest {
        val expected = mapOf(
            CoreApiSubmissionException.Reason.INVALID_REQUEST to FailureReason.INVALID_REQUEST,
            CoreApiSubmissionException.Reason.STALE_GENERATION to FailureReason.STALE_GENERATION,
            CoreApiSubmissionException.Reason.STALE_REVISION to FailureReason.STALE_REVISION,
            CoreApiSubmissionException.Reason.DEADLINE_EXCEEDED to FailureReason.DEADLINE_EXCEEDED,
            CoreApiSubmissionException.Reason.BACKPRESSURE to FailureReason.BACKPRESSURE,
            CoreApiSubmissionException.Reason.CORE_UNAVAILABLE to FailureReason.CORE_UNAVAILABLE,
            CoreApiSubmissionException.Reason.DUPLICATE to FailureReason.DUPLICATE,
            CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION to FailureReason.PROTOCOL_VIOLATION,
            CoreApiSubmissionException.Reason.SOURCE_NOT_OPEN to FailureReason.SOURCE_NOT_OPEN,
            CoreApiSubmissionException.Reason.SOURCE_AMBIGUOUS to FailureReason.SOURCE_AMBIGUOUS,
            CoreApiSubmissionException.Reason.WINDOW_UNAVAILABLE to FailureReason.WINDOW_UNAVAILABLE,
        )
        assertEquals(CoreApiSubmissionException.Reason.entries.toSet(), expected.keys)

        expected.forEach { (transport, domain) ->
            val result = submitCoreApiCommand { throw CoreApiSubmissionException(transport) }
            assertEquals(TaffyResult.Failure(domain), result)
        }
    }

    @Test
    fun `unexpected runtime failure is a closed protocol refusal`() = runTest {
        val result = submitCoreApiCommand { error("transport failed") }

        assertEquals(TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION), result)
    }

    @Test
    fun `structured cancellation remains cancellation`() {
        assertThrows(CancellationException::class.java) {
            runTest {
                submitCoreApiCommand { throw CancellationException("owner closed") }
            }
        }
    }
}

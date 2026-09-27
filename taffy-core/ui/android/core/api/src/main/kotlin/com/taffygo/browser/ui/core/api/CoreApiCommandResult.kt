// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import java.util.concurrent.CancellationException

/** Turns the throwing transport seam into the total result exposed by Android domain ports. */
suspend fun submitCoreApiCommand(command: suspend () -> Unit): TaffyResult<Unit> = try {
    command()
    TaffyResult.Success(Unit)
} catch (cancelled: CancellationException) {
    throw cancelled
} catch (failure: CoreApiSubmissionException) {
    TaffyResult.Failure(failure.reason.toFailureReason())
} catch (_: RuntimeException) {
    TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION)
}

private fun CoreApiSubmissionException.Reason.toFailureReason(): FailureReason = when (this) {
    CoreApiSubmissionException.Reason.INVALID_REQUEST -> FailureReason.INVALID_REQUEST
    CoreApiSubmissionException.Reason.STALE_GENERATION -> FailureReason.STALE_GENERATION
    CoreApiSubmissionException.Reason.STALE_REVISION -> FailureReason.STALE_REVISION
    CoreApiSubmissionException.Reason.DEADLINE_EXCEEDED -> FailureReason.DEADLINE_EXCEEDED
    CoreApiSubmissionException.Reason.BACKPRESSURE -> FailureReason.BACKPRESSURE
    CoreApiSubmissionException.Reason.CORE_UNAVAILABLE -> FailureReason.CORE_UNAVAILABLE
    CoreApiSubmissionException.Reason.DUPLICATE -> FailureReason.DUPLICATE
    CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION -> FailureReason.PROTOCOL_VIOLATION
    CoreApiSubmissionException.Reason.SOURCE_NOT_OPEN -> FailureReason.SOURCE_NOT_OPEN
    CoreApiSubmissionException.Reason.SOURCE_AMBIGUOUS -> FailureReason.SOURCE_AMBIGUOUS
    CoreApiSubmissionException.Reason.WINDOW_UNAVAILABLE -> FailureReason.WINDOW_UNAVAILABLE
}

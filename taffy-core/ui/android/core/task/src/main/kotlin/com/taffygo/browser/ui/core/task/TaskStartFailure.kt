// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.common.FailureReason

/**
 * A closed UI projection of a browser-owned task-start refusal.
 *
 * The last three are a request the browser could read and refused because of
 * the person's tabs or windows (decision 0231). Each has its own sentence,
 * because the one for INVALID_REQUEST asks for a change to the request, and no
 * change to the request fixes these.
 */
enum class TaskStartFailure {
    INVALID_REQUEST,
    STALE_GENERATION,
    STALE_REVISION,
    DEADLINE_EXCEEDED,
    BACKPRESSURE,
    CORE_UNAVAILABLE,
    DUPLICATE,
    PROTOCOL_VIOLATION,
    SOURCE_NOT_OPEN,
    SOURCE_AMBIGUOUS,
    WINDOW_UNAVAILABLE,
}

/** Preserve every closed Core API refusal without exposing transport values. */
fun FailureReason.toTaskStartFailure(): TaskStartFailure = when (this) {
    FailureReason.INVALID_REQUEST -> TaskStartFailure.INVALID_REQUEST
    FailureReason.STALE_GENERATION -> TaskStartFailure.STALE_GENERATION
    FailureReason.STALE_REVISION -> TaskStartFailure.STALE_REVISION
    FailureReason.DEADLINE_EXCEEDED -> TaskStartFailure.DEADLINE_EXCEEDED
    FailureReason.BACKPRESSURE -> TaskStartFailure.BACKPRESSURE
    FailureReason.CORE_UNAVAILABLE -> TaskStartFailure.CORE_UNAVAILABLE
    FailureReason.DUPLICATE -> TaskStartFailure.DUPLICATE
    FailureReason.SOURCE_NOT_OPEN -> TaskStartFailure.SOURCE_NOT_OPEN
    FailureReason.SOURCE_AMBIGUOUS -> TaskStartFailure.SOURCE_AMBIGUOUS
    FailureReason.WINDOW_UNAVAILABLE -> TaskStartFailure.WINDOW_UNAVAILABLE
    FailureReason.PROTOCOL_VIOLATION,
    FailureReason.OFFLINE,
    FailureReason.TIMED_OUT,
    FailureReason.NOT_FOUND,
    FailureReason.MALFORMED,
    FailureReason.UNSUPPORTED_VERSION,
    FailureReason.UNSUPPORTED_VALUE,
    FailureReason.NOT_PERMITTED,
    FailureReason.STORAGE_REFUSED,
    -> TaskStartFailure.PROTOCOL_VIOLATION
}

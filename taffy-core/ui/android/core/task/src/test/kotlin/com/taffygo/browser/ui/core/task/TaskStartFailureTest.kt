// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.common.FailureReason
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Every refusal the browser can answer a start with keeps its own member on
 * the way to the surface (decision 0231). A start refused over the person's
 * tabs or windows used to arrive as INVALID_REQUEST and be told to change a
 * request nothing was wrong with.
 */
class TaskStartFailureTest {
    @Test
    fun `each browser refusal keeps its exact member`() {
        val expected = mapOf(
            FailureReason.INVALID_REQUEST to TaskStartFailure.INVALID_REQUEST,
            FailureReason.STALE_GENERATION to TaskStartFailure.STALE_GENERATION,
            FailureReason.STALE_REVISION to TaskStartFailure.STALE_REVISION,
            FailureReason.DEADLINE_EXCEEDED to TaskStartFailure.DEADLINE_EXCEEDED,
            FailureReason.BACKPRESSURE to TaskStartFailure.BACKPRESSURE,
            FailureReason.CORE_UNAVAILABLE to TaskStartFailure.CORE_UNAVAILABLE,
            FailureReason.DUPLICATE to TaskStartFailure.DUPLICATE,
            FailureReason.SOURCE_NOT_OPEN to TaskStartFailure.SOURCE_NOT_OPEN,
            FailureReason.SOURCE_AMBIGUOUS to TaskStartFailure.SOURCE_AMBIGUOUS,
            FailureReason.WINDOW_UNAVAILABLE to TaskStartFailure.WINDOW_UNAVAILABLE,
        )

        expected.forEach { (reason, failure) -> assertEquals(failure, reason.toTaskStartFailure()) }
    }

    @Test
    fun `a reason the start seam never answers is a protocol violation`() {
        val browserRefusals = setOf(
            FailureReason.INVALID_REQUEST,
            FailureReason.STALE_GENERATION,
            FailureReason.STALE_REVISION,
            FailureReason.DEADLINE_EXCEEDED,
            FailureReason.BACKPRESSURE,
            FailureReason.CORE_UNAVAILABLE,
            FailureReason.DUPLICATE,
            FailureReason.SOURCE_NOT_OPEN,
            FailureReason.SOURCE_AMBIGUOUS,
            FailureReason.WINDOW_UNAVAILABLE,
        )

        FailureReason.entries.filterNot { it in browserRefusals }.forEach { reason ->
            assertEquals(TaskStartFailure.PROTOCOL_VIOLATION, reason.toTaskStartFailure())
        }
    }
}

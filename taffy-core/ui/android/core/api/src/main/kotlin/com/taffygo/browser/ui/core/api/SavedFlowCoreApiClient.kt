// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.SavedFlowQueryResult

/** Transient reviews and explicit manual navigation; never task execution authority. */
interface SavedFlowCoreApiClient {
    suspend fun findSavedFlows(requestId: String, goal: String): SavedFlowQueryResult =
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)

    suspend fun getSavedFlowReview(
        requestId: String,
        skillId: String,
        expectedVersion: UInt,
    ): SavedFlowQueryResult =
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)

    suspend fun openSavedFlowStart(requestId: String, skillId: String, expectedVersion: UInt): Unit =
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
}

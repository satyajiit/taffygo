// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
package org.chromium.taffy.host

import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import taffy.core_api.SavedFlowQueryResult

/** Query transport reuses the endpoint's cancellation and connection ledger. */
internal class CoreApiSavedFlowOperations(
    private val proxy: TaffyProfileCoreApi,
    private val submissions: CoreApiSubmissionDispatcher,
) {
    suspend fun find(requestId: String, goal: String): SavedFlowQueryResult =
        submissions.submitResult(
            call = { callback ->
                proxy.findSavedFlows(requestId, goal) { result ->
                    callback(CoreApiSubmissionStatus.ACCEPTED, projectSavedFlowQuery(result, requestId))
                }
            },
            validateAccepted = { it },
        )

    suspend fun review(requestId: String, skillId: String, expectedVersion: UInt): SavedFlowQueryResult =
        submissions.submitResult(
            call = { callback ->
                proxy.getSavedFlowReview(requestId, skillId, expectedVersion.toInt()) { result ->
                    callback(CoreApiSubmissionStatus.ACCEPTED, projectSavedFlowQuery(result, requestId))
                }
            },
            validateAccepted = { it },
        )

    suspend fun open(requestId: String, skillId: String, expectedVersion: UInt) =
        submissions.submit { callback ->
            proxy.openSavedFlowStart(requestId, skillId, expectedVersion.toInt(), callback)
        }
}

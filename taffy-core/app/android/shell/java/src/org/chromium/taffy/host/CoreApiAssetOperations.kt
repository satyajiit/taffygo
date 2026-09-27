// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Handler
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.api.PartMemberRead
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi

/** Owns asset commands and the non-submission member-read operation. */
internal class CoreApiAssetOperations(
    private val proxy: TaffyProfileCoreApi,
    private val submissions: CoreApiSubmissionDispatcher,
    private val mainHandler: Handler,
    private val closed: AtomicBoolean,
    private val onTransportFailure: () -> Unit,
) {
    suspend fun request(assetId: String, assetRevision: String) =
        submissions.submit { callback -> proxy.requestAsset(assetId, assetRevision, callback) }

    suspend fun remove(assetId: String, assetRevision: String) =
        submissions.submit { callback -> proxy.removeAsset(assetId, assetRevision, callback) }

    suspend fun readPartMember(assetId: String, memberPath: String): PartMemberRead =
        suspendCancellableCoroutine { continuation ->
            // This is a read, not a submission: no admission or far-side cancellation exists.
            val posted = mainHandler.post {
                if (!continuation.isActive) return@post
                if (closed.get()) {
                    continuation.resumeWithException(unavailable())
                    return@post
                }
                try {
                    proxy.readPartMember(assetId, memberPath) { status, bytes ->
                        if (continuation.isActive) {
                            continuation.resume(partMemberRead(status, bytes))
                        }
                    }
                } catch (_: RuntimeException) {
                    if (continuation.isActive) {
                        continuation.resumeWithException(unavailable())
                    }
                    onTransportFailure()
                }
            }
            if (!posted && continuation.isActive) {
                continuation.resumeWithException(unavailable())
            }
        }

    private fun unavailable() =
        CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
}
